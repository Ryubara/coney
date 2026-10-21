# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the guard that keeps an agent off a PCSX2 copy it has not claimed: `Registry.require_ports`,
`DebugClient.claimed`, `coney_tools.pcsx2_guard` (the decisions of the hook on the `pcsx2` MCP tools) and the hook
script `.claude/hooks/pcsx2_guard.py`.

The MCP server is one process for a whole agent session, so one agent's `pcsx2_connect` moves the connection of every
other agent in it. The guard lets an agent connect only to the ports of a copy it claimed (both ports, whatever the
mode) and locks nothing else. No PCSX2 runs: the process layer is replaced by a fake, as in test_pcsx2_claims.py.
"""

import importlib.util
import io
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
from collections.abc import Iterator
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest
from conventions_support import REPO
from test_pcsx2_claims import FakeProcesses, make_copy

from coney_tools import pcsx2_claims, pcsx2_claims_cli, pcsx2_guard, pcsx2_proc
from coney_tools.debug_server import DebugClient, DebugServerError
from coney_tools.pcsx2_claims import ClaimError, Registry
from coney_tools.pcsx2_guard import BINDING_KEEP_SECONDS, Guard

NOW = 1_800_000_000.0
CONNECT = "mcp__pcsx2__pcsx2_connect"


class Clock:
    """A clock the test moves."""

    def __init__(self) -> None:
        self.now = NOW

    def __call__(self) -> float:
        return self.now


@pytest.fixture
def procs(monkeypatch: pytest.MonkeyPatch) -> FakeProcesses:
    fake = FakeProcesses()
    monkeypatch.setattr(pcsx2_proc, "pcsx2_processes", lambda: dict(fake.pcsx2))
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid in fake.pcsx2 or pid in fake.alive)
    monkeypatch.setattr(pcsx2_proc, "is_pcsx2", lambda pid: pid in fake.pcsx2)
    monkeypatch.setattr(pcsx2_proc, "terminate", fake.terminate)
    monkeypatch.setattr(pcsx2_claims, "port_open", lambda port: False)
    return fake


@pytest.fixture
def clock() -> Clock:
    return Clock()


@pytest.fixture
def registry(tmp_path: Path, procs: FakeProcesses, clock: Clock) -> Registry:
    """Three copies: pcsx2 (PINE 28011, debug 21512), pcsx2-b (28012, 21513) and pcsx2-c (28013, 21514)."""
    for index, name in enumerate(("pcsx2", "pcsx2-b", "pcsx2-c")):
        make_copy(tmp_path / "root", name, 28011 + index, debug=21512 + index)
    return Registry(tmp_path / "root", tmp_path / "claims", clock=clock)


@pytest.fixture
def guard(registry: Registry, tmp_path: Path) -> Guard:
    return Guard(registry, tmp_path / "claims" / "mcp")


# -- Registry.require_ports ----------------------------------------------------------------------------------------


def test_the_ports_of_a_copy_the_agent_holds_pass(registry: Registry) -> None:
    held, _ = registry.claim("alice", "pcsx2-b")
    assert registry.require_ports(["alice"], debug_port=21513, pine_port=28012) == held
    assert registry.require_ports(["alice"], debug_port=21513) == held
    assert registry.require_ports(["alice"], pine_port=28012) == held


def test_the_ports_of_a_copy_another_agent_holds_are_refused(registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    with pytest.raises(ClaimError, match=r"pcsx2-b is held by alice, not bob"):
        registry.require_ports(["bob"], debug_port=21513, pine_port=28012)


def test_the_ports_of_an_unclaimed_copy_are_refused(registry: Registry) -> None:
    with pytest.raises(ClaimError, match=r"pcsx2-c is not claimed.*pcsx2 claim --agent"):
        registry.require_ports(["alice"], debug_port=21514)


def test_the_ports_of_a_stale_claim_are_refused(registry: Registry, clock: Clock) -> None:
    registry.claim("alice", "pcsx2-b")
    clock.now += registry.max_age + 1  # no process of the claim lives, and it is older than the maximum age
    with pytest.raises(ClaimError, match=r"stale"):
        registry.require_ports(["alice"], debug_port=21513)


def test_a_port_of_no_copy_is_refused(registry: Registry) -> None:
    registry.claim("alice", "pcsx2")
    with pytest.raises(ClaimError, match=r"21999.*debug server port of no PCSX2 copy"):
        registry.require_ports(["alice"], debug_port=21999)
    with pytest.raises(ClaimError, match=r"28999.*PINE port of no PCSX2 copy"):
        registry.require_ports(["alice"], pine_port=28999)


def test_a_debug_port_and_a_pine_port_of_two_copies_are_refused(registry: Registry) -> None:
    registry.claim("alice", "pcsx2")
    registry.claim("bob", "pcsx2-b")
    with pytest.raises(ClaimError, match=r"two different copies"):
        registry.require_ports(["alice", "bob"], debug_port=21512, pine_port=28012)


def test_no_port_at_all_is_refused(registry: Registry) -> None:
    with pytest.raises(ClaimError, match=r"names no port"):
        registry.require_ports(["alice"])


def test_any_of_the_names_an_agent_goes_by_may_hold_the_claim(registry: Registry) -> None:
    held, _ = registry.claim("alice-2", "pcsx2-c")
    assert registry.require_ports(["alice", "alice-2"], debug_port=21514) == held


def test_check_ports_succeeds_for_a_held_copy_and_refuses_another_agents(
    registry: Registry, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    registry.claim("alice", "pcsx2-b")
    assert pcsx2_claims_cli.run_check_ports("alice", 21513, 28012) == 0
    assert "alice holds pcsx2-b" in capsys.readouterr().out
    with pytest.raises(ClaimError, match=r"held by alice, not bob"):
        pcsx2_claims_cli.run_check_ports("bob", 21513, None)


def test_check_ports_is_a_command() -> None:
    from coney_tools.cli import _build_parser

    args = _build_parser().parse_args(["pcsx2", "check-ports", "--agent", "a", "--debug-port", "21513"])
    assert (args.command, args.agent, args.debug_port, args.pine_port) == ("check-ports", "a", 21513, None)


# -- DebugClient.claimed -------------------------------------------------------------------------------------------


@pytest.fixture
def debug_server() -> Iterator[tuple[int, list[int]]]:
    """A listening socket standing in for a debug server: its port, and the connections it accepted."""
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen()
    accepted: list[int] = []

    def accept() -> None:
        try:
            while True:
                connection, _ = server.accept()
                accepted.append(1)
                connection.close()
        except OSError:
            return

    threading.Thread(target=accept, daemon=True).start()
    yield server.getsockname()[1], accepted
    server.close()


def test_a_debug_client_connects_to_the_agents_own_copy(
    tmp_path: Path, procs: FakeProcesses, debug_server: tuple[int, list[int]]
) -> None:
    port, _ = debug_server
    make_copy(tmp_path / "root", "pcsx2", 28011, debug=port)
    registry = Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)
    registry.claim("alice", "pcsx2")
    with DebugClient.claimed("alice", registry=registry) as client:
        assert client.port == port
    with DebugClient.claimed("alice", "pcsx2", registry=registry) as client:
        assert client.port == port


def test_a_debug_client_never_connects_to_a_copy_another_agent_holds(
    tmp_path: Path, procs: FakeProcesses, debug_server: tuple[int, list[int]]
) -> None:
    port, accepted = debug_server
    make_copy(tmp_path / "root", "pcsx2", 28011, debug=port)
    registry = Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)
    registry.claim("alice", "pcsx2")
    with pytest.raises(ClaimError, match=r"held by alice, not bob"):
        DebugClient.claimed("bob", "pcsx2", registry=registry)
    with pytest.raises(ClaimError, match=r"bob holds no PCSX2 copy"):
        DebugClient.claimed("bob", registry=registry)
    assert accepted == []


def test_a_debug_client_needs_the_copy_named_when_the_agent_holds_two(tmp_path: Path, procs: FakeProcesses) -> None:
    make_copy(tmp_path / "root", "pcsx2", 28011, debug=21512)
    make_copy(tmp_path / "root", "pcsx2-b", 28012, debug=21513)
    registry = Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)
    registry.claim("alice", "pcsx2")
    registry.claim("alice", "pcsx2-b")
    with pytest.raises(ClaimError, match=r"holds pcsx2, pcsx2-b.*name one"):
        DebugClient.claimed("alice", registry=registry)


def test_a_debug_client_of_a_copy_without_a_debug_server_says_so(tmp_path: Path, procs: FakeProcesses) -> None:
    make_copy(tmp_path / "root", "pcsx2", 28011)
    registry = Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)
    registry.claim("alice", "pcsx2")
    with pytest.raises(DebugServerError, match=r"debug server is off"):
        DebugClient.claimed("alice", registry=registry)


# -- who is calling ------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("command", "names"),
    [
        ("uv run --project python coney-tools pcsx2 claim --agent alice", ["alice"]),
        ("uv run --project python coney-tools pcsx2 launch --agent=alice-2 --state x.p2s", ["alice-2"]),
        ("coney-tools pcsx2 record --agent alice && coney-tools build --agent other --test", ["alice"]),
        ("coney-tools pcsx2 claim --agent a; coney-tools pcsx2 claim --agent b", ["a", "b"]),
        ("coney-tools pcsx2 status", []),
        ("coney-tools build --agent alice", []),
        ("echo coney-tools pcsx2x --agent alice", []),
        ("coney-tools pcsx2 claim --agent 'bad name'", []),
        ("coney-tools pcsx2 claim --agent 'alice'", ["alice"]),
        ('coney-tools pcsx2 claim --agent "alice"', ["alice"]),
        ("coney-tools pcsx2 claim \\\n  --agent alice", ["alice"]),
        ("coney-tools pcsx2 claim `\r\n  --agent alice", ["alice"]),
        ("coney-tools pcsx2 release --agent other", []),
        ("coney-tools pcsx2 keys --agent other --copy pcsx2 space", []),
        ("coney-tools pcsx2 recipe --agent alice list", ["alice"]),
    ],
)
def test_the_agent_ids_of_a_pcsx2_command_are_read_from_its_agent_flags(command: str, names: list[str]) -> None:
    assert pcsx2_guard.agents_in(command) == names


def test_a_caller_is_bound_to_the_ids_it_used_in_pcsx2_commands(guard: Guard) -> None:
    assert guard.bound("s1", "agent-1") == set()
    guard.bind("s1", "agent-1", ["alice"])
    guard.bind("s1", "agent-1", ["alice-2"])
    guard.bind("s1", "agent-2", ["bob"])
    guard.bind("s2", "agent-1", ["carol"])
    assert guard.bound("s1", "agent-1") == {"alice", "alice-2"}
    assert guard.bound("s1", "agent-2") == {"bob"}
    assert guard.bound("s2", "agent-1") == {"carol"}
    assert guard.bound("s1", "") == set()  # the session's own thread has an id of its own


# -- the decisions -------------------------------------------------------------------------------------------------


def connect(guard: Guard, agent_id: str = "agent-1", **arguments: object) -> str | None:
    """The guard's answer to a pcsx2_connect call (None: allowed, else the reason it is refused)."""
    return guard.decide("s1", agent_id, CONNECT, {"mode": "debug", **arguments})


def test_a_connect_to_the_ports_of_the_callers_claim_is_allowed(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    assert connect(guard, agent="alice", debug_port=21513, pine_port=28012) is None


def test_a_connect_by_someone_who_never_claimed_through_a_shell_says_how(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    reason = connect(guard, agent="alice", debug_port=21513, pine_port=28012)
    assert reason is not None and "coney-tools pcsx2 claim --agent" in reason


def test_a_connect_to_the_ports_of_another_agents_claim_is_refused(guard: Guard, registry: Registry) -> None:
    registry.claim("coop-1", "pcsx2-b")
    guard.bind("s1", "agent-1", ["recipes-1"])
    reason = connect(guard, agent="recipes-1", debug_port=21513, pine_port=28012)
    assert reason is not None and "held by coop-1, not recipes-1" in reason


def test_a_connect_to_a_copy_nobody_claimed_is_refused(guard: Guard) -> None:
    guard.bind("s1", "agent-1", ["alice"])
    reason = connect(guard, agent="alice", debug_port=21514, pine_port=28013)
    assert reason is not None and "not claimed" in reason


@pytest.mark.parametrize("mode", ["auto", "debug", "pine"])
def test_both_ports_are_needed_whatever_the_mode(guard: Guard, registry: Registry, mode: str) -> None:
    registry.claim("alice", "pcsx2")
    guard.bind("s1", "agent-1", ["alice"])
    both = {"debug_port": 21512, "pine_port": 28011, "mode": mode, "agent": "alice"}
    assert guard.decide("s1", "agent-1", CONNECT, both) is None
    for missing in ("debug_port", "pine_port"):
        given = {key: value for key, value in both.items() if key != missing}
        reason = guard.decide("s1", "agent-1", CONNECT, given)
        assert reason is not None and missing in reason and "pcsx2_status" in reason
    reason = guard.decide("s1", "agent-1", CONNECT, {"agent": "alice"})
    assert reason is not None and "debug_port" in reason


def test_ports_of_two_copies_in_one_connect_are_refused(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2")
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    reason = guard.decide("s1", "agent-1", CONNECT, {"debug_port": 21512, "pine_port": 28012, "agent": "alice"})
    assert reason is not None and "two different copies" in reason


def test_nothing_is_locked_for_the_other_agents_of_a_session(guard: Guard, registry: Registry) -> None:
    registry.claim("fixer-4", "pcsx2")
    registry.claim("recipes-1", "pcsx2-b")
    guard.bind("s1", "agent-fixer", ["fixer-4"])
    guard.bind("s1", "agent-recipes", ["recipes-1"])
    assert connect(guard, "agent-fixer", agent="fixer-4", debug_port=21512, pine_port=28011) is None
    assert connect(guard, "agent-recipes", agent="recipes-1", debug_port=21513, pine_port=28012) is None
    assert connect(guard, "agent-fixer", agent="fixer-4", debug_port=21512, pine_port=28011) is None
    for tool in ("pcsx2_write_memory", "pcsx2_pause", "pcsx2_read_memory"):
        assert guard.decide("s1", "agent-recipes", f"mcp__pcsx2__{tool}", {}) is None


def test_an_agent_argument_must_be_an_id_the_caller_claimed_with(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    for tool in ("pcsx2_connect", "pcsx2_status", "pcsx2_write_memory", "pcsx2_pause"):
        ports = {"debug_port": 21513, "pine_port": 28012} if tool == "pcsx2_connect" else {}
        assert guard.decide("s1", "agent-1", f"mcp__pcsx2__{tool}", {**ports, "agent": "alice"}) is None
        reason = guard.decide("s1", "agent-1", f"mcp__pcsx2__{tool}", {**ports, "agent": "bob"})
        assert reason is not None and "'bob'" in reason and "alice" in reason
    # Another caller that never claimed has no id to name.
    reason = guard.decide("s1", "agent-2", "mcp__pcsx2__pcsx2_write_memory", {"agent": "alice"})
    assert reason is not None and "not an id you claimed" in reason


def test_a_call_without_an_agent_argument_is_the_servers_business(guard: Guard) -> None:
    # Whether `agent` is present is for the server to require; the guard only checks it where given.
    assert guard.decide("s1", "agent-1", "mcp__pcsx2__pcsx2_write_memory", {"address": "0x100000"}) is None


def test_a_connection_is_named_after_the_claim_it_connects_to(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2")
    registry.claim("alice-2", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice", "alice-2"])
    wrong = {"debug_port": 21513, "pine_port": 28012, "agent": "alice"}
    reason = guard.decide("s1", "agent-1", CONNECT, wrong)
    assert reason is not None and "name your connection after the claim" in reason and "'alice-2'" in reason
    assert guard.decide("s1", "agent-1", CONNECT, {**wrong, "agent": "alice-2"}) is None


@pytest.mark.parametrize(
    "tool",
    ["mcp__pcsx2__pcsx2_status", "mcp__pcsx2__ps2recomp_lookup_function", "mcp__ghidra-mcp__list_functions", "Bash"],
)
def test_other_tools_are_not_the_guards_business(guard: Guard, tool: str) -> None:
    assert guard.decide("s1", "agent-1", tool, {}) is None


def test_with_no_copies_set_up_the_guard_stays_out_of_the_way(tmp_path: Path, procs: FakeProcesses) -> None:
    empty = Registry(tmp_path / "nothing", tmp_path / "claims", clock=lambda: NOW)
    guard = Guard(empty, tmp_path / "claims" / "mcp")
    assert guard.decide("s1", "agent-1", CONNECT, {"debug_port": 21512, "pine_port": 28011}) is None


def test_bindings_nobody_renewed_for_days_are_pruned(guard: Guard) -> None:
    guard.bind("old", "agent-1", ["alice"])
    guard.bind("kept", "agent-1", ["bob"])
    (old,) = guard.folder.glob("old.bind.agent-1.*")
    long_ago = old.stat().st_mtime - BINDING_KEEP_SECONDS - 60
    os.utime(old, (long_ago, long_ago))
    guard.bind("new", "agent-1", ["carol"])
    assert not old.exists()
    assert guard.bound("kept", "agent-1") == {"bob"} and guard.bound("new", "agent-1") == {"carol"}
    # A caller that claims again renews its binding.
    (kept,) = guard.folder.glob("kept.bind.agent-1.*")
    os.utime(kept, (long_ago, long_ago))
    guard.bind("kept", "agent-1", ["bob"])
    guard.bind("other", "agent-1", ["dave"])
    assert guard.bound("kept", "agent-1") == {"bob"}


# -- odd input and the required agent ------------------------------------------------------------------------------


@pytest.mark.parametrize("agent", [None, ""])
def test_a_connect_without_an_agent_is_refused_whatever_server_runs(
    guard: Guard, registry: Registry, agent: str | None
) -> None:
    # The server may be one that does not require `agent` (a local-scope entry, an older checkout): two agents that
    # omit it would share the `default` connection, so the hook asks for it on the call that picks the copy.
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    ports = {"debug_port": 21513, "pine_port": 28012, "mode": "debug"}
    given = ports if agent is None else {**ports, "agent": agent}
    reason = guard.decide("s1", "agent-1", CONNECT, given)
    assert reason is not None and "`agent`" in reason and 'agent: "<your id>"' in reason
    assert guard.decide("s1", "agent-1", CONNECT, {**ports, "agent": "alice"}) is None


@pytest.mark.parametrize(
    "value", ["²", "٢١٥١٣", [21513], {"port": 21513}, True, -21513, "21513.5", 21513.5, float("nan"), float("inf")]
)
def test_a_port_that_is_no_port_is_refused_without_a_crash(guard: Guard, registry: Registry, value: object) -> None:
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    for key, other in (("debug_port", {"pine_port": 28012}), ("pine_port", {"debug_port": 21513})):
        reason = guard.decide("s1", "agent-1", CONNECT, {**other, key: value, "agent": "alice"})
        assert reason is not None and key in reason and "Blocked" in reason


def test_a_whole_number_sent_as_a_float_or_a_string_is_a_port(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    for debug, pine in ((21513.0, 28012.0), ("21513", "28012")):
        assert connect(guard, agent="alice", debug_port=debug, pine_port=pine) is None


@pytest.mark.parametrize("agent", [["alice"], {"alice": 1}, 5, True, 1.5])
@pytest.mark.parametrize("tool", ["pcsx2_connect", "pcsx2_write_memory", "pcsx2_status"])
def test_an_agent_that_is_no_string_is_refused_without_a_crash(
    guard: Guard, registry: Registry, tool: str, agent: object
) -> None:
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    ports = {"debug_port": 21513, "pine_port": 28012} if tool == "pcsx2_connect" else {}
    reason = guard.decide("s1", "agent-1", f"mcp__pcsx2__{tool}", {**ports, "agent": agent})
    assert reason is not None and "`agent`" in reason and "string" in reason


# -- the bindings: renewed on use, written without loss ---------------------------------------------------------------


def age(path: Path, seconds: float) -> float:
    """Set the modification time of `path` to `seconds` ago; returns that time."""
    then = path.stat().st_mtime - seconds
    os.utime(path, (then, then))
    return then


def test_a_binding_is_renewed_when_a_call_uses_it(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    (binding,) = guard.folder.glob("s1.bind.agent-1.*")
    nearly_expired = age(binding, BINDING_KEEP_SECONDS - 60)
    assert guard.decide("s1", "agent-1", "mcp__pcsx2__pcsx2_write_memory", {"agent": "alice"}) is None
    assert binding.stat().st_mtime > nearly_expired + BINDING_KEEP_SECONDS / 2
    # So a coordinator that only ever calls tools still goes by alice when the next bind prunes.
    age(binding, BINDING_KEEP_SECONDS / 2)
    guard.bind("s2", "agent-9", ["bob"])
    assert guard.bound("s1", "agent-1") == {"alice"}


def test_a_refused_call_does_not_renew_the_binding(guard: Guard) -> None:
    guard.bind("s1", "agent-1", ["alice"])
    (binding,) = guard.folder.glob("s1.bind.agent-1.*")
    long_ago = age(binding, 3600)
    assert guard.decide("s1", "agent-1", "mcp__pcsx2__pcsx2_write_memory", {"agent": "bob"}) is not None
    assert binding.stat().st_mtime == pytest.approx(long_ago, abs=1)


def test_temporary_files_a_crash_left_are_pruned(guard: Guard) -> None:
    guard.bind("s1", "agent-1", ["alice"])
    orphan = guard.folder / "s1.bind.agent-1.alice-12345678.json.4242.abcdef"
    orphan.write_text("{", encoding="utf-8")
    fresh = guard.folder / "s1.bind.agent-1.bob-12345678.json.4343.abcdef"  # another process may be writing it
    fresh.write_text("{", encoding="utf-8")
    age(orphan, 7200)
    guard.bind("s1", "agent-1", ["carol"])
    assert not orphan.exists() and fresh.exists()
    assert guard.bound("s1", "agent-1") == {"alice", "carol"}


def test_a_replace_that_fails_while_a_reader_has_the_file_open_is_retried(
    guard: Guard, monkeypatch: pytest.MonkeyPatch
) -> None:
    real = Path.replace
    failures = [PermissionError(13, "in use"), PermissionError(13, "in use")]

    def replace(self: Path, target: Any) -> Path:
        if failures:
            raise failures.pop()
        return real(self, target)

    monkeypatch.setattr(Path, "replace", replace)
    monkeypatch.setattr(time, "sleep", lambda seconds: None)
    guard.bind("s1", "agent-1", ["alice"])
    assert guard.bound("s1", "agent-1") == {"alice"} and not failures
    assert not [path for path in guard.folder.iterdir() if ".json." in path.name]  # no temporary file left


def test_two_processes_binding_different_ids_at_once_lose_neither(
    guard: Guard, registry: Registry, monkeypatch: pytest.MonkeyPatch
) -> None:
    rival = Guard(registry, guard.folder)
    real = Path.replace
    rivalled: list[bool] = []

    def replace(self: Path, target: Any) -> Path:
        if not rivalled:  # the other hook process binds its id while this one is writing its own
            rivalled.append(True)
            rival.bind("s1", "agent-1", ["bob"])
        return real(self, target)

    monkeypatch.setattr(Path, "replace", replace)
    guard.bind("s1", "agent-1", ["alice"])
    assert guard.bound("s1", "agent-1") == {"alice", "bob"}


def test_a_single_huge_log_line_is_kept(registry: Registry, monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda start=None: registry)
    monkeypatch.setattr(pcsx2_guard, "LOG_MAX_BYTES", 400)
    pcsx2_guard.log_failure(tmp_path, RuntimeError("x" * 5000))
    text = (registry.claims_dir / "mcp" / pcsx2_guard.LOG_FILE).read_text(encoding="utf-8")
    assert "RuntimeError: xxx" in text and len(text) < 1200


def test_the_failure_log_does_not_grow_without_bound(
    registry: Registry, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda start=None: registry)
    monkeypatch.setattr(pcsx2_guard, "LOG_MAX_BYTES", 400)
    for number in range(40):
        pcsx2_guard.log_failure(tmp_path, RuntimeError(f"failure {number}"))
    log = registry.claims_dir / "mcp" / pcsx2_guard.LOG_FILE
    lines = log.read_text(encoding="utf-8").splitlines()
    assert log.stat().st_size <= 400 + 100 and lines[-1].endswith("RuntimeError: failure 39")


# -- the hook's payloads ---------------------------------------------------------------------------------------------


def test_a_shell_command_binds_and_an_mcp_call_is_decided(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    shell = {
        "session_id": "s1",
        "agent_id": "agent-1",
        "tool_name": "Bash",
        "tool_input": {"command": "uv run --project python coney-tools pcsx2 claim --agent alice"},
    }
    assert pcsx2_guard.handle(shell, guard) is None
    call = {
        "session_id": "s1",
        "agent_id": "agent-1",
        "tool_name": CONNECT,
        "tool_input": {"debug_port": 21513, "pine_port": 28012, "mode": "debug", "agent": "alice"},
    }
    assert pcsx2_guard.handle(call, guard) is None
    other = {**call, "agent_id": "agent-2"}
    assert pcsx2_guard.handle(other, guard) is not None


def test_a_payload_without_a_session_is_let_through(guard: Guard) -> None:
    call = {"tool_name": CONNECT, "tool_input": {"debug_port": 21513, "pine_port": 28012}}
    assert pcsx2_guard.handle(call, guard) is None
    assert pcsx2_guard.handle({"tool_name": CONNECT, "tool_input": "not a dict", "session_id": "s1"}, guard) is None


def test_a_session_id_with_odd_characters_stays_inside_the_folder(guard: Guard, tmp_path: Path) -> None:
    guard.bind("../../evil", "a/b", ["alice"])
    assert guard.bound("../../evil", "a/b") == {"alice"}
    assert not (tmp_path / "evil").exists()
    assert all(path.resolve().is_relative_to(guard.folder.resolve()) for path in guard.folder.rglob("*"))


# -- the hook script -----------------------------------------------------------------------------------------------

SCRIPT = REPO / ".claude" / "hooks" / "pcsx2_guard.py"


def load_script() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("pcsx2_guard_hook", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_script(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], payload: object) -> tuple[int, str]:
    """Run the hook script on a payload; returns its exit code and stderr."""
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(json.dumps(payload).encode("utf-8"))))
    code = load_script().main()
    return code, capsys.readouterr().err


def test_the_script_blocks_a_refused_call_with_the_reason_on_stderr(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    registry: Registry,
    guard: Guard,
    tmp_path: Path,
) -> None:
    registry.claim("coop-1", "pcsx2-b")
    monkeypatch.setattr(pcsx2_guard, "default_guard", lambda start: guard)
    guard.bind("s1", "agent-1", ["recipes-1"])
    call = {"session_id": "s1", "agent_id": "agent-1", "tool_name": CONNECT, "cwd": str(tmp_path)}
    code, err = run_script(
        monkeypatch,
        capsys,
        {**call, "tool_input": {"debug_port": 21513, "pine_port": 28012, "agent": "recipes-1"}},
    )
    assert code == 2 and "coop-1" in err


def test_the_script_lets_through_what_it_cannot_read(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(b"not json")))
    assert load_script().main() == 0

    def broken(start: Path) -> Guard:
        raise RuntimeError("boom")

    monkeypatch.setattr(pcsx2_guard, "default_guard", broken)
    logged: list[BaseException] = []

    def log_failure(start: Path, error: BaseException) -> bool:
        logged.append(error)
        return True

    monkeypatch.setattr(pcsx2_guard, "log_failure", log_failure)
    code, err = run_script(monkeypatch, capsys, {"session_id": "s1", "tool_name": CONNECT, "tool_input": {}})
    assert code == 0 and err == "" and [str(error) for error in logged] == ["boom"]


def test_a_failure_of_the_hook_leaves_one_line_in_the_log(
    registry: Registry, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda start=None: registry)
    pcsx2_guard.log_failure(tmp_path, RuntimeError("boom"))
    pcsx2_guard.log_failure(tmp_path, ValueError("again"))
    lines = (registry.claims_dir / "mcp" / pcsx2_guard.LOG_FILE).read_text(encoding="utf-8").splitlines()
    assert [line.split(" ", 1)[1] for line in lines] == ["RuntimeError: boom", "ValueError: again"]


def test_the_hook_is_registered_for_the_mcp_tools_and_the_shell() -> None:
    settings = json.loads((REPO / ".claude" / "settings.json").read_text(encoding="utf-8"))
    registered = [
        (group["matcher"], hook["command"])
        for group in settings["hooks"]["PreToolUse"]
        for hook in group["hooks"]
        if "pcsx2_guard.py" in hook["command"]
    ]
    matchers = {matcher for matcher, _ in registered}
    assert "mcp__pcsx2__.*" in matchers, "the pcsx2 MCP tools are not guarded"
    assert {"Bash", "PowerShell"} <= matchers, "the shell commands that claim a copy are not read"


def test_the_script_logs_what_it_cannot_read(
    monkeypatch: pytest.MonkeyPatch, registry: Registry, tmp_path: Path
) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda start=None: registry)
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(tmp_path))
    for text in (b"not json", b"[1, 2]"):
        monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(text)))
        assert load_script().main() == 0
    lines = (registry.claims_dir / "mcp" / pcsx2_guard.LOG_FILE).read_text(encoding="utf-8").splitlines()
    assert len(lines) == 2 and all("payload" in line for line in lines)


def test_the_script_logs_when_the_package_cannot_load(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    monkeypatch.setitem(sys.modules, "coney_tools.pcsx2_guard", None)  # an import of it now raises ImportError
    monkeypatch.delattr("coney_tools.pcsx2_guard", raising=False)
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    call = {"session_id": "s1", "tool_name": CONNECT, "tool_input": {}, "cwd": str(tmp_path)}
    code, err = run_script(monkeypatch, capsys, call)
    assert code == 0 and err == ""
    text = (tmp_path / "coney-pcsx2-guard.log").read_text(encoding="utf-8")
    assert "ModuleNotFoundError" in text and "pcsx2_guard" in text


@pytest.mark.parametrize("cwd", [5, ["x"], None])
def test_the_script_copes_with_a_working_directory_that_is_no_path(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    registry: Registry,
    guard: Guard,
    cwd: object,
) -> None:
    registry.claim("coop-1", "pcsx2-b")
    monkeypatch.delenv("CLAUDE_PROJECT_DIR", raising=False)
    monkeypatch.setattr(pcsx2_guard, "default_guard", lambda start: guard)
    guard.bind("s1", "agent-1", ["recipes-1"])
    call = {"session_id": "s1", "agent_id": "agent-1", "tool_name": CONNECT, "cwd": cwd}
    code, err = run_script(
        monkeypatch, capsys, {**call, "tool_input": {"debug_port": 21513, "pine_port": 28012, "agent": "recipes-1"}}
    )
    assert code == 2 and "coop-1" in err


def test_the_guard_needs_nothing_but_the_standard_library() -> None:
    # The hook runs with `uv run --no-project`: nothing the guard imports may need a third-party package.
    package = REPO / "python" / "src"
    code = (
        "import sys; sys.path.insert(0, sys.argv[1]); import coney_tools.pcsx2_guard; "
        "print(sorted({'yaml', 'PIL', 'numpy', 'pytest'} & set(sys.modules)))"
    )
    done = subprocess.run(
        [sys.executable, "-I", "-c", code, str(package)], capture_output=True, text=True, check=True, timeout=60
    )
    assert done.stdout.strip() == "[]"


def filter_pattern(settings: dict[str, Any], matcher: str) -> re.Pattern[str]:
    """The `if` filter of the guard's registration for `matcher`, as a regular expression (`*` is any text, newlines
    included). This models Claude Code's rule matching, which the test cannot run; if the real matcher stops at a
    newline, the continued-line spellings are skipped and the caller is told to claim again (it fails closed)."""
    (condition,) = {
        hook["if"]
        for group in settings["hooks"]["PreToolUse"]
        if group["matcher"] == matcher
        for hook in group["hooks"]
        if "pcsx2_guard.py" in hook["command"]
    }
    inner = condition.removeprefix(f"{matcher}(").removesuffix(")")
    return re.compile(".*".join(re.escape(part) for part in inner.split("*")), re.DOTALL)


@pytest.mark.parametrize(
    "command",
    [
        "coney-tools pcsx2 claim --agent alice",
        "uv run --project python coney-tools pcsx2 claim --agent alice",
        ".venv/Scripts/coney-tools.exe pcsx2 claim --agent alice",
        '"coney-tools" pcsx2 claim --agent alice',
        "coney-tools  pcsx2 claim --agent alice",
        "coney-tools\tpcsx2 claim --agent alice",
        "coney-tools \\\n  pcsx2 claim --agent alice",
        "coney-tools pcsx2 \\\n  claim --agent alice",
    ],
)
def test_the_if_filter_lets_through_every_spelling_the_parser_reads(command: str) -> None:
    assert pcsx2_guard.agents_in(command) == ["alice"], "not a spelling the parser reads: fix this test"
    settings = json.loads((REPO / ".claude" / "settings.json").read_text(encoding="utf-8"))
    for matcher in ("Bash", "PowerShell"):
        assert filter_pattern(settings, matcher).search(command), f"the {matcher} `if` filter skips {command!r}"


def test_the_guides_tell_an_engineer_to_pass_agent_on_every_call() -> None:
    wanted = re.compile(r"agent: \"<(?:your|their) id>\"")
    setup = (REPO / "docs" / "guides" / "agents.md").read_text(encoding="utf-8")
    section = setup.split("**The PCSX2 server and your copy.**", 1)[1].split("**The roles.**", 1)[0]
    assert wanted.search(section) and "every" in section, "agents.md#setup does not say to pass `agent` on every call"
    research = (REPO / "docs" / "guides" / "research-workflow.md").read_text(encoding="utf-8")
    driving = research.split("What the debug server gives", 1)[1].split("input-recordings", 1)[0]
    assert wanted.search(driving), "research-workflow.md#driving-pcsx2 does not say to pass `agent`"
