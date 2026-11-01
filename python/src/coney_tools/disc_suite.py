# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools ci disc-suite`: the whole C++ suite with the disc on the default branch, after merges.

CI has no game data, so its runs skip every disc test, and `task check` runs only the C++ tests a change reaches
(`proof.ctest`, ctest_scope.py). A disc test that no change reached would then never run: this command runs them all.
The coordinator runs it in the after-merge tidy (docs/guides/coney-tools-ci.md#disc-suite). It fetches the default
branch, moves a worktree of its own (`ci.disc_suite.worktree` as the `<branch>` of `workspace.worktrees`) to it,
detached, and runs `coney-tools build --preset <proof.build.preset> --test --full` there with the disc, holding a
check slot like any whole suite. The run leaves its build record in the scratch folder `disc-suite`; a run counts as
passed only when `build_record.problems` finds nothing wrong with it (`task check`'s own rule for a whole suite).

A failure opens an issue in the task form with `ci.disc_suite.labels` (`P-high` among them), as a failed CI run on the
default branch does (ci_failure.py), or comments on the issue that is still open for it, found again by the marker
`<!-- coney-disc-suite -->` in its body. Without `apply` it prints what it would write.

Exit codes: 0 the suite passed; 1 it failed (the issue is filed, commented on, or printed); 2 it cannot run (no disc, a
git or gh failure, a dirty worktree); 75 the build gave up waiting for a slot (run it again). Everything that touches
the machine is in `SuiteEnv`, so the steps and the issue are tested with fakes.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from coney_tools import build_record, build_run
from coney_tools.audit_github import Runner
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions

#: The build gave up waiting for a slot: the code `coney-tools build` uses.
EXIT_QUEUED = build_run.EXIT_QUEUED


@dataclass
class SuiteEnv:
    """The machine the suite runs on: git (in a folder, with arguments; returns what it printed, raises ConfigError),
    the build (checkout, agent id, preset, task), whether there is a disc (None: there is, else why not), gh and where
    lines go."""

    git: Callable[[Path, list[str]], str]
    build: Callable[[Path, str, str, str], build_run.Result]
    disc: Callable[[Path], str | None]
    gh: Runner
    say: Callable[[str], None]


def worktree_of(main: Path, conventions: Conventions) -> Path:
    """The suite's own worktree: `ci.disc_suite.worktree` as the `<branch>` of `workspace.worktrees`, from the main
    checkout `main`."""
    template = conventions.workspace.worktrees
    if "<branch>" not in template:
        raise ConfigError(f"workspace.worktrees ({template}) must contain <branch>")
    return (main / template.replace("<branch>", conventions.ci.disc_suite.worktree)).resolve()


def run_suite(main: Path, conventions: Conventions, agent: str, env: SuiteEnv, apply: bool = False) -> int:
    """Move the suite's worktree to the default branch, run the whole suite there with the disc and report a failure;
    see the module docstring for the exit codes. ConfigError for what stops it before the build."""
    settings = conventions.ci.disc_suite
    branch = conventions.repo.default_branch
    ref = f"origin/{branch}"
    tree = worktree_of(main, conventions)
    env.git(main, ["fetch", "-q", "origin", branch])
    if tree.exists():
        if env.git(tree, ["status", "--porcelain"]).strip():
            raise ConfigError(f"{tree} has uncommitted changes; the suite runs only on a clean {ref}")
        env.git(tree, ["checkout", "-q", "--detach", ref])
    else:
        env.git(main, ["worktree", "add", "-q", "--detach", str(tree), ref])
    tip = env.git(tree, ["rev-parse", "HEAD"]).strip()
    why_not = env.disc(tree)
    if why_not:
        raise ConfigError(f"the disc suite needs the disc: {why_not}")
    preset = conventions.proof.build.preset
    env.say(f"ci disc-suite: {ref} at {tip[:10]} in {tree}: the whole suite with the disc, preset {preset}")
    result = env.build(tree, agent, preset, settings.worktree)
    if result.exit_code == EXIT_QUEUED:
        env.say("ci disc-suite: the build gave up waiting for a slot; run it again")
        return EXIT_QUEUED
    problems = _problems(result, tip, preset)
    if not problems:
        counts = result.tests
        env.say(
            f"ci disc-suite: passed: {counts.passed} of {counts.total} tests" if counts else "ci disc-suite: passed"
        )
        return 0
    for problem in problems:
        env.say(f"ci disc-suite: FAILED: {problem}")
    summary = result.summary[-settings.summary_lines :]
    _report(conventions, tip, problems, summary, env, apply)
    return 1


def _problems(result: build_run.Result, tip: str, preset: str) -> list[str]:
    """Why the run is not one whole, clean, passing run of `preset` at `tip` with the disc; empty when it is."""
    if result.record is None:
        return [f"the build left no build record (exit {result.exit_code})"]
    record = build_record.read(result.record.parent, result.record.stem)
    found = build_record.problems(record, tip=tip, preset=preset)
    if result.exit_code != 0 and not found:
        found.append(f"the build failed (exit {result.exit_code})")
    return found


def _marker(conventions: Conventions) -> str:
    """The marker that finds the open issue again."""
    return f"<!-- {conventions.ci.disc_suite.marker} -->"


def _report(
    conventions: Conventions, tip: str, problems: list[str], summary: list[str], env: SuiteEnv, apply: bool
) -> None:
    """Comment on the open issue of the suite, or open one; without `apply`, say what would be written."""
    settings = conventions.ci.disc_suite
    branch = conventions.repo.default_branch
    issues = json.loads(
        env.gh(["issue", "list", "--state", "open", "--label", "P-high", "--limit", "100", "--json", "number,body"])
    )
    open_issue = next((item for item in issues if _marker(conventions) in str(item.get("body", ""))), None)
    quoted = ["```text", *summary, "```"] if summary else []
    if open_issue is not None:
        number = int(open_issue["number"])
        text = "\n".join(
            [f"The disc suite failed again on `{branch}` at `{tip[:7]}`:", "", *_bullets(problems), *quoted]
        )
        if not apply:
            env.say(f"ci disc-suite: would comment on #{number} (no --apply):\n{text}")
            return
        env.gh(["issue", "comment", str(number), "--body", text])
        env.say(f"ci disc-suite: commented on #{number}")
        return
    title = f"tests: Fix the disc suite failing on {branch}"
    body = _body(conventions, tip, problems, quoted)
    if not apply:
        env.say(f"ci disc-suite: would create (no --apply):\n{title}\n\n{body}")
        return
    arguments = ["issue", "create", "--title", title, "--body", body]
    for label in settings.labels:
        arguments += ["--label", label]
    printed = env.gh(arguments).strip().splitlines()
    match = re.search(r"/issues/(\d+)\s*$", printed[-1] if printed else "")
    if not match:
        raise ConfigError(f"gh issue create did not print an issue URL (got {printed[-1:]!r})")
    env.say(f"ci disc-suite: created issue #{match.group(1)}")


def _bullets(lines: list[str]) -> list[str]:
    """`lines` as a Markdown list, then a blank line."""
    return [*(f"- {line}" for line in lines), ""]


def _body(conventions: Conventions, tip: str, problems: list[str], quoted: list[str]) -> str:
    """The issue text in the task form's sections (`issues.cli_uses_form`)."""
    settings = conventions.ci.disc_suite
    branch = conventions.repo.default_branch
    lines = [
        _marker(conventions),
        "",
        "### Summary",
        "",
        f"The whole C++ suite with the disc failed on `{branch}` at `{tip[:7]}` (`coney-tools ci disc-suite`, run in "
        "the after-merge tidy). CI cannot run the disc tests and `task check` runs only the tests a change reaches, "
        "so a merged change broke a disc test no check ran. The command filed this issue; it is `P-high`.",
        "",
        *_bullets(problems),
        *(
            ["What the run's summary shows (the log is in the `disc-suite` scratch folder):", "", *quoted, ""]
            if quoted
            else []
        ),
        "### Area",
        "",
        settings.area,
        "",
        '### What "done" means',
        "",
        f"`coney-tools ci disc-suite` passes on `{branch}` again. A fix is a pull request that makes the failing tests "
        "pass, or a revert of the change that broke them.",
        "",
        "### Fix list or steps",
        "",
        "- [ ] Run the failing tests on their own (`coney-tools build --test -R <name>`) and read the log",
        "- [ ] Find the merge that broke them (`git log` since the last passing run)",
        "- [ ] Fix it forward in a pull request, or revert the change",
        "- [ ] Run `coney-tools ci disc-suite` again and check that it passes",
        "",
        "### Screenshots",
        "",
        "Screenshots: none (disc suite failure)",
        "",
    ]
    return "\n".join(lines)


def run_git(folder: Path, arguments: list[str]) -> str:
    """Run git in `folder` and return what it printed; ConfigError when it fails."""
    try:
        done = subprocess.run(
            ["git", "-C", str(folder), *arguments], capture_output=True, text=True, encoding="utf-8", check=False
        )
    except OSError as error:
        raise ConfigError(f"git could not run: {error}") from error
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(arguments[:2])} failed in {folder}: {done.stderr.strip()}")
    return done.stdout
