# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/build_guard.py, the Claude Code hook that refuses a raw cmake, ctest or ninja run.

The script is a standalone, standard-library-only file (the hook runs it on every shell command), so it is loaded by
path. The matcher must block a command only where the program starts one; text that merely mentions it passes, and so
does `coney-tools build`, which is the way to build.
"""

import importlib.util
import io
import json
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run, settings

SCRIPT = REPO / ".claude" / "hooks" / "build_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("build_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()

BLOCKED = [
    "cmake --build --preset dev",
    "cmake --preset dev",
    "cmake --build build/dev -j 8",
    "cmake -S . -B build/x",
    "cmake -B build/x",
    "cmake -DCMAKE_BUILD_TYPE=Debug --preset ci",
    "ctest --preset dev",
    "ctest --preset ci -R core",
    "ctest",
    "ctest -j8 --output-on-failure",
    "ninja",
    "ninja -C build/dev",
    "ninja -n",
    "  cmake --build --preset dev",
    "cd ../../worktrees/coney/x && cmake --build --preset dev",
    "cmake --preset dev && cmake --build --preset dev && ctest --preset dev",
    "echo start; ctest --preset dev",
    "make | ninja",
    "(cmake --preset dev)",
    "echo $(ctest --preset dev)",
    "FOO=1 ctest --preset dev",
    "CC=clang-19 CXX=clang++-19 cmake --preset dev -B build/tidy",
    "cmake.exe --build --preset dev",
    "ctest.exe --preset dev",
    "/usr/bin/ctest --preset dev",
    "C:\\tools\\cmake.exe --build build",
    '"C:/Program Files/CMake/bin/ctest.exe" --preset dev',
    '& "C:\\Program Files\\CMake\\bin\\cmake.exe" --build --preset dev',
    "& ctest --preset dev",
    'cmd /c "call vcvars64.bat && cmake --build --preset dev"',
    "cmake --build \\\n  --preset dev",
    "cmake `\n  --build --preset dev",
]

ALLOWED = [
    "",
    "ls -la",
    "uv run --project python coney-tools build --agent a --preset dev --test",
    "uv run --project python coney-tools build --agent a --preset dev --test -R core",
    "coney-tools build status",
    "coney-tools build release --agent a",
    "cmake --version",
    "cmake -E copy a b",
    "cmake -E remove_directory build/x",
    "cmake -P script.cmake",
    "ctest -N",
    "ctest --preset dev -N -R core",
    "ctest --show-only=json-v1",
    "ctest --version",
    "ctest --help",
    "ninja --version",
    "ninja -h",
    "./build/dev/coney_tests.exe",
    "build/dev/coney --help",
    "git log --oneline -- CMakeLists.txt",
    'echo "run cmake --build --preset dev"',
    "echo 'then ctest --preset dev'",
    'git commit -m "docs: use coney-tools build, not cmake --build and ctest"',
    "grep -rn 'ctest --preset' docs",
    'rg "cmake --build" .',
    "cat <<'EOF' > note.md\ncmake --build --preset dev\nctest --preset dev\nEOF",
    "cat <<EOF\nuse:\n  ninja -C build\nEOF\nls",
    "@'\ncmake --preset dev\n'@ | Set-Content note.md",
    "cmakelists.txt",
    "mycmake --build x",
    "cmake-gui",
    "ctest2 --preset dev",
    "ninjafoo",
    "pip install ninja-build",
]


@pytest.mark.parametrize("command", BLOCKED)
def test_a_raw_configure_build_or_test_run_is_blocked(command: str) -> None:
    assert GUARD.blocked(command), command


@pytest.mark.parametrize("command", ALLOWED)
def test_coney_tools_build_and_text_that_only_mentions_the_tools_passes(command: str) -> None:
    assert not GUARD.blocked(command), command


def run(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str) -> tuple[int, str]:
    """Run the hook on `stdin`; returns the exit code and what it wrote to stderr."""
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main()
    return code, capsys.readouterr().err


def payload(command: str, tool: str = "Bash") -> str:
    """The JSON Claude Code sends a PreToolUse hook."""
    return json.dumps({"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": {"command": command}})


def test_a_blocked_call_exits_2_and_names_the_replacement(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, err = run(monkeypatch, capsys, payload("cmake --build --preset dev"))
    assert code == 2
    assert "coney-tools build --agent" in err and "75" in err


def test_the_powershell_tool_is_checked_the_same_way(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert run(monkeypatch, capsys, payload("ctest --preset dev", "PowerShell"))[0] == 2


def test_the_way_to_build_exits_0_silently(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    command = "uv run --project python coney-tools build --agent a --preset dev --test"
    assert run(monkeypatch, capsys, payload(command)) == (0, "")


@pytest.mark.parametrize(
    "stdin", ["", "not json", "[]", "{}", '{"tool_input": null}', '{"tool_input": {"command": 5}}']
)
def test_input_the_hook_cannot_read_never_blocks(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run(monkeypatch, capsys, stdin) == (0, "")


def test_the_dispatcher_runs_the_hook_for_both_shell_tools_and_the_settings_allow_the_new_command() -> None:
    assert "build_guard" in guards_run("Bash", "ninja") and "build_guard" in guards_run("PowerShell", "ninja")
    allowed = settings()["permissions"]["allow"]
    assert "Bash(uv run --project python coney-tools build*)" in allowed
    assert not [rule for rule in allowed if rule.startswith(("Bash(cmake", "Bash(ctest", "Bash(ninja"))]
