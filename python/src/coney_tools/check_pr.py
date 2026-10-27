# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools ci pr` command: everything CI checks on a pull request, run before the pull request is opened.

`ci run` (check_cli) is the gate for the files a change touched. A pull request is also its title, its
description and the engine's build and tests. The hooks (Claude Code's, and the same dispatcher in every other agent
tool, docs/guides/enforcement.md#harnesses) judge one command at a time, and nothing put the whole change together
before CI's `title`, `docs` and `body` jobs and the `build` workflow: this is that backstop, run before the pull request
is opened, whichever tool made the change. This command runs the
`title`, `body` and `docs-rule` checks on the text it is given, then the steps of `ci run`, then the engine's
build and tests when the change touches what the engine jobs read (docs/guides/testing.md#local-gate). The rules are the
ones in `.github/conventions.yml`, run by the same functions as `pr check-title`, `pr check-body` and `pr
check-docs`; nothing is copied.
"""

from __future__ import annotations

import os
import sys
from collections.abc import Callable
from pathlib import Path

from coney_tools import build_cli, check_cli, local_gate
from coney_tools.build_run import EXIT_QUEUED
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.local_gate import StepResult
from coney_tools.process_lifetime import bind_children
from coney_tools.repo_checks import branch_trailer_models, check_body, check_docs, check_title

#: The checks on the text, which CI runs as its `title`, `body` and `docs` jobs.
TEXT_STEPS: tuple[str, ...] = ("title", "body", "docs-rule")
#: The engine's build and tests, which CI runs as its `build` workflow (minus the other compilers and the sanitizers).
ENGINE_STEP = "engine"
#: Every step, in the order they run and print; `--only` and `--skip` take these names.
STEPS: tuple[str, ...] = (*TEXT_STEPS, *check_cli.STEPS, ENGINE_STEP)
#: The CI scope classes (`paths.docs_only`, `tools_only`, `engine` of the conventions) whose files the engine jobs read:
#: `engine`, and `unknown`, which CI runs everything for.
ENGINE_CLASSES = ("engine", "unknown")
#: The preset the engine step builds, as the first hour of CONTRIBUTING.md does.
DEFAULT_PRESET = "dev"


def _read(path: Path, what: str) -> str:
    """The text of `path`; a ConfigError naming `what` when it cannot be read."""
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{what} {path}: cannot be read ({error})") from error


def step_title(title: str, conventions: Conventions) -> StepResult:
    """CI's `title` job: the title follows `commits.title`."""
    problems = check_title(title, conventions.commits.title)
    if problems:
        return StepResult("title", False, f"refused: {title!r}", output=tuple(problems))
    return StepResult("title", True, "follows area: Verb the rest")


def step_body(root: Path, body: str, title: str, base: str, conventions: Conventions) -> StepResult:
    """CI's `body` job, which sees only the description and the title, and one more rule CI cannot see: the `Model:`
    line names every model that a trailer on the branch's commits names (`pr check-body --worktree`)."""
    models = branch_trailer_models(root, base, conventions.commits.trailers)
    problems = check_body(body, conventions.pull_requests, models, title)
    if problems:
        return StepResult("body", False, "refused (see .github/pull_request_template.md)", output=tuple(problems))
    return StepResult("body", True, "sections, summary, AI use and the model line")


def step_docs_rule(changed: list[str], body: str, conventions: Conventions) -> StepResult:
    """CI's `docs` job: a change to code changes the docs too, or the description says `Docs: none`."""
    problems = check_docs(changed, body, conventions.paths, conventions.pull_requests.docs_none)
    if problems:
        return StepResult("docs-rule", False, "refused", output=tuple(problems))
    return StepResult("docs-rule", True, "the docs change with the code, or the description says so")


def engine_needed(changed: list[str], conventions: Conventions) -> bool:
    """True when a changed file is one the engine jobs read or one CI sorts into no class (it runs everything)."""
    return any(conventions.paths.ci_class(name) in ENGINE_CLASSES for name in changed)


def build_command(preset: str, agent: str | None) -> list[str]:
    """The `coney-tools build` command that builds the engine and runs its tests, the way every guide says to run it
    (never `cmake --build` or `ctest`: docs/guides/coney-tools-build.md#build)."""
    command = [sys.executable, "-m", "coney_tools.cli", "build", "--preset", preset, "--test"]
    return [*command, "--agent", agent] if agent else command


def step_engine(root: Path, changed: list[str], conventions: Conventions, preset: str, agent: str | None) -> StepResult:
    """The engine's build and tests (`build --preset <preset> --test`), when the change touches what they read."""
    if not engine_needed(changed, conventions):
        return StepResult(ENGINE_STEP, True, "no engine file changed", skipped=True)
    print(f"     building and testing the engine ({preset}); this takes a while", flush=True)
    code, output = local_gate.run_command(build_command(preset, agent), root)
    if code == 0:
        return StepResult(ENGINE_STEP, True, f"{preset} build and tests")
    # A build's failure is at the end of its output, so the last lines are kept, not the first.
    lines = [line for line in output.splitlines() if line.strip()]
    return StepResult(
        ENGINE_STEP,
        False,
        f"build or tests failed (exit {code})",
        output=tuple(lines[-local_gate.MAX_OUTPUT_LINES :]),
    )


def pr_steps(
    root: Path, base: str, title: str, body: str, preset: str, agent: str | None
) -> dict[str, Callable[[], StepResult]]:
    """Every step of `STEPS` by name, for the change since `base` and the text `title` and `body`."""
    conventions = load_conventions(root)
    changed = local_gate.changed_files(root, base)
    # The two checks of CI that read the pull request's file list read the committed one, deletions included.
    listed = local_gate.pull_request_files(root, base)
    steps: dict[str, Callable[[], StepResult]] = {
        "title": lambda: step_title(title, conventions),
        "body": lambda: step_body(root, body, title, base, conventions),
        "docs-rule": lambda: step_docs_rule(listed, body, conventions),
        ENGINE_STEP: lambda: step_engine(root, listed, conventions, preset, agent),
    }
    steps.update(check_cli.changed_steps(root, base, changed))
    return steps


def engine_agent(root: Path, agent: str | None) -> str:
    """The id the engine step's `build` runs under. `build` takes its id from the user name only when it is typed in a
    terminal, and the step runs it with its output captured, so it would refuse a run without one: the id is resolved
    here, in the process that may be one: the given id, else a person's (`build`'s own default), else the worktree's
    (`CONEY_AGENT`, else its folder name), the one a detached `ci run` names itself by."""
    return agent or build_cli.default_agent_id() or check_cli.default_agent(root, os.environ)


def run_pr(
    base: str,
    title: str,
    body_file: Path,
    only: list[str] | None,
    skip: list[str] | None,
    preset: str = DEFAULT_PRESET,
    agent: str | None = None,
) -> int:
    """Check the pull request that the change since `base` and the text (`title`, the description in `body_file`) make;
    print a line per step; 0 when all pass, 1 when any fails (CI would refuse it too), 2 for a setup problem."""
    root = find_repo_root(Path.cwd())
    body = _read(body_file, "the description")
    bind_children()  # an interrupted run leaves none of its programs behind
    wanted = [step for step in STEPS if (not only or step in only) and step not in (skip or [])]
    steps = pr_steps(root, base, title.strip(), body, preset, engine_agent(root, agent))
    print(f"pull request {title.strip()!r}: changes since {base}")
    code = check_cli.run_steps(wanted, steps, "ci pr")
    if code == EXIT_QUEUED:
        return code
    if code:
        print("coney-tools ci pr: FAILED (CI would refuse this pull request too)")
        return 1
    print("coney-tools ci pr: ok. CI only: GCC-only warnings, macOS, sanitizers, clang-tidy on changed lines.")
    return 0
