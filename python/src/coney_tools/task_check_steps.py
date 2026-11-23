# SPDX-License-Identifier: GPL-3.0-or-later
"""The checks `coney-tools task check` runs, each by the name `proof.profiles` in `.github/conventions.yml` gives it.

One function per check, taking a `StepContext` and returning a `local_gate.StepResult`; a check that cannot run on this
machine (a program is missing) raises `CheckUnavailable` instead of failing, because nothing is wrong with the change.
The two checks of the build, `msvc-ci-build` and `ctest-full-disc` (`proof.build.checks`), are one run of `coney-tools
build` and are answered by `task_check` from the build record, not here. The commands are the ones CI runs: the ruff
and mypy words are those of `check_cli` and the progress check's are `progress_cli`'s (a test compares each with CI's
python job), so a pass here and in CI agree.
The `clang-tidy` check is CI's pull-request pass run here (local_tidy.py), after the build has written the compile
database it reads.
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

from coney_tools import (
    ci_python_checks,
    docs_check,
    docs_cli,
    local_tidy,
    progress_cli,
    proof_profiles,
    uv_environment,
)
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
    #: The paths the test scopes are read from: None is `paths`; a fix's are the files changed since the Check it
    #: stands on (check_fix.py).
    tested: tuple[str, ...] | None = None

    @property
    def pytest_scope(self) -> proof_profiles.PytestScope:
        """Which Python tests the change needs (`proof.pytest`): from the paths, or for a fix (`tested`) from the files
        changed since its Check and the classes the whole change touches (`proof_profiles.fix_pytest_scope`)."""
        if self.tested is None:
            return proof_profiles.pytest_scope(self.conventions, self.paths)
        return proof_profiles.fix_pytest_scope(self.conventions, self.tested, self.paths)


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
        # Never sync the shared environment: another session may hold its executables open (uv_environment.py).
        code, output = context.run(uv_environment.no_sync(command, context.root), context.root)
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
    """`coney-tools docs check` (the pages the change touched meet the standards: writing-docs.md#touch-it) and the
    checks CI's python job runs over the checkout, with CI's words and this check's base for its `HEAD^1`: `progress
    check` (`progress_cli.check_as_ci`: the progress inputs agree, and an `@orig` tag the branch adds spells the
    database's name; CI insists on a readable base) and each of `ci_python_checks.CHECKS` (the conventions and their
    generated blocks, the `@orig` tag and copy rules, every generator's `--check`). All of them run, so one run shows
    every problem; a pass of this check says all held. They are part of this check, not checks of their own, so a stamp
    written before they were added still names every check its profile lists."""
    result = docs_check.check_changed(context.root, context.base, docs_check.context_for(context.root))
    failed = result.failed
    lines = [line for page in result.pages if page in failed for line in docs_cli.page_lines(result, page)]
    code, printed = progress_cli.check_as_ci(context.root, context.base)
    if code != 0:
        lines += [f"progress check: {line}" for line in printed if line.strip()]
    broken = 0
    for words in ci_python_checks.CHECKS:
        command = ci_python_checks.command(words, context.base)
        # Never sync the shared environment: another session may hold its executables open (uv_environment.py).
        status, output = context.run(uv_environment.no_sync(command, context.root), context.root)
        if status != 0:
            broken += 1
            lines.append(f"{' '.join(command[5:])}: exit {status}")
            lines.extend(f"  {line}" for line in _tail(output))
    pages = f"{len(result.pages)} pages meet the standards" if not failed else f"{len(failed)} pages fail"
    repository = (
        f"{len(ci_python_checks.CHECKS)} repository checks pass"
        if not broken
        else f"{broken} repository check{'' if broken == 1 else 's'} fail{'s' if broken == 1 else ''}"
    )
    detail = f"{pages}; progress inputs {'agree' if code == 0 else 'fail'}; {repository}"
    return StepResult("docs-check", not failed and code == 0 and not broken, detail, output=tuple(lines))


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


def clang_tidy_preflight(root: Path, conventions: Conventions) -> None:
    """Find the clang-tidy the `clang-tidy` check will run before the run starts: the check comes after the build and
    the whole test run, so a machine without the program would learn it hours in. CheckUnavailable when there is none
    of CI's major version. The compile database is not looked for here: the build writes it."""
    try:
        local_tidy.find_program(root, conventions.ci.tidy.llvm_major)
    except local_tidy.TidyUnavailable as error:
        raise CheckUnavailable(str(error)) from error


def clang_tidy_step(context: StepContext) -> StepResult:
    """clang-tidy with the repository's `.clang-tidy` on the lines the change touches in its C++ files, against the
    compile database of the build preset (`proof.build.preset`), as CI's `clang-tidy` job checks the pull request
    (local_tidy.py). A finding names its file, line and check; no program of CI's major version, or no compile
    database, is CheckUnavailable."""
    root = context.root
    try:
        program = local_tidy.find_program(root, context.conventions.ci.tidy.llvm_major)
        build_dir = local_tidy.compile_database(root, context.conventions.proof.build.preset)
    except local_tidy.TidyUnavailable as error:
        raise CheckUnavailable(str(error)) from error
    local = local_tidy.run_local(
        root,
        context.base,
        context.conventions,
        run_command,
        program=program,
        build_dir=build_dir,
        jobs=local_tidy.default_jobs(root),
        probe=run_command,
    )
    count = len(local.plan.units)
    checked = f"{count} source{'' if count == 1 else 's'} checked"
    if not count:
        return StepResult("clang-tidy", True, "no C++ source to check on the changed lines")
    if local.ok:
        advice = f"; {len(local.advisory)} advice that may differ from CI" if local.advisory else ""
        return StepResult(
            "clang-tidy", True, f"clean on the changed lines ({checked}{advice})", output=tuple(local.lines())
        )
    found = len(local.result.findings)
    detail = f"{found} finding{'' if found == 1 else 's'} on the changed lines ({checked})"
    if local.result.failures and not found:
        detail = f"clang-tidy could not check every source ({checked})"
    return StepResult("clang-tidy", False, detail, output=tuple(local.lines() + local.notes))


#: Every check that is its own command, by the name `proof.profiles` gives it.
IMPLEMENTED: dict[str, Step] = {
    "docs-check": docs_check_step,
    "mkdocs": mkdocs_step,
    "pre-commit": pre_commit_step,
    "ruff": ruff_step,
    "mypy": mypy_step,
    "pytest": pytest_step,
    "actionlint": actionlint_step,
    "clang-tidy": clang_tidy_step,
}


#: What a check needs that can be looked for before the run starts (checkout, conventions; CheckUnavailable when it is
#: missing), by check name: only for a check that comes late, so its absence does not cost the checks before it.
Preflight = Callable[[Path, Conventions], None]
PREFLIGHT: dict[str, Preflight] = {"clang-tidy": clang_tidy_preflight}
