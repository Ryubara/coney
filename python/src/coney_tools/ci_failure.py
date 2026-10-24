# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools ci report-failure`: a failed run on the default branch opens a P-critical issue.

The nightly build, a push to `main` and a manual run all happen on `main`, where nobody is waiting on them: a red one
could stay red for days. The `ci-failure` workflow calls this when a watched workflow (`ci.failure.workflows`) ends in
failure on the default branch. It reads the run and the log of its failed steps, names the files the log mentions with
the last commits that touched them (the likely culprits), and files an issue in the task form with the labels of
`ci.failure.labels` (`P-critical` among them: while one has no claim, only it can be claimed). A workflow that fails
again while its issue is open adds a comment to that issue instead of opening another; a run is never reported twice.

The issue is found again by a marker in its body: `<!-- coney-ci-failure:<workflow> -->`.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from coney_tools.audit_github import Runner
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions

#: Runs `git` in the checkout with these arguments and returns its output; "" when it fails.
GitRunner = Callable[[list[str]], str]

#: How an event is told in the issue.
EVENT_WORDS = {"schedule": "nightly", "push": "push to main", "workflow_dispatch": "manual"}
#: Conclusions of a job that count as a failure of it (a cancelled job is someone's decision, not a failure).
FAILED_CONCLUSIONS = ("failure", "timed_out", "startup_failure")

_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_PREFIX = re.compile(r"^[^\t]*\t[^\t]*\t﻿?\d{4}-\d\d-\d\dT[\d:.]+Z ?")
_FILE = re.compile(r"[\w./+-]+\.(?:cpp|hpp|h|py|yml|yaml|md|cmake|toml|json|sh|txt|ps1|ts)\b")
_LOOKS_FAILED = re.compile(r"\berror\b|\bFAILED\b|\(Failed\)|\*\*\*", re.IGNORECASE)


@dataclass(frozen=True)
class Outcome:
    """What `report_failure` did: `skipped` (not a failure to report), `known` (this run is already on the issue),
    `created`, `commented`, or `would-create` / `would-comment` without `apply`."""

    kind: str
    number: int | None = None
    text: str = ""
    detail: str = ""


def failed_jobs(info: dict[str, Any]) -> list[dict[str, Any]]:
    """The jobs of a run (`gh run view --json jobs`) that failed, in order."""
    return [job for job in info.get("jobs", []) if job.get("conclusion") in FAILED_CONCLUSIONS]


def strip_log_prefix(line: str) -> str:
    """A line of `gh run view --log-failed` (`job<TAB>step<TAB>timestamp text`) as the program printed it, no colour."""
    return _ANSI.sub("", _PREFIX.sub("", line, count=1)).rstrip()


def files_in(lines: Sequence[str], root: Path, limit: int) -> list[str]:
    """The files of the checkout that `lines` mention, in the order first met, at most `limit`.

    A runner prints absolute paths (`/home/runner/work/coney/coney/src/a.cpp`): the longest tail of a path that is a
    file below `root` is the file. A path that no longer exists (deleted since) is left out.
    """
    found: list[str] = []
    for line in lines:
        for token in _FILE.findall(line):
            parts = [part for part in token.replace("\\", "/").split("/") if part and part != "."]
            if ".." in parts:
                continue
            for start in range(len(parts)):
                candidate = "/".join(parts[start:])
                if (root / candidate).is_file():
                    if candidate not in found:
                        found.append(candidate)
                    break
            if len(found) >= limit:
                return found
    return found


def excerpt_of(lines: Sequence[str], limit: int) -> list[str]:
    """The lines worth quoting: the first `limit` that name an error or a failed test, else the last `limit`."""
    failures = [line for line in lines if _LOOKS_FAILED.search(line)]
    return failures[:limit] if failures else list(lines[-limit:])


def _marker(settings_marker: str, workflow: str) -> str:
    """The HTML comment that ties an issue to a workflow."""
    return f"<!-- {settings_marker}:{workflow} -->"


def _run_marker(settings_marker: str, run: int) -> str:
    """The HTML comment that ties an issue or comment to one run."""
    return f"<!-- {settings_marker}-run:{run} -->"


def _commits(files: Sequence[str], count: int, git: GitRunner) -> dict[str, list[str]]:
    """Each file -> the last `count` commits that touched it, as `hash title (author, date)` lines."""
    history: dict[str, list[str]] = {}
    for name in files:
        arguments = ["log", "-n", str(count), "--format=%h %s (%an, %ad)", "--date=short", "--", name]
        history[name] = [line for line in git(arguments).splitlines() if line.strip()]
    return history


def _when(event: str) -> str:
    """`nightly`, `push to main` or `manual`: how the run started."""
    return EVENT_WORDS.get(event, event)


def _body(
    conventions: Conventions,
    info: dict[str, Any],
    run: int,
    excerpt: Sequence[str],
    log_problem: str,
    files: Sequence[str],
    history: dict[str, list[str]],
) -> str:
    """The issue text in the task form's sections (`issues.cli_uses_form`)."""
    settings = conventions.ci.failure
    workflow, url, sha = (
        str(info.get("workflowName") or info.get("name")),
        str(info.get("url", "")),
        str(info["headSha"]),
    )
    jobs = failed_jobs(info)
    lines = [
        _marker(settings.marker, workflow),
        _run_marker(settings.marker, run),
        "",
        "### Summary",
        "",
        f"The {_when(str(info.get('event')))} run of the `{workflow}` workflow failed on `{info.get('headBranch')}` at "
        f"`{sha[:7]}` ([run {run}]({url})), so the default branch is broken until this is fixed. CI filed this issue; "
        "it is `P-critical`, so it comes before other work.",
        "",
        "Failed jobs:",
        "",
        *[f"- [{job.get('name')}]({job.get('url', url)}) ({job.get('conclusion')})" for job in jobs],
        "",
    ]
    if excerpt:
        lines += ["What the log of the failed steps shows:", "", "```text", *excerpt, "```", ""]
    elif log_problem:
        lines += [f"The log could not be read: {log_problem}", ""]
    if files:
        lines += ["Files the log names, each with the last commits that touched it (where to look first):", ""]
        for name in files:
            lines.append(f"- `{name}`")
            lines += [f"  - {commit}" for commit in history.get(name, [])]
        lines.append("")
    else:
        lines += ["The log names no file of the checkout, so there is no list of likely commits.", ""]
    lines += [
        "### Area",
        "",
        settings.area,
        "",
        '### What "done" means',
        "",
        f"The `{workflow}` workflow passes on `{info.get('headBranch')}` again: its next run is green. "
        "A fix is a pull request that makes the failing check pass, or a revert of the change that broke it.",
        "",
        "### Fix list or steps",
        "",
        f"- [ ] Read the log of the failed jobs: {url}",
        "- [ ] Find the change that broke it (the commits above, then `git log` for the failing step)",
        "- [ ] Fix it forward in a pull request, or revert the change",
        "- [ ] Check that the next run of the workflow is green",
        "",
        "### Screenshots",
        "",
        "Screenshots: none (CI failure)",
        "",
    ]
    return "\n".join(lines)


def _comment(conventions: Conventions, info: dict[str, Any], run: int, excerpt: Sequence[str]) -> str:
    """The comment for a workflow that failed again while its issue is open."""
    settings = conventions.ci.failure
    jobs = ", ".join(str(job.get("name")) for job in failed_jobs(info))
    lines = [
        _run_marker(settings.marker, run),
        f"The {_when(str(info.get('event')))} run failed again at `{str(info['headSha'])[:7]}` "
        f"([run {run}]({info.get('url', '')})). Failed jobs: {jobs}.",
    ]
    if excerpt:
        lines += ["", "```text", *excerpt, "```"]
    return "\n".join(lines)


def _open_issue(workflow: str, conventions: Conventions, run: Runner) -> dict[str, Any] | None:
    """The open issue that tracks `workflow`'s failures on the default branch, if there is one."""
    settings = conventions.ci.failure
    arguments = ["issue", "list", "--state", "open", "--label", "P-critical", "--limit", "100"]
    issues = json.loads(run([*arguments, "--json", "number,title,body"]))
    marker = _marker(settings.marker, workflow)
    return next((item for item in issues if marker in str(item.get("body", ""))), None)


def _already_reported(issue: dict[str, Any], run_id: int, conventions: Conventions, run: Runner) -> bool:
    """True when this run is on the issue already, in its text or in one of its comments."""
    marker = _run_marker(conventions.ci.failure.marker, run_id)
    if marker in str(issue.get("body", "")):
        return True
    comments = json.loads(run(["issue", "view", str(issue["number"]), "--json", "comments"])).get("comments", [])
    return any(marker in str(item.get("body", "")) for item in comments)


def report_failure(
    run_id: int, conventions: Conventions, run: Runner, git: GitRunner, root: Path, apply: bool = False
) -> Outcome:
    """Report the failed run `run_id`: open an issue (or comment on the open one), or say why not.

    Without `apply` nothing is written: the outcome is `would-create` or `would-comment` with the text that would be.
    """
    settings = conventions.ci.failure
    info = json.loads(
        run(
            [
                "run",
                "view",
                str(run_id),
                "--json",
                "databaseId,name,workflowName,event,headBranch,headSha,conclusion,url,jobs",
            ]
        )
    )
    workflow = str(info.get("workflowName") or info.get("name"))
    why = (
        f"{workflow} is not a watched workflow"
        if workflow not in settings.workflows
        else f"its event is {info.get('event')}"
        if info.get("event") not in settings.events
        else f"it ran on {info.get('headBranch')}, not {conventions.repo.default_branch}"
        if info.get("headBranch") != conventions.repo.default_branch
        else f"it ended in {info.get('conclusion')}"
        if info.get("conclusion") != "failure"
        else ""
    )
    if why:
        return Outcome("skipped", detail=f"run {run_id} is not a failure to report: {why}")
    log_problem = ""
    lines: list[str] = []
    try:
        lines = [strip_log_prefix(line) for line in run(["run", "view", str(run_id), "--log-failed"]).splitlines()]
    except OSError as error:
        log_problem = str(error)
    excerpt = excerpt_of([line for line in lines if line.strip()], settings.log_lines)
    existing = _open_issue(workflow, conventions, run)
    if existing is not None:
        if _already_reported(existing, run_id, conventions, run):
            return Outcome("known", int(existing["number"]), detail=f"run {run_id} is already on #{existing['number']}")
        text = _comment(conventions, info, run_id, excerpt)
        if not apply:
            return Outcome("would-comment", int(existing["number"]), text)
        run(["issue", "comment", str(existing["number"]), "--body", text])
        return Outcome("commented", int(existing["number"]), text)
    files = files_in(lines, root, settings.files_listed)
    history = _commits(files, settings.commits_per_file, git)
    title = f"ci: Fix the failed {workflow} run on {conventions.repo.default_branch}"
    text = _body(conventions, info, run_id, excerpt, log_problem, files, history)
    if not apply:
        return Outcome("would-create", None, f"{title}\n\n{text}")
    arguments = ["issue", "create", "--title", title, "--body", text]
    for label in settings.labels:
        arguments += ["--label", label]
    printed = run(arguments).strip().splitlines()
    match = re.search(r"/issues/(\d+)\s*$", printed[-1] if printed else "")
    if not match:
        raise ConfigError(f"gh issue create did not print an issue URL (got {printed[-1:]!r})")
    return Outcome("created", int(match.group(1)), text)


def run_git(root: Path) -> GitRunner:
    """A `git -C root` runner; a failing command gives no output (a file with no history lists no commits)."""

    def git(arguments: list[str]) -> str:
        try:
            done = subprocess.run(
                ["git", "-C", str(root), *arguments],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )
        except OSError:
            return ""
        return done.stdout if done.returncode == 0 else ""

    return git
