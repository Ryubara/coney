# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools repo pr-health --rerun`: restart the required checks that failed for a reason that no longer holds.

A required check of an open pull request is rerun only when its failure is stale, never when the code is at fault:

1. the `title` or `body` check failed but the pull request's current title or description passes `repo check-title` or
   `repo check-body` (the text was fixed after the run);
2. the check concluded `cancelled`, `startup_failure` or `timed_out`, or its log shows the runner or GitHub failed
   (a pattern of `hygiene.pr_health.rerun.log_patterns`).

A real lint, build or test failure is left to the author. A workflow is rerun at most `rerun.max_per_sha` times on one
head commit (counted from the attempts GitHub keeps, so no local state can be lost), which keeps a flaky job from
looping. Without `apply` nothing is changed: the plan is only printed. When GitHub refuses a rerun the result says
which new event the pull request needs instead (a push, or an edit of the title or description).
"""

from __future__ import annotations

import json
import re
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field

from coney_tools.audit_github import Runner
from coney_tools.conventions import Conventions
from coney_tools.repo_checks import check_body, check_title, first_line

#: The run and job of a check's `detailsUrl`: `.../actions/runs/<run>/job/<job>`.
_RUN_URL = re.compile(r"/actions/runs/(?P<run>\d+)/jobs?/(?P<job>\d+)")
#: Said when a stale title or description is reported: a rerun cannot pick up the fix.
NO_RERUN = " (a rerun reuses the event the run started from, with the old text)"
#: How many runs of one commit are read; a commit has a handful.
RUN_LIMIT = 100


@dataclass(frozen=True)
class FailedCheck:
    """One failed check of a pull request, as `gh pr list` reports it."""

    name: str
    #: The check run's conclusion in upper case (`FAILURE`, `CANCELLED`, ...).
    conclusion: str
    #: The check's `detailsUrl`, which holds the run and job ids.
    url: str
    #: The workflow the check belongs to (`pr`, `build`).
    workflow: str


@dataclass(frozen=True)
class PullChecks:
    """What the rerun needs of one open pull request."""

    number: int
    branch: str
    #: The current title and description (they may have been edited since the failed run).
    title: str
    body: str
    #: The head commit.
    head: str
    #: The required checks of the base branch.
    required: frozenset[str]
    #: Every check whose latest state is failed, required or not.
    failed: tuple[FailedCheck, ...]


@dataclass(frozen=True)
class Rerun:
    """One run to restart (or, `capped` or `refused`, not restarted) and why."""

    number: int
    branch: str
    run: int
    #: The stale required checks of that run, sorted.
    checks: tuple[str, ...]
    #: Why each is stale, in the order of `checks`.
    reasons: tuple[str, ...]
    #: `would-rerun` (a dry run), `reran`, `refused` (GitHub would not), `capped` (already rerun the allowed times) or
    #: `new-event` (a fixed title or description: only a new event, not a rerun, makes the check see the fix).
    status: str
    #: For `refused`, `capped` and `new-event`: what happened and which new event the pull request needs instead.
    detail: str = ""

    def line(self) -> str:
        """The text line: `#<n> <branch> rerun <checks> (run <id>): <reasons> -> <outcome>`."""
        outcome = {"would-rerun": "would rerun", "reran": "reran"}.get(self.status, f"{self.status}: {self.detail}")
        why = "; ".join(dict.fromkeys(self.reasons))
        return f"#{self.number} {self.branch} rerun {', '.join(self.checks)} (run {self.run}): {why} -> {outcome}"

    def as_json(self) -> dict[str, object]:
        """The result as a JSON object."""
        return {
            "number": self.number,
            "branch": self.branch,
            "run": self.run,
            "checks": list(self.checks),
            "reasons": list(self.reasons),
            "status": self.status,
            "detail": self.detail,
        }


@dataclass(frozen=True)
class _Stale:
    """A failed required check that is stale: which kind, why, and its run and job."""

    check: FailedCheck
    kind: str
    reason: str
    run: int
    job: int


@dataclass
class _Commit:
    """What GitHub says about the runs of a head commit."""

    #: Run id to its workflow.
    workflows: dict[int, str] = field(default_factory=dict)
    #: Workflow to the reruns already made of it on this commit (the attempts beyond the first of its runs, summed).
    reruns: dict[str, int] = field(default_factory=dict)


def _ids(url: str) -> tuple[int, int] | None:
    """The (run, job) ids in a check's URL, or None when it has none."""
    found = _RUN_URL.search(url)
    return (int(found.group("run")), int(found.group("job"))) if found else None


def _read_commit(head: str, run: Runner) -> _Commit:
    """Read the workflow runs of `head` with their attempts; empty when `gh` cannot (nothing is then rerun)."""
    arguments = [
        "run",
        "list",
        "--commit",
        head,
        "--limit",
        str(RUN_LIMIT),
        "--json",
        "databaseId,workflowName,attempt",
    ]
    try:
        runs = json.loads(run(arguments))
    except (OSError, ValueError):
        return _Commit()
    commit = _Commit()
    for item in runs if isinstance(runs, list) else []:
        if not isinstance(item, dict) or not isinstance(item.get("databaseId"), int):
            continue
        workflow, attempt = str(item.get("workflowName", "")), item.get("attempt")
        commit.workflows[item["databaseId"]] = workflow
        commit.reruns[workflow] = commit.reruns.get(workflow, 0) + (attempt - 1 if isinstance(attempt, int) else 0)
    return commit


def _stale_text(pull: PullChecks, check: FailedCheck, conventions: Conventions) -> tuple[str, str] | None:
    """The (kind, reason) when `check` is the title or description check and the current text passes it."""
    rules = conventions.hygiene.pr_health.rerun
    if check.name == rules.title_check and not check_title(first_line(pull.title), conventions.commits.title):
        return "title", "the title now passes check-title"
    if check.name == rules.body_check and not check_body(pull.body, conventions.pull_requests):
        return "description", "the description now passes check-body"
    return None


def _runner_lost(job: int, conventions: Conventions, run: Runner) -> str | None:
    """The reason when the log of `job` matches a pattern of a runner or GitHub failure; None when none does."""
    try:
        log = run(["api", "--allow-escape-sequences", f"repos/{{owner}}/{{repo}}/actions/jobs/{job}/logs"])
    except OSError:
        return None
    for pattern in conventions.hygiene.pr_health.rerun.log_patterns:
        found = re.search(pattern, log, re.IGNORECASE)
        if found:
            return f"the log shows '{found.group(0)}'"
    return None


def _advice(stale: Sequence[_Stale]) -> str:
    """The new event a pull request needs when its run cannot be rerun."""
    edits = [f"the {item}" for item in ("title", "description") if any(entry.kind == item for entry in stale)]
    options = [f"edit {' or '.join(edits)}"] if edits else []
    if any(entry.kind == "runner" for entry in stale):
        options.append("push a commit")
    return "needs a new event: " + " or ".join(options)


def _find_stale(
    pull: PullChecks, conventions: Conventions, run: Runner, commit: Callable[[], _Commit]
) -> tuple[list[_Stale], list[_Stale], list[_Stale]]:
    """The stale required checks of `pull`: those to rerun, those the cap holds back, and the stale text ones."""
    rules = conventions.hygiene.pr_health.rerun
    allowed: list[_Stale] = []
    capped: list[_Stale] = []
    text: list[_Stale] = []
    for check in pull.failed:
        ids = _ids(check.url)
        if check.name not in pull.required or ids is None:
            continue
        found = _stale_text(pull, check, conventions)
        if found:
            text.append(_Stale(check, found[0], found[1], *ids))
            continue
        if check.conclusion not in rules.conclusions and check.conclusion != "FAILURE":
            continue
        kind, reason = "runner", f"concluded {check.conclusion.lower()}"
        known = commit()
        if ids[0] not in known.workflows:
            continue  # a run of another commit, or one GitHub did not list: not about this head
        over = known.reruns.get(known.workflows[ids[0]], 0) >= rules.max_per_sha
        if check.conclusion not in rules.conclusions:
            # A plain failure is stale only if its log shows the runner failed; at the cap the log is not even read.
            lost = None if over else _runner_lost(ids[1], conventions, run)
            if lost is None:
                continue
            reason = lost
        (capped if over else allowed).append(_Stale(check, kind, reason, *ids))
    return allowed, capped, text


def _restart(pull: PullChecks, stale: list[_Stale], apply: bool, run: Runner) -> Rerun:
    """Rerun (or plan to rerun) the stale jobs of one run of `pull`."""
    stale = sorted(stale, key=lambda item: item.check.name)
    number = stale[0].run
    names, reasons = tuple(item.check.name for item in stale), tuple(item.reason for item in stale)
    if not apply:
        return Rerun(pull.number, pull.branch, number, names, reasons, "would-rerun")
    jobs = sorted({item.job for item in stale})
    # `--failed` restarts every failed job of the run: only safe when all of them are stale.
    others = [c for c in pull.failed if (ids := _ids(c.url)) is not None and ids[0] == number and ids[1] not in jobs]
    commands = (
        [["run", "rerun", str(number), "--failed"]]
        if not others
        else [["run", "rerun", str(number), "--job", str(job)] for job in jobs]
    )
    try:
        for command in commands:
            run(command)
    except OSError as error:
        return Rerun(pull.number, pull.branch, number, names, reasons, "refused", f"{error}; {_advice(stale)}")
    return Rerun(pull.number, pull.branch, number, names, reasons, "reran")


def plan_reruns(pulls: Sequence[PullChecks], conventions: Conventions, run: Runner, apply: bool = False) -> list[Rerun]:
    """The reruns for `pulls`; with `apply` they are done (`gh run rerun`), otherwise only planned.

    One result per run, in the order of the pull requests then the run ids. A run whose failed jobs are all stale is
    rerun with `--failed`; when it also holds a failure that is not stale, only the stale jobs are rerun (`--job`), so a
    real failure is not restarted along with them.
    """
    limit = conventions.hygiene.pr_health.rerun.max_per_sha
    results: list[Rerun] = []
    for pull in sorted(pulls, key=lambda item: item.number):
        if not pull.failed:
            continue
        cache: list[_Commit] = []

        def commit(pull: PullChecks = pull, cache: list[_Commit] = cache) -> _Commit:
            """The runs of this pull request's head commit, read on first use."""
            if not cache:
                cache.append(_read_commit(pull.head, run))
            return cache[0]

        allowed, capped, text = _find_stale(pull, conventions, run, commit)
        runs = sorted({item.run for item in (*allowed, *capped, *text)})
        for number in runs:
            fresh = [item for item in text if item.run == number]
            held = [item for item in capped if item.run == number]
            go = [item for item in allowed if item.run == number]
            if fresh:
                results.append(_report(pull, fresh, "new-event", _advice(fresh) + NO_RERUN))
            if held:
                detail = f"{limit} reruns of {held[0].check.workflow} already on {pull.head[:7]}; {_advice(held)}"
                results.append(_report(pull, held, "capped", detail))
            if go:
                results.append(_restart(pull, go, apply, run))
    return results


def _report(pull: PullChecks, stale: list[_Stale], status: str, detail: str) -> Rerun:
    """A result for the stale checks of one run that are not restarted."""
    members = sorted(stale, key=lambda item: item.check.name)
    names, reasons = tuple(item.check.name for item in members), tuple(item.reason for item in members)
    return Rerun(pull.number, pull.branch, members[0].run, names, reasons, status, detail)
