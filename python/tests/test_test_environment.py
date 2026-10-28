# SPDX-License-Identifier: GPL-3.0-or-later
"""The suite does not depend on the shell that runs it: `conftest.py` clears the variables the tools read.

Inside a task the shell has `CONEY_TASK` and `VIRTUAL_ENV`, and a run of the whole suite there once failed seven tests
that pass in a clean shell. These tests prove the fix three ways: the variables are gone in a test, the real tests that
failed pass with the variables set, and a variable a tool starts reading must be added to the list.
"""

from __future__ import annotations

import ast
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest
from ambient_env import CLEARED, NOT_READ, OPT_IN
from conventions_support import REPO

from coney_tools import identity, unit

#: What a task's shell holds, set to another task's values.
TASK_SHELL = {
    "CONEY_TASK": "another-task",
    "VIRTUAL_ENV": str(REPO / "somewhere-else" / ".venv"),
    "CONEY_AGENT": "another-agent",
    "CLAUDECODE": "1",
}

#: Tests that failed under `CONEY_TASK`, as the issue lists them, run here by name.
PREVIOUSLY_FAILING = [
    "test_build_cli.py::test_a_run_that_built_and_tested_leaves_a_build_record",
    "test_task_review.py::test_the_environment_comes_from_the_checkout_and_its_branch",
]

#: A name that looks like a variable of ours or of the tools around us: `CONEY_X`, `PCSX2_X`, `VIRTUAL_ENV`.
VARIABLE = re.compile(r"(?:(?:CONEY|PCSX2|GHIDRA|CLAUDE|GITHUB)_[A-Z0-9_]*[A-Z0-9]|VIRTUAL_ENV|CLAUDECODE)")


def test_the_variables_the_tools_read_are_not_set_in_a_test() -> None:
    """The fixture ran: none of the listed variables reaches the test, whatever the shell had."""
    assert [name for name in CLEARED if name in os.environ] == []
    assert identity.current().verified is False


def test_a_test_can_set_a_cleared_variable_for_itself(monkeypatch: pytest.MonkeyPatch) -> None:
    """The fixture shares the test's `monkeypatch`, so a test that needs `CONEY_TASK` sets it and tools see it."""
    monkeypatch.setenv("CONEY_TASK", "mine")
    assert unit.task_name(REPO) == "mine"


@pytest.mark.parametrize("test", PREVIOUSLY_FAILING)
def test_the_tests_that_failed_in_a_tasks_shell_pass_in_one(test: str) -> None:
    """Run each as pytest does from a task's shell (`CONEY_TASK`, `VIRTUAL_ENV`, `CLAUDECODE` set to other values)."""
    here = Path(__file__).resolve().parent
    done = subprocess.run(
        [sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider", str(here / test)],
        cwd=REPO,
        env={**os.environ, **TASK_SHELL},
        capture_output=True,
        text=True,
        check=False,
    )
    assert done.returncode == 0, done.stdout[-3000:]
    assert "1 passed" in done.stdout


def _names_in(path: Path) -> set[str]:
    """The string literals of a Python file that are exactly the name of a variable (not prose that mentions one)."""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    return {
        node.value
        for node in ast.walk(tree)
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and VARIABLE.fullmatch(node.value)
    }


def test_every_variable_the_code_names_is_cleared_or_explained() -> None:
    """A tool that starts reading a new variable adds it to `ambient_env.py`, or this fails and says which."""
    sources = [*(REPO / "python/src/coney_tools").glob("*.py"), *(REPO / ".claude/hooks").glob("*.py")]
    sources.append(REPO / "mkdocs_hooks.py")
    named: dict[str, str] = {}
    for source in sources:
        for name in _names_in(source):
            named.setdefault(name, source.relative_to(REPO).as_posix())
    unknown = {name: where for name, where in sorted(named.items()) if name not in CLEARED and name not in NOT_READ}
    assert unknown == {}, "add each to CLEARED (a tool reads it) or NOT_READ (a tool only sets it) in ambient_env.py"


def test_the_lists_do_not_overlap_and_name_only_variables() -> None:
    """A name is in one list; the opt-in switches are not cleared."""
    lists = [CLEARED, NOT_READ, OPT_IN]
    names = [name for table in lists for name in table]
    assert len(names) == len(set(names))
    assert all(re.fullmatch(r"[A-Z][A-Z0-9_]*", name) for name in names)
    assert all(table[name] for table in lists for name in table)
