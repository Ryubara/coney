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
import socket
import threading
from collections.abc import Iterator
from pathlib import Path
from types import ModuleType

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
    assert connect(guard, debug_port=21513, pine_port=28012) is None


def test_a_connect_by_someone_who_never_claimed_through_a_shell_says_how(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    reason = connect(guard, debug_port=21513, pine_port=28012)
    assert reason is not None and "coney-tools pcsx2 claim --agent" in reason


def test_a_connect_to_the_ports_of_another_agents_claim_is_refused(guard: Guard, registry: Registry) -> None:
    registry.claim("coop-1", "pcsx2-b")
    guard.bind("s1", "agent-1", ["recipes-1"])
    reason = connect(guard, debug_port=21513, pine_port=28012)
    assert reason is not None and "held by coop-1, not recipes-1" in reason


def test_a_connect_to_a_copy_nobody_claimed_is_refused(guard: Guard) -> None:
    guard.bind("s1", "agent-1", ["alice"])
    reason = connect(guard, debug_port=21514, pine_port=28013)
    assert reason is not None and "not claimed" in reason


@pytest.mark.parametrize("mode", ["auto", "debug", "pine"])
def test_both_ports_are_needed_whatever_the_mode(guard: Guard, registry: Registry, mode: str) -> None:
    registry.claim("alice", "pcsx2")
    guard.bind("s1", "agent-1", ["alice"])
    both = {"debug_port": 21512, "pine_port": 28011, "mode": mode}
    assert guard.decide("s1", "agent-1", CONNECT, both) is None
    for missing in ("debug_port", "pine_port"):
        given = {key: value for key, value in both.items() if key != missing}
        reason = guard.decide("s1", "agent-1", CONNECT, given)
        assert reason is not None and missing in reason and "pcsx2_status" in reason
    reason = guard.decide("s1", "agent-1", CONNECT, {})
    assert reason is not None and "debug_port" in reason


def test_ports_of_two_copies_in_one_connect_are_refused(guard: Guard, registry: Registry) -> None:
    registry.claim("alice", "pcsx2")
    registry.claim("alice", "pcsx2-b")
    guard.bind("s1", "agent-1", ["alice"])
    reason = guard.decide("s1", "agent-1", CONNECT, {"debug_port": 21512, "pine_port": 28012})
    assert reason is not None and "two different copies" in reason


def test_nothing_is_locked_for_the_other_agents_of_a_session(guard: Guard, registry: Registry) -> None:
    registry.claim("fixer-4", "pcsx2")
    registry.claim("recipes-1", "pcsx2-b")
    guard.bind("s1", "agent-fixer", ["fixer-4"])
    guard.bind("s1", "agent-recipes", ["recipes-1"])
    assert connect(guard, "agent-fixer", debug_port=21512, pine_port=28011) is None
    assert connect(guard, "agent-recipes", debug_port=21513, pine_port=28012) is None
    assert connect(guard, "agent-fixer", debug_port=21512, pine_port=28011) is None
    for tool in ("pcsx2_write_memory", "pcsx2_pause", "pcsx2_read_memory"):
        assert guard.decide("s1", "agent-recipes", f"mcp__pcsx2__{tool}", {}) is None


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
    old = guard.folder / "old.bind.agent-1.json"
    long_ago = old.stat().st_mtime - BINDING_KEEP_SECONDS - 60
    os.utime(old, (long_ago, long_ago))
    guard.bind("new", "agent-1", ["carol"])
    assert not old.exists()
    assert guard.bound("kept", "agent-1") == {"bob"} and guard.bound("new", "agent-1") == {"carol"}
    # A caller that claims again renews its binding.
    os.utime(guard.folder / "kept.bind.agent-1.json", (long_ago, long_ago))
    guard.bind("kept", "agent-1", ["bob"])
    guard.bind("other", "agent-1", ["dave"])
    assert guard.bound("kept", "agent-1") == {"bob"}


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
        "tool_input": {"debug_port": 21513, "pine_port": 28012, "mode": "debug"},
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
    code, err = run_script(monkeypatch, capsys, {**call, "tool_input": {"debug_port": 21513, "pine_port": 28012}})
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
    monkeypatch.setattr(pcsx2_guard, "log_failure", lambda start, error: logged.append(error))
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
    assert "mcp__pcsx2__pcsx2_connect" in matchers, "pcsx2_connect is not guarded"
    assert {"Bash", "PowerShell"} <= matchers, "the shell commands that claim a copy are not read"
