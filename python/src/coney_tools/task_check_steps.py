# SPDX-License-Identifier: GPL-3.0-or-later
"""The checks `coney-tools task check` runs, each by the name `proof.profiles` in `.github/conventions.yml` gives it.

One function per check, taking a `StepContext` and returning a `local_gate.StepResult`; a check that cannot run on this
machine (a program is missing) raises `CheckUnavailable` instead of failing, because nothing is wrong with the change.
The two checks of the build, `msvc-ci-build` and `ctest-full-disc` (`proof.build.checks`), are one run of `coney-tools
build` and are answered by `task_check` from the build record, not here. The commands are the ones CI runs: the ruff
and mypy words are those of `check_cli` (which a test compares with CI's python job), so a pass here and in CI agree.
This module never runs clang-tidy: CI does, on every pull request.
"""

from __future__ import annotations

import os
import re
import shutil
import tempfile
from collections.abc import Callable, Sequence
from contextlib import AbstractContextManager, nullcontext
from dataclasses import dataclass
from pathlib import Path

from coney_tools import docs_check, docs_cli, proof_profiles
from coney_tools.check_cli import MYPY_COMMAND, RUFF_COMMANDS
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.local_gate import StepResult, run_command

#: Runs a command in a folder and returns its exit code and its output (stdout and stderr together).
Runner = Callable[[Sequence[str], Path], tuple[int, str]]
#: Holds a check slot while a full suite (`what`: `pytest`) runs; the real one waits in the queue of
#: build_claims.CHECK_LANE (check_slots.py), the default holds nothing.
Hold = Callable[[str], AbstractContextManager[None]]
#: The checks that run a whole suite and so hold a check slot for their run.
FULL_SUITE_CHECKS: tuple[str, ...] = ("pytest",)
#: How many lines of a failing command are kept for the summary.
KEEP_LINES = 15
#: The pytest run: the tests of the Python package and the hooks, as CI's python job runs them (no `-x`: the stamp
#: should say everything that is wrong). `pytest_step` adds the scope (`--reads <class>`, conftest.py) and the workers.
PYTEST_COMMAND: tuple[str, ...] = ("uv", "run", "--project", "python", "pytest", "python/tests", "-q", "--no-header")


#: The counts in pytest's last line ("4210 passed, 3 skipped in 3612.40s"), without the time.
PYTEST_COUNTS = re.compile(r"^(\d+ (?:passed|failed|skipped)(?:, \d+ \w+)*)(?= in )", re.MULTILINE)
#: One count in that line ("11 skipped").
COUNT = re.compile(r"(\d+) (\w+)")


class CheckUnavailable(ConfigError):
    """A check cannot run on this machine; the change is not at fault, so no stamp is written."""


@dataclass(frozen=True)
class StepContext:
    """What a check is given: the worktree, the base branch the change is measured from, the changed paths, the
    conventions, how to run a command and how to hold a check slot for a full suite."""

    root: Path
    base: str
    paths: tuple[str, ...]
    conventions: Conventions
    run: Runner = run_command
    hold: Hold = lambda what: nullcontext()
    #: The pytest workers (pytest-xdist) a run may use: a check slot's share of the cores (`pytest_workers`).
    workers: int = 1

    @property
    def pytest_scope(self) -> proof_profiles.PytestScope:
        """Which Python tests the change needs (`proof.pytest`)."""
        return proof_profiles.pytest_scope(self.conventions, self.paths)


Step = Callable[[StepContext], StepResult]


def _need(program: str, why: str) -> str:
    """The path of `program`, or CheckUnavailable saying what it is for."""
    found = shutil.which(program)
    if found is None:
        raise CheckUnavailable(f"{program} is not installed ({why})")
    return found


def _tail(output: str) -> tuple[str, ...]:
    """The last lines of a command's output worth showing for a failure."""
    lines = [line for line in output.splitlines() if line.strip()]
    kept = lines[-KEEP_LINES:]
    return tuple(
        (
            [f"... {len(lines) - len(kept)} earlier lines; run the check by hand for all of it"]
            if len(lines) > len(kept)
            else []
        )
        + kept
    )


def _commands(
    name: str,
    commands: Sequence[Sequence[str]],
    context: StepContext,
    ok: str,
    counts: re.Pattern[str] | None = None,
) -> StepResult:
    """Run each command in the worktree; the check fails if any does, and every failure shows its tail. With `counts`,
    a pass appends the last match in the output to `ok` (pytest's "412 passed, 3 skipped") and keeps its numbers by
    kind, for the stamp."""
    problems: list[str] = []
    found = ""
    counted: tuple[tuple[str, int], ...] = ()
    for command in commands:
        code, output = context.run(list(command), context.root)
        if code != 0:
            problems.append(f"{' '.join(command[:6])}: exit {code}")
            problems.extend(f"  {line}" for line in _tail(output))
        elif counts is not None and (matches := counts.findall(output)):
            found = matches[-1]
            counted = tuple((kind, int(number)) for number, kind in COUNT.findall(found))
    detail = f"{ok}: {found}" if found else ok
    if problems:
        return StepResult(name, False, "failed", output=tuple(problems))
    return StepResult(name, True, detail, counts=counted)


def docs_check_step(context: StepContext) -> StepResult:
    """`coney-tools docs check`: the pages the change touched meet the standards (writing-docs.md#touch-it)."""
    result = docs_check.check_changed(context.root, context.base, docs_check.context_for(context.root))
    failed = result.failed
    lines = [line for page in result.pages if page in failed for line in docs_cli.page_lines(result, page)]
    detail = f"{len(result.pages)} pages meet the standards" if not failed else f"{len(failed)} pages fail"
    return StepResult("docs-check", not failed, detail, output=tuple(lines))


def mkdocs_step(context: StepContext) -> StepResult:
    """`mkdocs build --strict` into a throwaway folder: the site builds with no warning."""
    root = context.root
    venv = root / ".venv" / ("Scripts" if os.name == "nt" else "bin") / ("python.exe" if os.name == "nt" else "python")
    with tempfile.TemporaryDirectory(prefix="coney-site-") as site:
        if venv.is_file():
            command = [str(venv), "-m", "mkdocs", "build", "--strict", "--site-dir", site]
        else:
            _need("uv", "docs/guides/building.md#python-tools")
            command = ["uv", "run", "--no-project", "--with-requirements", "requirements-docs.txt", "python", "-m"]
            command += ["mkdocs", "build", "--strict", "--site-dir", site]
        code, output = context.run(command, root)
    if code == 0:
        return StepResult("mkdocs", True, "mkdocs build --strict")
    lines = [
        line for line in output.splitlines() if line.startswith(("WARNING", "ERROR", "Aborted")) or "Error" in line
    ]
    return StepResult(
        "mkdocs", False, "mkdocs build --strict failed", output=tuple(lines[:KEEP_LINES] or _tail(output))
    )


def pre_commit_step(context: StepContext) -> StepResult:
    """`pre-commit run --all-files`: formatting, hygiene, the 512 KB limit, markdownlint and the repository hooks."""
    runner = ["uvx", "pre-commit"] if shutil.which("uvx") else [_need("pre-commit", "uvx or pre-commit on PATH")]
    return _commands("pre-commit", [[*runner, "run", "--all-files"]], context, "all hooks pass")


def ruff_step(context: StepContext) -> StepResult:
    """ruff check and ruff format --check over the package and the hook scripts, as CI's python job does."""
    _need("uv", "docs/guides/building.md#python-tools")
    return _commands("ruff", RUFF_COMMANDS, context, "ruff check, ruff format --check")


def mypy_step(context: StepContext) -> StepResult:
    """`mypy --strict` over the package and its tests, as CI's python job does."""
    _need("uv", "docs/guides/building.md#python-tools")
    return _commands("mypy", [MYPY_COMMAND], context, "mypy --strict")


def pytest_workers(cores: int | None, slots: int) -> int:
    """The pytest workers of one check slot: its share of the cores (16 cores and 4 slots give 4), at least one."""
    return max(1, (cores or 1) // max(1, slots))


def pytest_arguments(workers: int) -> list[str]:
    """pytest-xdist's arguments for `workers` (none for one: the tests then run in pytest's own process)."""
    return ["-n", str(workers)] if workers > 1 else []


def pytest_step(context: StepContext) -> StepResult:
    """The Python tests the change needs (`StepContext.pytest_scope`), on the workers of a check slot. The whole suite
    holds a check slot, which a machine can run only a few of at once (a build_claims.SlotTimeout from the wait is the
    caller's to turn into "queued"); a scoped run, only the tests marked as reading the docs or engine classes the
    change touches, is a few hundred tests and holds none."""
    _need("uv", "docs/guides/building.md#python-tools")
    scope = context.pytest_scope
    selection = [word for name in scope.classes for word in ("--reads", name)]
    command = [*PYTEST_COMMAND, *selection, *pytest_arguments(context.workers)]
    ok = "pytest python/tests, " + (scope.text if not scope.whole else "the whole suite")
    with context.hold("pytest") if scope.whole else nullcontext():
        return _commands("pytest", [command], context, ok, PYTEST_COUNTS)


def actionlint_step(context: StepContext) -> StepResult:
    """actionlint over the workflows, when a workflow changed."""
    _need("actionlint", "https://github.com/rhysd/actionlint; CI runs it too")
    return _commands("actionlint", [["actionlint"]], context, "workflows are clean")


#: Every check that is its own command, by the name `proof.profiles` gives it.
IMPLEMENTED: dict[str, Step] = {
    "docs-check": docs_check_step,
    "mkdocs": mkdocs_step,
    "pre-commit": pre_commit_step,
    "ruff": ruff_step,
    "mypy": mypy_step,
    "pytest": pytest_step,
    "actionlint": actionlint_step,
}
