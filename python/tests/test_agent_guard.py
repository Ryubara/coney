# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/agent_guard.py, the Claude Code hook that guards how an Agent call starts an agent.

It refuses the Agent tool's `isolation: "worktree"` option (which puts the agent in a worktree of its own choosing
under `.claude/worktrees/`, outside the layout the guides require) and a call that is not one of Coney's roles: no
`subagent_type`, "general-purpose" or "claude". The roles are read from `.claude/agents/` at run time. The hook is a
standalone, standard-library-only file, so it is loaded by path, like the build guard.
"""

import importlib.util
import io
import json
from pathlib import Path
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run

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


ROLES = ["engineer", "reviewer", "tooling"]


def agent(subagent_type: object = "engineer", **extra: object) -> dict[str, object]:
    """An Agent call's input with the given type (pass None to leave the key out)."""
    call: dict[str, object] = {"description": "x", "prompt": "y", **extra}
    if subagent_type is not None:
        call["subagent_type"] = subagent_type
    return call


@pytest.mark.parametrize("isolation", ["worktree", "Worktree", " worktree ", "WORKTREE"])
def test_worktree_isolation_is_blocked(isolation: str) -> None:
    message = GUARD.refusal(agent(isolation=isolation), ROLES)
    assert message is not None and "../../worktrees/coney/" in message


@pytest.mark.parametrize(
    "extra",
    [{}, {"isolation": None}, {"isolation": "remote"}, {"model": "sonnet"}, {"prompt": "isolation: worktree in text"}],
)
def test_other_agent_calls_pass(extra: dict[str, object]) -> None:
    assert GUARD.refusal(agent(**extra), ROLES) is None


@pytest.mark.parametrize("role", [*ROLES, "Explore", "Plan", "fork", "claude-code-guide", "explore", " engineer "])
def test_the_roles_and_the_built_in_helpers_are_allowed(role: str) -> None:
    assert GUARD.refusal(agent(role), ROLES) is None


@pytest.mark.parametrize(
    "subagent_type", [None, "", "   ", "general-purpose", "General-Purpose", " claude ", "claude", 5, ["engineer"]]
)
def test_the_generic_agent_is_refused(subagent_type: object) -> None:
    message = GUARD.refusal(agent(subagent_type), ROLES)
    assert message is not None
    for name in ROLES:
        assert name in message
    assert "Explore" in message and "Plan" in message and "fork" in message and "claude-code-guide" in message
    assert "docs/guides/agents.md" in message and "engineer" in message


def test_a_missing_type_is_called_general_purpose_in_the_message() -> None:
    message = GUARD.refusal(agent(None), ROLES)
    assert message is not None and "general-purpose" in message


def test_a_type_that_is_no_role_is_refused_when_the_roles_are_known() -> None:
    message = GUARD.refusal(agent("statusline-setup"), ROLES)
    assert message is not None and '"statusline-setup"' in message


def test_with_no_roles_found_only_the_generic_types_are_refused() -> None:
    assert GUARD.refusal(agent("anything"), []) is None
    assert GUARD.refusal(agent("general-purpose"), []) is not None
    assert GUARD.refusal(agent(None), []) is not None


def test_the_advice_names_only_roles_that_exist() -> None:
    message = GUARD.refusal(agent(None), ["engineer"])
    assert message is not None
    assert "engineer:" in message and "reviewer:" not in message


def test_the_worktree_refusal_comes_before_the_role_one() -> None:
    message = GUARD.refusal(agent(None, isolation="worktree"), ROLES)
    assert message is not None and "isolation" in message


def test_the_roles_are_read_from_the_agents_folder(tmp_path: Path) -> None:
    (tmp_path / "engineer.md").write_text("---\nname: engineer\n---\n# Engineer\n", encoding="utf-8")
    (tmp_path / "file-name.md").write_text('---\nname: "named"\ndescription: x\n---\n', encoding="utf-8")
    (tmp_path / "plain.md").write_text("# no front matter\n", encoding="utf-8")
    (tmp_path / "ignored.txt").write_text("name: nope\n", encoding="utf-8")
    assert GUARD.roles(tmp_path) == ["engineer", "named", "plain"]


def test_the_coordinator_is_a_main_session_role_not_a_subagent(tmp_path: Path) -> None:
    (tmp_path / "coordinator.md").write_text("---\nname: coordinator\n---\n", encoding="utf-8")
    (tmp_path / "engineer.md").write_text("---\nname: engineer\n---\n", encoding="utf-8")
    assert GUARD.roles(tmp_path) == ["engineer"]
    assert GUARD.refusal({"subagent_type": "coordinator"}, ["engineer"]) is not None
    assert "coordinator" not in GUARD.roles()


def test_a_missing_agents_folder_gives_no_roles(tmp_path: Path) -> None:
    assert GUARD.roles(tmp_path / "absent") == []


def test_the_shipped_roles_are_found() -> None:
    assert set(GUARD.roles()) == {"engineer", "reviewer", "tooling"}


def test_a_new_role_file_is_allowed_without_editing_the_hook(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    (tmp_path / "cartographer.md").write_text("---\nname: cartographer\n---\n", encoding="utf-8")
    monkeypatch.setattr(GUARD, "AGENTS_DIR", tmp_path)
    assert run(monkeypatch, capsys, payload(agent("cartographer"))) == (0, "")
    assert run(monkeypatch, capsys, payload(agent("engineer")))[0] == 2


def test_a_blocked_worktree_call_exits_2_and_points_at_the_agents_guide(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, err = run(monkeypatch, capsys, payload(agent(isolation="worktree")))
    assert code == 2
    assert "docs/guides/agents.md" in err and "../../worktrees/coney/" in err


def test_a_generic_call_exits_2_and_names_the_roles(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, err = run(monkeypatch, capsys, payload({"prompt": "y", "model": "sonnet"}))
    assert code == 2
    assert "engineer" in err and "reviewer" in err and "docs/guides/agents.md" in err


def test_an_ordinary_agent_call_exits_0_silently(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert run(monkeypatch, capsys, payload(agent("engineer", model="sonnet"))) == (0, "")


@pytest.mark.parametrize(
    "stdin",
    ["", "not json", "[]", "{}", '{"tool_input": null}', '{"tool_input": "x"}'],
)
def test_input_the_hook_cannot_read_never_blocks(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run(monkeypatch, capsys, stdin) == (0, "")


def test_the_dispatcher_runs_the_hook_for_the_agent_tool_only() -> None:
    assert guards_run("Agent") == ["agent_guard", "slot_guard"]
    assert "agent_guard" not in guards_run("Bash", "ls")
