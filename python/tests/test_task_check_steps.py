# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the individual checks of `task check`: the commands they run, how a failure reads and what is missing."""

import shutil
from collections.abc import Sequence
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


def context(runner: Commands, root: Path = REPO) -> StepContext:
    return StepContext(root, "origin/main", ("python/src/coney_tools/cli.py",), load_conventions(REPO), runner)


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
    assert result.ok and result.detail == "pytest python/tests: 4210 passed, 3 skipped"
    runner.codes["pytest"] = (0, "")
    assert task_check_steps.pytest_step(context(runner)).detail == "pytest python/tests"


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
