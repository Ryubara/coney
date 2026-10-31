# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the individual checks of `task check`: the commands they run, how a failure reads and what is missing."""

import shutil
from collections.abc import Iterator, Sequence
from contextlib import contextmanager
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import check_cli, docs_check, docs_cli, task_check_steps
from coney_tools.conventions import load_conventions
from coney_tools.task_check_steps import CheckUnavailable, StepContext


class Commands:
    """A runner that records the commands and answers with canned exit codes by a word in the command."""

    def __init__(self) -> None:
        self.commands: list[list[str]] = []
        self.codes: dict[str, tuple[int, str]] = {}

    def __call__(self, command: Sequence[str], cwd: Path) -> tuple[int, str]:
        words = list(command)
        self.commands.append(words)
        for word, answer in self.codes.items():
            if word in words:
                return answer
        return 0, ""


def context(
    runner: Commands,
    root: Path = REPO,
    paths: tuple[str, ...] = ("python/src/coney_tools/cli.py",),
    workers: int = 1,
    held: list[str] | None = None,
) -> StepContext:
    """A context for the checks of a change to `paths`, recording each check slot it holds in `held`."""

    @contextmanager
    def hold(what: str) -> Iterator[None]:
        if held is not None:
            held.append(what)
        yield

    return StepContext(root, "origin/main", paths, load_conventions(REPO), runner, hold, workers)


def test_ruff_runs_both_of_ci_commands_and_mypy_the_strict_one() -> None:
    runner = Commands()
    assert task_check_steps.ruff_step(context(runner)).ok
    assert runner.commands == [list(c) for c in check_cli.RUFF_COMMANDS]
    runner = Commands()
    assert task_check_steps.mypy_step(context(runner)).ok
    assert runner.commands == [list(check_cli.MYPY_COMMAND)]
    assert "--config-file" in runner.commands[0] and "python/pyproject.toml" in runner.commands[0]


def test_pytest_runs_the_whole_suite_without_stopping_at_the_first_failure() -> None:
    runner = Commands()
    assert task_check_steps.pytest_step(context(runner)).ok
    assert runner.commands == [list(task_check_steps.PYTEST_COMMAND)]
    assert "-x" not in runner.commands[0] and "python/tests" in runner.commands[0]


def test_a_passing_pytest_run_says_how_many_tests_passed() -> None:
    runner = Commands()
    runner.codes["pytest"] = (0, "....\n4210 passed, 3 skipped in 3612.40s (1:00:12)\n")
    result = task_check_steps.pytest_step(context(runner))
    assert result.ok and result.detail == "pytest python/tests, the whole suite: 4210 passed, 3 skipped"
    assert dict(result.counts) == {"passed": 4210, "skipped": 3}
    runner.codes["pytest"] = (0, "")
    empty = task_check_steps.pytest_step(context(runner))
    assert empty.detail == "pytest python/tests, the whole suite" and empty.counts == ()


def test_the_whole_suite_runs_in_parallel_on_the_workers_of_a_check_slot_and_holds_one() -> None:
    runner, held = Commands(), list[str]()
    assert task_check_steps.pytest_step(context(runner, workers=4, held=held)).ok
    assert runner.commands == [[*task_check_steps.PYTEST_COMMAND, "-n", "4"]]
    assert held == ["pytest"]


def test_a_docs_or_engine_change_runs_only_the_tests_marked_as_reading_them_without_a_slot() -> None:
    runner, held = Commands(), list[str]()
    runner.codes["pytest"] = (0, "212 passed, 9400 deselected in 40.00s\n")
    paths = ("docs/guides/testing.md", "src/ai/brain.cpp")
    result = task_check_steps.pytest_step(context(runner, paths=paths, workers=4, held=held))
    selection = ["--reads", "docs_only", "--reads", "engine"]
    assert runner.commands == [[*task_check_steps.PYTEST_COMMAND, *selection, "-n", "4"]]
    assert held == []  # a scoped run is not a full suite
    assert result.ok and result.detail == (
        "pytest python/tests, only the tests marked as reading docs_only, engine: 212 passed, 9400 deselected"
    )
    assert dict(result.counts) == {"passed": 212, "deselected": 9400}


def test_the_workers_of_a_check_slot_are_its_share_of_the_cores() -> None:
    assert task_check_steps.pytest_workers(cores=16, slots=4) == 4
    assert task_check_steps.pytest_workers(cores=16, slots=3) == 5
    assert task_check_steps.pytest_workers(cores=2, slots=4) == 1
    assert task_check_steps.pytest_workers(cores=None, slots=4) == 1
    assert task_check_steps.pytest_arguments(1) == []  # one worker runs in the process, without xdist


def test_the_pytest_counts_keep_every_kind_pytest_names() -> None:
    runner = Commands()
    runner.codes["pytest"] = (0, "7276 passed, 11 skipped, 1 deselected in 900.00s\n")
    result = task_check_steps.pytest_step(context(runner))
    assert dict(result.counts) == {"passed": 7276, "skipped": 11, "deselected": 1}


def test_a_failing_command_fails_the_check_and_shows_the_tail_of_its_output() -> None:
    runner = Commands()
    runner.codes["mypy"] = (1, "".join(f"line {n}\n" for n in range(40)) + "Found 3 errors\n")
    result = task_check_steps.mypy_step(context(runner))
    assert not result.ok and result.name == "mypy"
    assert result.output[-1].strip() == "Found 3 errors" and any("exit 1" in line for line in result.output)
    assert len(result.output) <= task_check_steps.KEEP_LINES + 3


def test_ruff_fails_if_either_command_fails_and_still_runs_the_other() -> None:
    runner = Commands()
    runner.codes["check"] = (1, "E501")
    result = task_check_steps.ruff_step(context(runner))
    assert not result.ok and len(runner.commands) == 2


def test_pre_commit_runs_on_every_file(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", lambda name: "/bin/" + name if name == "uvx" else None)
    runner = Commands()
    assert task_check_steps.pre_commit_step(context(runner)).ok
    assert runner.commands == [["uvx", "pre-commit", "run", "--all-files"]]
    monkeypatch.setattr(shutil, "which", lambda name: None)
    with pytest.raises(CheckUnavailable, match="pre-commit"):
        task_check_steps.pre_commit_step(context(Commands()))


def test_actionlint_is_unavailable_without_the_program(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", lambda name: None)
    with pytest.raises(CheckUnavailable, match="actionlint is not installed"):
        task_check_steps.actionlint_step(context(Commands()))
    monkeypatch.setattr(shutil, "which", lambda name: "/bin/actionlint")
    runner = Commands()
    assert task_check_steps.actionlint_step(context(runner)).ok and runner.commands == [["actionlint"]]


def test_uv_is_needed_for_the_python_checks(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", lambda name: None)
    for step in (task_check_steps.ruff_step, task_check_steps.mypy_step, task_check_steps.pytest_step):
        with pytest.raises(CheckUnavailable, match="uv is not installed"):
            step(context(Commands()))


def test_mkdocs_builds_strict_into_a_throwaway_folder_and_reports_warnings(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(shutil, "which", lambda name: "/bin/uv")
    runner = Commands()
    assert task_check_steps.mkdocs_step(context(runner, tmp_path)).ok
    [command] = runner.commands
    assert "mkdocs" in command and "--strict" in command and command[command.index("--site-dir") + 1] != str(tmp_path)
    failing = Commands()
    failing.codes["--strict"] = (1, "INFO fine\nWARNING - a broken link\nAborted with 1 warnings in strict mode!\n")
    result = task_check_steps.mkdocs_step(context(failing, tmp_path))
    assert not result.ok and result.output == ("WARNING - a broken link", "Aborted with 1 warnings in strict mode!")


def test_docs_check_fails_when_a_page_fails_the_standards(monkeypatch: pytest.MonkeyPatch) -> None:
    class Fake:
        pages = ("docs/guides/a.md", "docs/guides/b.md")
        failed: dict[str, list[str]] = {"docs/guides/b.md": ["finding"]}  # noqa: RUF012

    monkeypatch.setattr(docs_check, "context_for", lambda root: object())
    monkeypatch.setattr(docs_check, "check_changed", lambda root, base, ctx: Fake())
    monkeypatch.setattr(docs_cli, "page_lines", lambda result, page: [f"{page}: finding"])
    result = task_check_steps.docs_check_step(context(Commands()))
    assert not result.ok and result.output == ("docs/guides/b.md: finding",) and "1 pages fail" in result.detail
    Fake.failed = {}
    assert task_check_steps.docs_check_step(context(Commands())).ok


def test_no_check_runs_clang_tidy() -> None:
    assert "clang-tidy" not in task_check_steps.IMPLEMENTED
    source = Path(task_check_steps.__file__).read_text(encoding="utf-8")
    assert "run-clang-tidy" not in source and "clang-tidy-" not in source
