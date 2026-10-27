# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools repo pr-health`: list the open pull requests that are stuck, with the cause and who should act.

One `gh pr list --json` read of the open pull requests (with each one's check rollup) and one read of the branch
ruleset, which says which checks are required. A pull request is stuck when a required check failed, when it conflicts
with the default branch, when a required check has been queued or running longer than
`hygiene.thresholds.pr_pending_hours`, or when it targets the default branch, is not a draft and has auto-merge
off. Failing checks that are not required are ignored, a draft is never stuck, and a stacked pull request (base other
than the default branch) is reported as stacked, never as lacking auto-merge. GitHub answers `UNKNOWN` for mergeability
while it computes it; that is "not known yet", shown only with `--verbose`. Without `--labels` and `--rerun` the
command only reads GitHub. `--labels` also compares each pull request's labels with its causes and, with `--apply`,
adds and removes `hygiene.pr_health.labels` (`S-conflict`, `S-failing`) through `gh` (`coney_tools.pr_labels`; the
`hygiene` workflow runs it on every push to the default branch). `--rerun` (see `coney_tools.pr_rerun`) adds the failed
required checks that are stale and would be rerun; only `--rerun --apply` calls `gh run rerun`.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any, NamedTuple

from coney_tools.audit_github import Runner, run_gh
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import PR_HEALTH_CAUSES, Conventions, load_conventions
from coney_tools.pr_labels import LabelChange, plan, unknown_numbers, write
from coney_tools.pr_rerun import FailedCheck, PullChecks, Rerun, plan_reruns

#: The JSON fields read from `gh pr list`.
PULL_FIELDS = "number,title,headRefName,baseRefName,isDraft,mergeable,autoMergeRequest,statusCheckRollup,labels,url"
#: The fields `--rerun` reads besides those: the head commit, and the description to check against the template.
RERUN_FIELDS = f"{PULL_FIELDS},headRefOid,body"
#: More open pull requests than this are not expected; the command says so when it reaches it.
LIMIT = 200

#: Check-run conclusions (and status-context states) that block a merge.
FAILED = frozenset({"FAILURE", "TIMED_OUT", "CANCELLED", "STARTUP_FAILURE", "ACTION_REQUIRED", "STALE", "ERROR"})
#: Check-run conclusions that count as passing for a required check (GitHub treats a skipped one as passing).
PASSED = frozenset({"SUCCESS", "NEUTRAL", "SKIPPED"})

#: The causes, in the order one pull request lists them.
CAUSES = PR_HEALTH_CAUSES
#: Who acts on each cause: the author fixes their branch; the coordinator unsticks CI and merging.
WHO = {"checks-failed": "author", "conflict": "author", "ci-stuck": "coordinator", "auto-merge-off": "coordinator"}


class CheckState(NamedTuple):
    """One check of a pull request, reduced to what the command needs."""

    name: str
    #: `failed`, `pending` or `passed`.
    state: str
    #: When it started (or was queued); None when GitHub gave no time.
    started: datetime | None
    #: The check run's conclusion in upper case (a status context's state); empty while it has none.
    conclusion: str = ""
    #: The check's `detailsUrl` (it holds the run and job ids) and its workflow; empty for a commit status.
    url: str = ""
    workflow: str = ""


@dataclass(frozen=True)
class Stuck:
    """One reason a pull request is stuck."""

    number: int
    branch: str
    cause: str
    #: What the cause is about: the failing checks with their class, the age of a pending check, the base.
    detail: str
    #: `author` or `coordinator`.
    who: str
    url: str = ""
    #: The checks involved (failing or pending), for scripts.
    checks: tuple[str, ...] = ()
    #: `lint` or `code` for `checks-failed`; empty otherwise.
    kind: str = ""

    def line(self) -> str:
        """The text line: `#<n> <branch> <cause> <detail> -> <who>`."""
        return f"#{self.number} {self.branch} {self.cause} {self.detail} -> {self.who}"

    def as_json(self) -> dict[str, object]:
        """The finding as a JSON object."""
        return {
            "number": self.number,
            "branch": self.branch,
            "cause": self.cause,
            "kind": self.kind,
            "detail": self.detail,
            "who": self.who,
            "checks": list(self.checks),
            "url": self.url,
        }


@dataclass(frozen=True)
class Note:
    """A pull request the command looked at and did not call stuck, and why (shown with `--verbose`)."""

    number: int
    branch: str
    #: `draft`, `stacked` or `mergeable-unknown`.
    what: str
    detail: str = ""

    def line(self) -> str:
        """The text line, in the same shape as a finding."""
        return f"#{self.number} {self.branch} {self.what}" + (f" {self.detail}" if self.detail else "")

    def as_json(self) -> dict[str, object]:
        """The note as a JSON object."""
        return {"number": self.number, "branch": self.branch, "what": self.what, "detail": self.detail}


@dataclass
class Health:
    """What the command found."""

    stuck: list[Stuck] = field(default_factory=list)
    notes: list[Note] = field(default_factory=list)
    #: Things that could not be judged (no required checks in the ruleset, too many pull requests).
    notices: list[str] = field(default_factory=list)
    #: How many open pull requests were looked at.
    checked: int = 0
    #: The label changes `--labels` found (empty without it).
    label_changes: list[LabelChange] = field(default_factory=list)
    #: The reruns planned or done (`--rerun`); None when the option was not given.
    reruns: list[Rerun] | None = None

    def as_json(self) -> dict[str, object]:
        """The report as the JSON object scripts read; `rerun` is there only with `--rerun`."""
        report: dict[str, object] = {
            "checked": self.checked,
            "stuck": [item.as_json() for item in self.stuck],
            "other": [item.as_json() for item in self.notes],
            "notices": list(self.notices),
            "labels": [item.as_json() for item in self.label_changes],
        }
        if self.reruns is not None:
            report["rerun"] = [item.as_json() for item in self.reruns]
        return report


# --- reading the data ---------------------------------------------------------------------------------------------


def _moment(value: object) -> datetime | None:
    """A GitHub timestamp as a datetime, or None when it is absent or malformed (GitHub writes 0001-01-01 for none)."""
    if not isinstance(value, str) or not value or value.startswith("0001"):
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def checks_of(rollup: object) -> list[CheckState]:
    """The latest state of each check in a pull request's `statusCheckRollup`.

    Entries are check runs (`name`, `status`, `conclusion`) or commit statuses (`context`, `state`). When one name
    appears twice (a rerun) the newest wins.
    """
    latest: dict[str, tuple[datetime, CheckState]] = {}
    for entry in rollup if isinstance(rollup, list) else []:
        if not isinstance(entry, dict):
            continue
        name = str(entry.get("name") or entry.get("context") or "")
        if not name:
            continue
        started = _moment(entry.get("startedAt"))
        result = str(entry.get("conclusion") or entry.get("state") or "").upper()
        complete = str(entry.get("status") or "COMPLETED").upper() == "COMPLETED"
        if complete and result in FAILED:
            state = "failed"
        elif complete and result in PASSED:
            state = "passed"
        else:
            # No conclusion yet: queued, running, waiting or a status still PENDING or EXPECTED.
            state = "pending"
        stamp = _moment(entry.get("completedAt")) or started or datetime.min.replace(tzinfo=UTC)
        if name not in latest or stamp >= latest[name][0]:
            details, workflow = str(entry.get("detailsUrl") or ""), str(entry.get("workflowName") or "")
            latest[name] = (stamp, CheckState(name, state, started, result, details, workflow))
    return [state for _, state in latest.values()]


def required_from_rules(rules: object) -> tuple[str, ...]:
    """The required status check names in the answer of `rules/branches/<branch>`, in the order given."""
    names: list[str] = []
    for rule in rules if isinstance(rules, list) else []:
        if not isinstance(rule, dict) or rule.get("type") != "required_status_checks":
            continue
        parameters = rule.get("parameters")
        for item in (parameters or {}).get("required_status_checks", []) if isinstance(parameters, dict) else []:
            context = item.get("context") if isinstance(item, dict) else None
            if isinstance(context, str) and context not in names:
                names.append(context)
    return tuple(names)


# --- judging ------------------------------------------------------------------------------------------------------


def _hours(span: timedelta) -> str:
    """A duration as `3.4 h`."""
    return f"{span.total_seconds() / 3600:.1f} h"


def judge(
    pulls: Sequence[dict[str, Any]],
    required: Sequence[str],
    conventions: Conventions,
    now: datetime,
    pending_hours: float | None = None,
) -> Health:
    """Decide which of the open `pulls` (gh's JSON) are stuck. `required` is the ruleset's list of required checks.

    `pending_hours` overrides `hygiene.thresholds.pr_pending_hours`.
    """
    limit = timedelta(hours=conventions.hygiene.thresholds.pr_pending_hours if pending_hours is None else pending_hours)
    lint = set(conventions.hygiene.pr_health.lint_checks)
    default = conventions.repo.default_branch
    health = Health(checked=len(pulls))
    for pull in sorted(pulls, key=lambda item: int(item["number"])):
        number, branch, url = int(pull["number"]), str(pull.get("headRefName", "")), str(pull.get("url", ""))
        base = str(pull.get("baseRefName", ""))
        if pull.get("isDraft"):
            health.notes.append(Note(number, branch, "draft"))
            continue
        if base != default:
            health.notes.append(Note(number, branch, "stacked", f"on {base}"))
        found: list[Stuck] = []
        checks = [check for check in checks_of(pull.get("statusCheckRollup")) if check.name in required]
        failing = sorted(check.name for check in checks if check.state == "failed")
        if failing:
            code = [name for name in failing if name not in lint]
            kind = "code" if code else "lint"
            # A pull request failing both kinds lists the code failures first: they take the longer fix.
            groups = [("code", code), ("lint", [name for name in failing if name in lint])]
            detail = "; ".join(f"{label}: {', '.join(names)}" for label, names in groups if names)
            found.append(
                Stuck(number, branch, "checks-failed", detail, WHO["checks-failed"], url, tuple(failing), kind)
            )
        mergeable = str(pull.get("mergeable") or "UNKNOWN").upper()
        if mergeable == "CONFLICTING":
            found.append(Stuck(number, branch, "conflict", f"with {base}", WHO["conflict"], url))
        elif mergeable != "MERGEABLE":
            health.notes.append(Note(number, branch, "mergeable-unknown", "GitHub has not computed it yet"))
        slow = [
            (check.name, now - check.started)
            for check in checks
            if check.state == "pending" and check.started is not None and now - check.started > limit
        ]
        if slow:
            detail = ", ".join(f"{name} {_hours(age)}" for name, age in sorted(slow))
            names = tuple(name for name, _ in sorted(slow))
            found.append(
                Stuck(number, branch, "ci-stuck", f"{detail} (limit {_hours(limit)})", WHO["ci-stuck"], url, names)
            )
        if base == default and not pull.get("autoMergeRequest"):
            found.append(Stuck(number, branch, "auto-merge-off", f"into {base}", WHO["auto-merge-off"], url))
        health.stuck.extend(sorted(found, key=lambda item: CAUSES.index(item.cause)))
    return health


# --- the command --------------------------------------------------------------------------------------------------


def fetch(
    default_branch: str, run: Runner = run_gh, fields: str = PULL_FIELDS
) -> tuple[list[dict[str, Any]], tuple[str, ...]]:
    """Read the open pull requests (`fields` of each) and the default branch's required checks.

    ConfigError when `gh` cannot.
    """
    try:
        pulls = json.loads(run(["pr", "list", "--state", "open", "--limit", str(LIMIT), "--json", fields]))
        rules = json.loads(run(["api", f"repos/{{owner}}/{{repo}}/rules/branches/{default_branch}"]))
    except (OSError, ValueError) as error:
        raise ConfigError(f"cannot read GitHub with gh ({error}); sign in with `gh auth login`") from error
    if not isinstance(pulls, list):
        raise ConfigError("gh pr list did not return a list")
    return pulls, required_from_rules(rules)


def pull_checks(pull: dict[str, Any], required: Sequence[str]) -> PullChecks:
    """What `--rerun` needs of one pull request (gh's JSON): its text, head commit and failed checks.

    A draft has none: it is never rerun, as it is never stuck.
    """
    failed = tuple(
        FailedCheck(check.name, check.conclusion, check.url, check.workflow)
        for check in checks_of(pull.get("statusCheckRollup"))
        if check.state == "failed" and not pull.get("isDraft")
    )
    return PullChecks(
        number=int(pull["number"]),
        branch=str(pull.get("headRefName", "")),
        title=str(pull.get("title") or ""),
        body=str(pull.get("body") or ""),
        head=str(pull.get("headRefOid") or ""),
        required=frozenset(required),
        failed=failed,
    )


def format_text(health: Health, verbose: bool) -> Iterable[str]:
    """The report lines: one per cause, the reruns, then (verbose) the pull requests not called stuck."""
    yield from (item.line() for item in health.stuck)
    yield from (item.line() for item in health.reruns or [])
    if verbose:
        yield from (f"{item.line()} (not stuck)" for item in health.notes)


def add_pr_health_parser(pr_commands: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add `pr list-stuck` and its options to the `pr` command group."""
    parser = pr_commands.add_parser(
        "list-stuck",
        help="list the open pull requests that are stuck (failed required check, conflict, stuck CI, no auto-merge)",
        description=(
            "Lists the open pull requests that are stuck, one line each: #number branch cause detail -> who acts. "
            "Required checks come from the branch ruleset. Exit code 0 when nothing is stuck, 1 when something is, "
            "2 when gh or the conventions cannot be read. Reads GitHub only, unless --apply with --labels or --rerun."
        ),
    )
    parser.add_argument("--json", action="store_true", help="print the report as JSON instead of text")
    parser.add_argument(
        "--pending-hours",
        type=float,
        metavar="H",
        help="hours a required check may be pending (default: hygiene.thresholds.pr_pending_hours)",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="also show drafts, stacked pull requests and unknown mergeability"
    )
    parser.add_argument(
        "--labels",
        action="store_true",
        help="also list the labels to add to or remove from the open pull requests (hygiene.pr_health.labels)",
    )
    parser.add_argument(
        "--rerun",
        action="store_true",
        help="also list the failed required checks that are stale (text fixed since, runner lost) and would be rerun",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="with --labels: make those label changes through gh; with --rerun: really rerun the checks (gh run rerun)",
    )


def run_pr_health(
    args: argparse.Namespace,
    run: Runner = run_gh,
    now: datetime | None = None,
    sleep: Callable[[float], None] = time.sleep,
) -> int:
    """Run `coney-tools repo pr-health`; 0 when nothing is stuck, 1 when something is.

    With `--labels`, GitHub is reread (after `sleep`) while it still answers `UNKNOWN` for a pull request's
    mergeability, up to `hygiene.pr_health.unknown_retries` times, before the label changes are printed or applied.
    """
    if args.apply and not (args.labels or args.rerun):
        raise ConfigError("--apply goes with --labels or --rerun")
    conventions = load_conventions(find_repo_root(Path.cwd()))
    fields = RERUN_FIELDS if args.rerun else PULL_FIELDS
    pulls, required = fetch(conventions.repo.default_branch, run, fields)
    health = judge(pulls, required, conventions, now or datetime.now(UTC), args.pending_hours)
    if args.labels:
        rules = conventions.hygiene.pr_health
        for _ in range(rules.unknown_retries):
            if not unknown_numbers(health):
                break
            sleep(rules.unknown_wait_seconds)
            pulls, required = fetch(conventions.repo.default_branch, run, fields)
            health = judge(pulls, required, conventions, now or datetime.now(UTC), args.pending_hours)
        health.label_changes = plan(pulls, health, rules)
    if args.rerun:
        health.reruns = plan_reruns([pull_checks(pull, required) for pull in pulls], conventions, run, args.apply)
    if not required:
        health.notices.append(f"the {conventions.repo.default_branch} ruleset requires no status checks")
    if len(pulls) >= LIMIT:
        health.notices.append(f"{len(pulls)} pull requests read: the limit, so more may be open")
    if args.json:
        print(json.dumps(health.as_json(), indent=1, ensure_ascii=False))
    else:
        for line in format_text(health, args.verbose):
            print(line)
        for change in health.label_changes:
            print(change.line(args.apply))
        for text in health.notices:
            print(f"coney-tools: note: {text}", file=sys.stderr)
    if args.apply and args.labels:
        write(health.label_changes, run)
    return 1 if health.stuck else 0
