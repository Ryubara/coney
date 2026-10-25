# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the guard that keeps an agent's `agent` argument its own on the xemu MCP tools: coney_tools.xemu_guard,
the hook script `.claude/hooks/xemu_guard.py` and the dispatcher's choice to run it. No xemu runs."""

import importlib.util
import io
import json
import sys
from pathlib import Path
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run
from xemu_support import make_copy

from coney_tools import pcsx2_proc, xemu_claims, xemu_guard
from coney_tools.xemu_claims import Registry
from coney_tools.xemu_guard import Guard, ids_in

READ = "mcp__xemu__xemu_read_memory"


@pytest.fixture
def guard(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Guard:
    """A guard over two copies; `alice` holds `xemu` (her holder pid lives), nobody holds `xemu-b`."""
    monkeypatch.setattr(xemu_claims, "xemu_processes", dict)
    monkeypatch.setattr(xemu_claims, "is_xemu", lambda pid: False)
    monkeypatch.setattr(xemu_claims, "port_open", lambda port: False)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid == 5)
    make_copy(tmp_path / "root", "xemu", 1234, 4444)
    make_copy(tmp_path / "root", "xemu-b", 1235, 4445)
    registry = Registry(tmp_path / "root", tmp_path / "claims")
    registry.claim("alice", "xemu", holder_pid=5)
    return Guard(registry, tmp_path / "claims" / "mcp")


def payload(tool: str, **arguments: object) -> dict[str, object]:
    return {"session_id": "s1", "agent_id": "sub1", "tool_name": tool, "tool_input": dict(arguments)}


def test_ids_come_from_claiming_xemu_commands_only() -> None:
    assert ids_in("uv run --project python coney-tools xemu claim --agent alice") == ["alice"]
    assert ids_in("coney-tools xemu record-run --agent=bob --name x") == ["bob"]
    assert ids_in("coney-tools xemu release --agent carol") == []
    assert ids_in("coney-tools pcsx2 claim --agent dave") == []


def test_a_call_without_agent_is_refused(guard: Guard) -> None:
    reason = xemu_guard.handle(payload(READ, address="0x10000"), guard)
    assert reason is not None and "needs `agent`" in reason


def test_an_id_the_caller_never_claimed_with_is_refused(guard: Guard) -> None:
    reason = xemu_guard.handle(payload(READ, agent="alice"), guard)
    assert reason is not None and "not an id you claimed" in reason


def test_the_caller_own_claim_passes(guard: Guard) -> None:
    assert xemu_guard.handle(payload("Bash", command="coney-tools xemu claim --agent alice"), guard) is None
    assert xemu_guard.handle(payload(READ, agent="alice"), guard) is None


def test_another_subagent_of_the_session_does_not_inherit_the_id(guard: Guard) -> None:
    xemu_guard.handle(payload("Bash", command="coney-tools xemu claim --agent alice"), guard)
    other = {**payload(READ, agent="alice"), "agent_id": "sub2"}
    assert xemu_guard.handle(other, guard) is not None


def test_an_id_without_a_claim_is_refused(guard: Guard) -> None:
    xemu_guard.handle(payload("Bash", command="coney-tools xemu claim --agent erin"), guard)
    reason = xemu_guard.handle(payload(READ, agent="erin"), guard)
    assert reason is not None and "holds no xemu claim" in reason


def test_no_copies_set_up_fails_open(tmp_path: Path) -> None:
    empty = Guard(Registry(tmp_path / "none", tmp_path / "claims"), tmp_path / "mcp")
    assert xemu_guard.handle(payload(READ), empty) is None


def test_other_tools_are_not_its_business(guard: Guard) -> None:
    assert xemu_guard.handle(payload("mcp__pcsx2__pcsx2_status"), guard) is None
    assert xemu_guard.handle({"tool_name": READ, "tool_input": {}}, guard) is None  # no session


def test_the_dispatcher_runs_it_for_the_xemu_tools_and_commands() -> None:
    assert guards_run("mcp__xemu__xemu_status") == ["xemu_guard"]
    for tool in ("Bash", "PowerShell"):
        assert "xemu_guard" in guards_run(tool, "uv run --project python coney-tools xemu claim --agent x")
        assert "xemu_guard" not in guards_run(tool, "git status")


def _hook() -> ModuleType:
    spec = importlib.util.spec_from_file_location("xemu_guard_hook", REPO / ".claude" / "hooks" / "xemu_guard.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_the_hook_script_blocks_with_exit_2(guard: Guard, monkeypatch: pytest.MonkeyPatch) -> None:
    hook = _hook()
    monkeypatch.setattr(xemu_guard, "default_guard", lambda start: guard)
    monkeypatch.setattr(sys, "stdin", io.StringIO(json.dumps(payload(READ, agent="alice"))))
    errors = io.StringIO()
    monkeypatch.setattr(sys, "stderr", errors)
    assert hook.main() == 2 and "not an id you claimed" in errors.getvalue()


def test_the_hook_script_passes_an_unreadable_payload(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    hook = _hook()
    monkeypatch.setattr(xemu_guard, "log_failure", lambda start, error: True)
    monkeypatch.setattr(sys, "stdin", io.StringIO("{not json"))
    assert hook.main() == 0
