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
from ambient_env import CLEARED, KEPT, NOT_READ, OPT_IN
from conventions_support import REPO

from coney_tools import identity, unit

#: What a task's shell holds, set to another task's values.
TASK_SHELL = {
    "CONEY_TASK": "another-task",
    "VIRTUAL_ENV": str(REPO / "somewhere-else" / ".venv"),
    "CONEY_AGENT": "another-agent",
    "CLAUDECODE": "1",
}

#: One test of each kind that failed under `CONEY_TASK`: a build-record test (six of the seven) and the task review one.
#: The whole files were run in a task's shell by hand, as the pull request says.
PREVIOUSLY_FAILING = [
    "test_build_cli.py::test_a_run_that_built_and_tested_leaves_a_build_record",
    "test_task_review.py::test_the_environment_comes_from_the_checkout_and_its_branch",
]

#: A name that is ours or belongs to a tool around us: such a name in the code is a variable, however it is read.
PREFIXED = re.compile(r"(?:(?:CONEY|PCSX2|GHIDRA|CLAUDE|GITHUB|CMAKE)_[A-Z0-9_]*[A-Z0-9]|VIRTUAL_ENV|CLAUDECODE)")
#: What a variable's name looks like when it is read through a mapping: capitals first (`ProgramFiles(x86)` too).
READ_NAME = re.compile(r"[A-Z][A-Za-z0-9_()]*")
#: The names a mapping of the environment goes by in the code (`os.environ`, a parameter `env`, `base`, ...).
MAPPINGS = {"environ", "env", "base", "environment"}
#: Calls that read one variable: `os.getenv(name)`, `build_run.env_get(env, name)`, `env.get(name)`.
READERS = {"getenv", "env_get", "get", "pop", "setdefault"}


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


def _is_mapping(node: ast.expr) -> bool:
    """Whether `node` is the environment: `os.environ`, `self.env`, a parameter called `env`, ..."""
    if isinstance(node, ast.Attribute):
        return node.attr in MAPPINGS or _is_mapping(node.value)
    return isinstance(node, ast.Name) and node.id in MAPPINGS


def _string(node: ast.expr) -> str | None:
    """The text of a string literal, else None."""
    return node.value if isinstance(node, ast.Constant) and isinstance(node.value, str) else None


def _names_in(path: Path) -> set[str]:
    """The variables a Python file names: a literal that is exactly one of our prefixed names (however it is read), and
    the first literal argument of a read of the environment (`getenv`, `env_get`, `env.get`, `environ["X"]`,
    `"X" in env`). A name held in a variable and read from there is not seen."""
    names: set[str] = set()
    for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
        read: str | None = None
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and PREFIXED.fullmatch(node.value):
            names.add(node.value)
        elif isinstance(node, ast.Call):
            function = node.func
            called = function.attr if isinstance(function, ast.Attribute) else getattr(function, "id", "")
            on_mapping = isinstance(function, ast.Attribute) and _is_mapping(function.value)
            if called in ("getenv", "env_get") or (called in READERS and on_mapping):
                read = next((text for arg in node.args if (text := _string(arg)) is not None), None)
        elif isinstance(node, ast.Subscript) and _is_mapping(node.value):
            read = _string(node.slice)
        elif isinstance(node, ast.Compare) and any(isinstance(op, ast.In) for op in node.ops):
            read = _string(node.left) if any(_is_mapping(other) for other in node.comparators) else None
        if read is not None and READ_NAME.fullmatch(read):
            names.add(read)
    return names


def test_every_variable_the_code_reads_is_cleared_or_explained() -> None:
    """A tool that starts reading a new variable adds it to `ambient_env.py`, or this fails and says which."""
    sources = [path for folder in ("python/src", ".claude") for path in (REPO / folder).rglob("*.py")]
    sources = [path for path in sources if ".venv" not in path.parts and "__pycache__" not in path.parts]
    sources.append(REPO / "mkdocs_hooks.py")
    assert len(sources) > 100  # the scan walked the folders, not an empty list
    known = {*CLEARED, *KEPT, *NOT_READ}
    named: dict[str, str] = {}
    for source in sources:
        for name in _names_in(source):
            named.setdefault(name, source.relative_to(REPO).as_posix())
    unknown = {name: where for name, where in sorted(named.items()) if name not in known}
    assert unknown == {}, "add each to CLEARED (a tool reads it), KEPT (the machine needs it) or NOT_READ (a child's)"


def test_the_scan_sees_the_ways_a_variable_is_read(tmp_path: Path) -> None:
    """Each way the code reads the environment is found, so the check above is not blind to a spelling."""
    source = tmp_path / "reader.py"
    lines = [
        "import os",
        "a = os.environ.get('FROM_ENVIRON_GET')",
        "b = os.environ['FROM_ENVIRON_INDEX']",
        "c = env_get(env, 'FROM_ENV_GET')",
        "d = env.get('FROM_PARAMETER')",
        "e = 'FROM_IN' in base",
        "f = os.getenv('FROM_GETENV')",
        "g = 'CMAKE_FROM_PREFIX'",
        "h = config.get('lowercase_key')",
    ]
    source.write_text(os.linesep.join(lines) + os.linesep, encoding="utf-8")
    assert _names_in(source) == {
        "FROM_ENVIRON_GET",
        "FROM_ENVIRON_INDEX",
        "FROM_ENV_GET",
        "FROM_PARAMETER",
        "FROM_IN",
        "FROM_GETENV",
        "CMAKE_FROM_PREFIX",
    }


def test_the_lists_do_not_overlap_and_name_only_variables() -> None:
    """A name is in one list; the opt-in switches are not cleared."""
    lists = [CLEARED, KEPT, NOT_READ, OPT_IN]
    names = [name for table in lists for name in table]
    assert len(names) == len(set(names))
    assert all(re.fullmatch(r"[A-Za-z][A-Za-z0-9_()]*", name) for name in names)
    assert all(table[name] for table in lists for name in table)
