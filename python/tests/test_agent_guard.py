# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/agent_guard.py, the Claude Code hook that refuses an Agent call with worktree isolation.

The Agent tool's `isolation: "worktree"` option puts the agent in a worktree of its own choosing under
`.claude/worktrees/` with an automatic branch name, outside the layout the guides require. The hook is a standalone,
standard-library-only file, so it is loaded by path, like the build guard.
"""

import importlib.util
import io
import json
from types import ModuleType

import pytest
from conventions_support import REPO

SCRIPT = REPO / ".claude" / "hooks" / "agent_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("agent_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()


def run(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str) -> tuple[int, str]:
    """Run the hook on `stdin`; returns the exit code and what it wrote to stderr."""
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main()
    return code, capsys.readouterr().err


def payload(tool_input: object, tool: str = "Agent") -> str:
    """The JSON Claude Code sends a PreToolUse hook."""
    return json.dumps({"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": tool_input})


@pytest.mark.parametrize("isolation", ["worktree", "Worktree", " worktree ", "WORKTREE"])
def test_worktree_isolation_is_blocked(isolation: str) -> None:
    assert GUARD.blocked({"description": "x", "prompt": "y", "isolation": isolation})


@pytest.mark.parametrize(
    "tool_input",
    [
        {"description": "x", "prompt": "y"},
        {"prompt": "y", "isolation": None},
        {"prompt": "y", "isolation": ""},
        {"prompt": "y", "isolation": "remote"},
        {"prompt": "isolation: worktree is mentioned in the text only"},
        {"prompt": "y", "model": "sonnet", "subagent_type": "implementer"},
    ],
)
def test_other_agent_calls_pass(tool_input: dict[str, object]) -> None:
    assert not GUARD.blocked(tool_input)


def test_a_blocked_call_exits_2_and_points_at_the_agents_guide(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, err = run(monkeypatch, capsys, payload({"prompt": "y", "isolation": "worktree"}))
    assert code == 2
    assert "docs/guides/agents.md" in err and "../../worktrees/coney/" in err


def test_an_ordinary_agent_call_exits_0_silently(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert run(monkeypatch, capsys, payload({"prompt": "y", "model": "sonnet"})) == (0, "")


@pytest.mark.parametrize(
    "stdin",
    ["", "not json", "[]", "{}", '{"tool_input": null}', '{"tool_input": "x"}', '{"tool_input": {"isolation": 5}}'],
)
def test_input_the_hook_cannot_read_never_blocks(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run(monkeypatch, capsys, stdin) == (0, "")


def test_the_settings_register_the_hook_for_the_agent_tool() -> None:
    settings = json.loads((REPO / ".claude" / "settings.json").read_text(encoding="utf-8"))
    guard = [entry for entry in settings["hooks"]["PreToolUse"] if "agent_guard.py" in json.dumps(entry)]
    assert len(guard) == 1 and guard[0]["matcher"] == "Agent"
    assert "agent_guard.py" in guard[0]["hooks"][0]["command"] and guard[0]["hooks"][0]["type"] == "command"
