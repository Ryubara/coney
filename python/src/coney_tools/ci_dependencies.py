# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools ci dependencies` and `ci rerun-dependencies`: a pull request waits for the pull requests it names.

A description may say `Depends on #N` (`pull_requests.stacked.depends_line`): the work builds on a pull request that is
not on the default branch yet. The `dependencies` job of the `dependencies` workflow is a required check, so the pull
request cannot merge while any such pull request is open. Stacked pull requests are not how the project works any more
(`docs/guides/how-we-work.md`), so the line is a backstop and most pull requests have none and pass at once.

The check reads the description from GitHub when it runs, not from the event that started the run: a rerun reuses the
event, and the point of a rerun is to see that a dependency has merged since. `rerun_waiting` finds the open pull
requests whose `dependencies` run failed and whose dependencies have all merged now, and restarts those runs; the
workflow calls it on every push to the default branch, which is when a dependency merges.
"""

from __future__ import annotations

import json
from collections.abc import Sequence
from dataclasses import dataclass

from coney_tools.audit_github import Runner
from coney_tools.conventions import Conventions
from coney_tools.repo_checks import depends_numbers

#: How many open pull requests the rerun pass reads; the repository has a few dozen open at most.
PULL_LIMIT = 300
#: How many runs of one commit are read to find its newest `dependencies` run.
RUN_LIMIT = 10


@dataclass(frozen=True)
class Waiting:
    """One pull request a description depends on that is not merged (or cannot be waited for)."""

    number: int
    #: `open`, `closed`, `missing` (not a pull request) or `self`.
    kind: str

    def line(self) -> str:
        """The sentence that says why, and what to do."""
        match self.kind:
            case "open":
                return f"#{self.number} is still open: it must be merged first"
            case "closed":
                return f"#{self.number} was closed without merging: change or remove the Depends on line"
            case "self":
                return f"#{self.number} is this pull request itself"
            case _:
                return f"#{self.number} is not a pull request: Depends on names pull requests, not issues"


def dependency_numbers(body: str, line: str) -> tuple[int, ...]:
    """The pull requests `body` says it depends on (`line` is `pull_requests.stacked.depends_line`)."""
    return depends_numbers(body, line)


def _state_of(number: int, run: Runner) -> str | None:
    """The state GitHub gives pull request `number` (`OPEN`, `CLOSED`, `MERGED`); None when there is no such one.

    Any other failure of `gh` raises OSError: a check that cannot ask must not call a dependency merged or missing.
    """
    try:
        found = json.loads(run(["pr", "view", str(number), "--json", "number,state"]))
    except OSError as error:
        if "Could not resolve" in str(error):
            return None
        raise
    return str(found.get("state", "")) if isinstance(found, dict) else None


def waiting_on(number: int, body: str, line: str, run: Runner) -> list[Waiting]:
    """The dependencies of pull request `number` (description `body`) that are not merged, in the order written."""
    waiting: list[Waiting] = []
    for dependency in dependency_numbers(body, line):
        if dependency == number:
            waiting.append(Waiting(dependency, "self"))
            continue
        state = _state_of(dependency, run)
        if state is None:
            waiting.append(Waiting(dependency, "missing"))
        elif state == "OPEN":
            waiting.append(Waiting(dependency, "open"))
        elif state != "MERGED":
            waiting.append(Waiting(dependency, "closed"))
    return waiting


@dataclass(frozen=True)
class Rerun:
    """One waiting pull request whose dependencies have all merged, and what became of its failed run."""

    number: int
    run: int
    #: `would-rerun` (a dry run), `reran`, `refused` (GitHub would not) or `capped` (over the `rerun_limit`).
    status: str
    detail: str = ""

    def line(self) -> str:
        """The text line: `#<n> <outcome> run <id> (dependencies are merged)`."""
        outcome = {"would-rerun": "would rerun", "reran": "reran"}.get(self.status, self.status)
        text = f"#{self.number} {outcome} run {self.run} (dependencies are merged)"
        return f"{text}: {self.detail}" if self.detail else text


def _failed_run(head: str, workflow: str, run: Runner) -> int | None:
    """The id of the newest pull request run of `workflow` on `head` if it failed; None if it passed or is going."""
    arguments = ["run", "list", "--workflow", workflow, "--commit", head, "--limit", str(RUN_LIMIT)]
    arguments += ["--json", "databaseId,conclusion,status,event"]
    runs = json.loads(run(arguments))
    pull_runs = [item for item in runs if isinstance(item, dict) and item.get("event") == "pull_request"]
    if not pull_runs:
        return None
    newest = max(pull_runs, key=lambda item: int(item.get("databaseId", 0)))
    if newest.get("status") != "completed" or newest.get("conclusion") not in ("failure", "cancelled", "timed_out"):
        return None
    return int(newest["databaseId"])


def rerun_waiting(conventions: Conventions, run: Runner, apply: bool = False) -> list[Rerun]:
    """Restart the failed `dependencies` run of each open pull request whose dependencies have all merged.

    Without `apply` the restarts are only planned. Pull requests are taken in number order and at most
    `ci.dependencies.rerun_limit` runs are restarted (the rest are `capped`: the next pass restarts them). A pull
    request with no `Depends on` line, one still waiting on an unmerged pull request, and one whose newest run passed,
    is going or is not there is left alone.
    """
    line = conventions.pull_requests.stacked.depends_line
    settings = conventions.ci.dependencies
    pulls = json.loads(
        run(["pr", "list", "--state", "open", "--limit", str(PULL_LIMIT), "--json", "number,body,headRefOid"])
    )
    results: list[Rerun] = []
    restarted = 0
    for pull in sorted((item for item in pulls if isinstance(item, dict)), key=lambda item: int(item["number"])):
        number = int(pull["number"])
        body = str(pull.get("body") or "")
        if not dependency_numbers(body, line) or waiting_on(number, body, line, run):
            continue
        failed = _failed_run(str(pull.get("headRefOid", "")), settings.workflow, run)
        if failed is None:
            continue
        if restarted >= settings.rerun_limit:
            results.append(Rerun(number, failed, "capped", f"{settings.rerun_limit} runs restarted in this pass"))
            continue
        restarted += 1
        if not apply:
            results.append(Rerun(number, failed, "would-rerun"))
            continue
        try:
            run(["run", "rerun", str(failed), "--failed"])
        except OSError as error:
            results.append(Rerun(number, failed, "refused", str(error)))
            continue
        results.append(Rerun(number, failed, "reran"))
    return results


def format_waiting(number: int, waiting: Sequence[Waiting]) -> list[str]:
    """The lines the check prints when pull request `number` waits: a header, then one line per dependency."""
    count = len(waiting)
    header = f"dependencies: waiting on {count} pull request{'s' if count != 1 else ''} (#{number} cannot merge yet)"
    return [header, *(f"  {item.line()}" for item in waiting)]
