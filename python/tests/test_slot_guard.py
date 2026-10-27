# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/slot_guard.py, the Claude Code hook that refuses the coordinator's start of an engineer whose
slot is full (docs/guides/agents.md#slots).

The slot logic is `coney-tools task slots --admit` (tests/test_task_slots.py); these tests give the hook a stand-in for
that command and check what it sends, whom it holds to the rule and what it does when the command cannot run.
"""

import importlib.util
import io
import json
import sys
from types import ModuleType
from typing import Any

import pytest
from conventions_support import REPO
from hooks_support import DISPATCH, guards_run

from coney_tools import task_slots

SCRIPT = REPO / ".claude" / "hooks" / "slot_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("slot_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()


class Fake:
    """A stand-in for `coney-tools`: records what it was run with and answers as told."""

    def __init__(self, code: int = 0, out: str = "") -> None:
        self.code, self.out = code, out
        self.calls: list[tuple[list[str], str]] = []

    def __call__(self, args: list[str], stdin: str) -> tuple[int, str]:
        self.calls.append((args, stdin))
        return self.code, self.out


def call(
    subagent_type: object = "engineer",
    prompt: object = "Task id: x-1",
    agent_type: object = "coordinator",
    **extra: Any,
) -> dict[str, Any]:
    """The hook input of an Agent call."""
    tool_input: dict[str, Any] = {"description": "d", "prompt": prompt}
    if subagent_type is not None:
        tool_input["subagent_type"] = subagent_type
    payload: dict[str, Any] = {"hook_event_name": "PreToolUse", "tool_name": "Agent", "tool_input": tool_input, **extra}
    if agent_type is not None:
        payload["agent_type"] = agent_type
    return payload


def test_the_coordinator_start_is_sent_to_the_tool_with_the_brief_and_the_type() -> None:
    fake = Fake()
    assert GUARD.refusal(call(prompt="Hello\nTask id: x-1\n"), fake) is None
    ((args, stdin),) = fake.calls
    assert args == ["task", "slots", "--admit", "--agent-type=engineer", "--brief", "-", "--reserve"]
    assert stdin == "Hello\nTask id: x-1\n"


def test_a_refusal_of_the_tool_is_the_hooks_reason() -> None:
    reason = GUARD.refusal(call(), Fake(3, "Blocked: no task slot is free on this machine (3 of 3 in use)."))
    assert reason is not None and reason.startswith("Blocked: no task slot is free") and reason.endswith("\n")


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer", "Explore", "fork"])
def test_every_type_is_sent_and_the_tool_decides(agent_type: str) -> None:
    fake = Fake()
    GUARD.refusal(call(agent_type), fake)
    assert fake.calls[0][0][3] == f"--agent-type={agent_type}"


def test_a_call_with_no_type_is_sent_with_an_empty_type() -> None:
    fake = Fake()
    GUARD.refusal(call(None), fake)
    assert fake.calls[0][0][3] == "--agent-type="


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer", None, "general-purpose"])
def test_only_the_coordinator_is_held_to_the_rule(agent_type: object) -> None:
    fake = Fake(3, "Blocked")
    assert GUARD.refusal(call(agent_type=agent_type), fake) is None and fake.calls == []


@pytest.mark.parametrize("tool", ["Bash", "Read", "Write", "mcp__x__y"])
def test_other_tools_pass_untouched(tool: str) -> None:
    fake = Fake(3, "Blocked")
    payload = call()
    payload["tool_name"] = tool
    assert GUARD.refusal(payload, fake) is None and fake.calls == []


@pytest.mark.parametrize("payload", [None, "x", [], {"tool_name": "Agent"}, call(prompt=None)])
def test_input_it_cannot_read_passes_or_is_sent_empty(payload: object) -> None:
    fake = Fake()
    assert GUARD.refusal(payload, fake) is None


def test_a_tool_that_cannot_run_passes_the_call_and_leaves_a_line(
    tmp_path: Any, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    assert GUARD.refusal(call(), Fake(2, "uv: not found")) is None
    assert "could not check the slots (exit 2)" in (tmp_path / "coney-hooks.log").read_text(encoding="utf-8")


def test_a_start_let_through_with_the_claim_unchecked_leaves_a_line(
    tmp_path: Any, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    out = "1 of 3 task slots in use with x-1\nwarning: the claim of 'x-1' was not checked, GitHub could not be read"
    assert GUARD.refusal(call(), Fake(0, out)) is None
    log = (tmp_path / "coney-hooks.log").read_text(encoding="utf-8")
    assert "let through with a check skipped: the claim of 'x-1' was not checked" in log


def test_a_plain_start_leaves_no_line(tmp_path: Any, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    assert GUARD.refusal(call(), Fake(0, "1 of 3 task slots in use with x-1")) is None
    assert not (tmp_path / "coney-hooks.log").exists()


@pytest.mark.parametrize("code", [1, 2, 4, 127, 255])
def test_every_exit_code_but_the_refusal_passes_the_call(
    code: int, tmp_path: Any, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    assert GUARD.refusal(call(), Fake(code, "")) is None


def test_the_refusal_code_is_the_one_the_tool_exits_with() -> None:
    assert GUARD.REFUSED == task_slots.REFUSED


def test_the_hook_waits_longer_than_the_longest_check() -> None:
    # A hook that times out passes the call uncounted, so its budget covers the tool's own longest run (the GitHub
    # reads within their budget, then the lock wait) and the `uv run` start-up; Claude Code's timeout for the
    # PreToolUse dispatcher covers the hook.
    assert GUARD.TIMEOUT >= task_slots.ADMIT_MAX_SECONDS + 20
    settings = json.loads((REPO / ".claude" / "settings.json").read_text(encoding="utf-8"))
    (entry,) = settings["hooks"]["PreToolUse"]
    assert entry["hooks"][0]["timeout"] > GUARD.TIMEOUT


def test_a_refusal_with_no_words_is_not_a_block(tmp_path: Any, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    assert GUARD.refusal(call(), Fake(GUARD.REFUSED, "")) is None


def test_main_blocks_with_exit_2_and_the_reason_on_stderr(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(GUARD, "run_coney_tools", Fake(3, "Blocked: full"))
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(json.dumps(call()).encode("utf-8"))))
    assert GUARD.main() == 2
    assert "Blocked: full" in capsys.readouterr().err


def test_main_passes_input_it_cannot_read(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(b"not json")))
    assert GUARD.main() == 0


# --- in the dispatcher ----------------------------------------------------------------------------------------------


def test_the_dispatcher_runs_the_slot_guard_for_the_agent_tool_last() -> None:
    assert guards_run("Agent") == ["agent_guard", "slot_guard"]
    assert guards_run("Bash", "ls") != [] and "slot_guard" not in guards_run("Bash", "ls")
    assert DISPATCH.LAST_GUARDS == ("slot_guard",)


def dispatch_agent(
    monkeypatch: pytest.MonkeyPatch, fake: Fake, subagent_type: str = "engineer"
) -> tuple[int, str, str]:
    """An Agent call of the coordinator through the dispatcher, with the slot check replaced by `fake`."""
    monkeypatch.setattr(DISPATCH.guard_module("slot_guard"), "run_coney_tools", fake)
    result: tuple[int, str, str] = DISPATCH.dispatch(json.dumps(call(subagent_type)), {"roles": {"coordinator": {}}})
    return result


def test_a_full_slot_blocks_the_call_through_the_dispatcher(monkeypatch: pytest.MonkeyPatch) -> None:
    fake = Fake(3, "Blocked: no task slot is free on this machine (3 of 3 in use).")
    code, _, err = dispatch_agent(monkeypatch, fake)
    assert code == 2 and "no task slot is free" in err and len(fake.calls) == 1


def test_a_call_another_guard_refuses_keeps_no_slot(monkeypatch: pytest.MonkeyPatch) -> None:
    fake = Fake(0, "ok")
    code, _, err = dispatch_agent(monkeypatch, fake, subagent_type="general-purpose")
    assert code == 2 and "general-purpose" in err
    assert fake.calls == []  # nothing was asked, so nothing was reserved


# --- the real runner ------------------------------------------------------------------------------------------------


class Done:
    """What `subprocess.run` returns."""

    def __init__(self, code: int, out: str, err: str) -> None:
        self.returncode, self.stdout, self.stderr = code, out.encode("utf-8"), err.encode("utf-8")


def test_the_runner_starts_the_tool_from_the_project_without_the_sessions_virtual_env(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Any
) -> None:
    seen: dict[str, Any] = {}

    def fake_run(command: list[str], **options: Any) -> Done:
        seen.update(command=command, **options)
        return Done(task_slots.REFUSED, "Blocked: full\n", "warning: VIRTUAL_ENV does not match")

    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(tmp_path))
    monkeypatch.setenv("VIRTUAL_ENV", "elsewhere")
    monkeypatch.setattr("subprocess.run", fake_run)
    assert GUARD.run_coney_tools(["task", "slots"], "brief") == (task_slots.REFUSED, "Blocked: full")
    assert seen["command"][:5] == ["uv", "run", "--quiet", "--project", str(tmp_path / "python")]
    assert seen["command"][5:] == ["coney-tools", "task", "slots"] and seen["input"] == b"brief"
    assert "VIRTUAL_ENV" not in seen["env"] and seen["cwd"] == tmp_path


def test_the_runner_keeps_the_error_text_of_a_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("subprocess.run", lambda command, **options: Done(2, "", "usage: coney-tools"))
    assert GUARD.run_coney_tools(["task"], "") == (2, "usage: coney-tools")


def test_the_runner_reports_a_tool_that_cannot_start(monkeypatch: pytest.MonkeyPatch) -> None:
    def fail(command: list[str], **options: Any) -> Done:
        raise FileNotFoundError("uv")

    monkeypatch.setattr("subprocess.run", fail)
    assert GUARD.run_coney_tools(["task"], "") == (2, "uv")


# --- the real runner against a real crash ----------------------------------------------------------------------------


def stand_in_tool(monkeypatch: pytest.MonkeyPatch, script: str) -> None:
    """Make the hook run a Python one-liner where it would run `coney-tools`."""
    monkeypatch.setattr(GUARD, "tool_command", lambda args: [sys.executable, "-c", script])


def test_a_real_crash_of_the_tool_passes_the_call_and_leaves_a_line(
    tmp_path: Any, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    stand_in_tool(monkeypatch, "raise RuntimeError('boom')")  # Python exits 1 with the traceback on stderr
    assert GUARD.refusal(call()) is None
    log = (tmp_path / "coney-hooks.log").read_text(encoding="utf-8")
    assert "could not check the slots (exit 1)" in log and "RuntimeError: boom" in log


def test_a_real_refusal_blocks_with_the_tools_words_and_not_its_stderr(monkeypatch: pytest.MonkeyPatch) -> None:
    script = f"import sys; sys.stderr.write('uv warning'); print('Blocked: full'); sys.exit({task_slots.REFUSED})"
    stand_in_tool(monkeypatch, script)
    assert GUARD.refusal(call()) == "Blocked: full\n"


def test_a_real_success_passes(monkeypatch: pytest.MonkeyPatch) -> None:
    stand_in_tool(monkeypatch, "print('ok')")
    assert GUARD.refusal(call()) is None


def test_the_real_tool_refuses_an_unknown_task_through_the_hook(tmp_path: Any, monkeypatch: pytest.MonkeyPatch) -> None:
    # End to end: the hook runs the real `coney-tools task slots --admit` for an engineer brief that names no task id.
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(REPO))
    reason = GUARD.refusal(call(prompt="Fix the doors."))
    assert reason is not None and "names no unit" in reason and "Task id: <id>" in reason
