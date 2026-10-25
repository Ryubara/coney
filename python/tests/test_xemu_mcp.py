# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.xemu_mcp and coney_tools.xemu_debug: the MCP server over the fake GDB stub and monitor of
xemu_support. Each agent reaches only the copy it claimed; reads leave a running game running; the protocol answers
one JSON line per request."""

import io
import json
import struct
from collections.abc import Iterator
from pathlib import Path

import pytest
from xemu_support import FakeGdbStub, FakeMonitor, eventually, fake_xemu, make_copy

from coney_tools import xemu_claims
from coney_tools.config import ConfigError
from coney_tools.xemu_claims import Registry
from coney_tools.xemu_mcp import READ_ONLY_TOOLS, TOOLS, Server, parse_address, serve


@pytest.fixture
def rig(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Iterator[tuple[Server, FakeGdbStub, FakeMonitor, Registry]]:
    """A copy `xemu` served by a fake stub and monitor, claimed by `alice`; a second copy `xemu-b` with nothing
    running, claimed by `bob`."""
    monkeypatch.setattr(xemu_claims, "xemu_processes", dict)
    monkeypatch.setattr(xemu_claims, "is_xemu", lambda pid: False)
    with fake_xemu() as (stub, monitor):
        make_copy(tmp_path / "root", "xemu", stub.port, monitor.port)
        make_copy(tmp_path / "root", "xemu-b", 61001, 61002)
        registry = Registry(tmp_path / "root", tmp_path / "claims")
        # Claimed before "starting": a copy that serves its ports with no claim is someone's, and is not handed out.
        monkeypatch.setattr(xemu_claims, "port_open", lambda port: False)
        registry.claim("alice", "xemu")
        registry.claim("bob", "xemu-b")
        monkeypatch.setattr(xemu_claims, "port_open", lambda port: port in (stub.port, monitor.port))
        server = Server(lambda: registry, scratch=tmp_path / "scratch")
        yield server, stub, monitor, registry
        server.close()


def call(server: Server, tool: str, **arguments: object) -> tuple[str, bool]:
    return server.call(tool, dict(arguments))


def test_addresses_are_read_in_hex_or_decimal() -> None:
    assert parse_address("0x0032dd40") == 0x0032DD40 and parse_address(16) == 16
    with pytest.raises(ConfigError, match="32-bit"):
        parse_address(1 << 33)


def test_every_tool_needs_the_agent(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, *_ = rig
    text, failed = call(server, "xemu_status")
    assert failed and "agent" in text
    assert all("agent" in tool["inputSchema"]["required"] for tool in server.tools())  # type: ignore[index]


def test_an_agent_reaches_only_its_own_claim(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    text, failed = call(server, "xemu_status", agent="carol")
    assert failed and "holds no xemu copy" in text
    assert stub.connections == 0


def test_status_names_the_copy_and_its_version(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    stub.running = True
    text, failed = call(server, "xemu_status", agent="alice")
    status = json.loads(text)
    assert not failed and status["copy"] == "xemu" and status["running"] is True
    assert status["version"] == "8.2.50 (xemu-v0.8.110)"


def test_a_read_of_a_running_game_leaves_it_running(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    stub.running = True
    stub.poke(0x00010100, struct.pack("<f", 1.5))
    text, failed = call(server, "xemu_read_memory", agent="alice", address="0x00010100", type="f32")
    assert not failed and json.loads(text)["values"] == [1.5]
    assert eventually(lambda: stub.running)


def test_a_hex_read_gives_rows(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    stub.poke(0x00010000, bytes(range(20)))
    text, _ = call(server, "xemu_read_memory", agent="alice", address=0x00010000, count=20)
    assert text.splitlines()[1] == "0x00010010: 10 11 12 13"


def test_too_long_a_read_is_refused(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, *_ = rig
    text, failed = call(server, "xemu_read_memory", agent="alice", address=0x00010000, count=5000)
    assert failed and "count" in text


def test_writes_take_hex_or_a_typed_value(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    assert not call(server, "xemu_write_memory", agent="alice", address=0x00010000, hex="aabb")[1]
    assert not call(server, "xemu_write_memory", agent="alice", address=0x00010004, value=7, type="u32")[1]
    assert stub.peek(0x00010000, 8) == b"\xaa\xbb\x00\x00\x07\x00\x00\x00"


def test_pause_continue_and_a_breakpoint_hit(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    stub.running = True
    assert call(server, "xemu_pause", agent="alice")[0] == "paused" and not stub.running
    assert not call(server, "xemu_set_breakpoint", agent="alice", address="0x00011000")[1]
    assert (0, 0x00011000, 1) in stub.breakpoints
    assert call(server, "xemu_continue", agent="alice")[0] == "running" and eventually(lambda: stub.running)
    stub.hit()
    status = json.loads(call(server, "xemu_status", agent="alice")[0])
    assert status["running"] is False and status["eip"] == "0x00011000"


def test_a_register_write_needs_a_pause(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    stub.running = True
    text, failed = call(server, "xemu_write_register", agent="alice", name="eax", value=5)
    assert failed and "pause" in text
    call(server, "xemu_pause", agent="alice")
    assert not call(server, "xemu_write_register", agent="alice", name="eax", value=5)[1]
    assert json.loads(call(server, "xemu_read_registers", agent="alice")[0])["eax"] == "0x00000005"


def test_steps_move_eip(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, *_ = rig
    assert call(server, "xemu_step", agent="alice", count=3)[0].endswith("eip 0x00011003")


def test_a_screenshot_is_a_png_outside_the_repository(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], tmp_path: Path
) -> None:
    server, *_ = rig
    text, failed = call(server, "xemu_screenshot", agent="alice")
    assert not failed and "2 x 1 (screendump)" in text
    assert len(list((tmp_path / "scratch" / "xemu").glob("xemu-*.png"))) == 1
    text, failed = call(server, "xemu_screenshot", agent="alice", out=str(Path(__file__).parent / "shot.png"))
    assert failed and "inside the repository" in text


def test_snapshots_are_saved_listed_and_loaded(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, *_ = rig
    assert not call(server, "xemu_save_snapshot", agent="alice", name="shadow-edge")[1]
    assert json.loads(call(server, "xemu_list_snapshots", agent="alice")[0]) == ["shadow-edge"]
    assert not call(server, "xemu_load_snapshot", agent="alice", name="shadow-edge")[1]
    text, failed = call(server, "xemu_load_snapshot", agent="alice", name="missing")
    assert failed and "does not exist" in text
    text, failed = call(server, "xemu_save_snapshot", agent="alice", name="two words")
    assert failed and "snapshot" in text


def test_a_released_claim_closes_the_connection(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], monkeypatch: pytest.MonkeyPatch
) -> None:
    server, stub, _, registry = rig
    call(server, "xemu_status", agent="alice")
    assert "alice" in server.held
    registry.release("alice", "xemu")
    serving = xemu_claims.port_open  # type: ignore[attr-defined]
    monkeypatch.setattr(xemu_claims, "port_open", lambda port: False)  # dave claims it as if alice had closed xemu
    registry.claim("dave", "xemu")
    monkeypatch.setattr(xemu_claims, "port_open", serving)
    text, failed = call(server, "xemu_status", agent="alice")
    assert failed and "holds no xemu copy" in text
    assert not call(server, "xemu_status", agent="dave")[1]
    assert "alice" not in server.held and stub.connections == 2


def test_disconnect_frees_the_stub(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, *_ = rig
    call(server, "xemu_status", agent="alice")
    assert call(server, "xemu_disconnect", agent="alice") == ("closed alice's connection", False)
    assert server.held == {}


def test_the_read_only_tools_are_tools_that_change_nothing() -> None:
    names = {tool.name for tool in TOOLS}
    assert names >= READ_ONLY_TOOLS
    assert not {n for n in READ_ONLY_TOOLS if any(w in n for w in ("write", "set", "load", "save", "pause", "step"))}


def test_the_protocol_answers_one_line_per_request(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, *_ = rig
    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-03-26"}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {"name": "xemu_status", "arguments": {"agent": "alice"}},
        },
        {"jsonrpc": "2.0", "id": 4, "method": "nothing"},
    ]
    stdin = io.StringIO("".join(json.dumps(r) + "\n" for r in requests) + "not json\n")
    stdout = io.StringIO()
    serve(stdin, stdout, server)
    answers = [json.loads(line) for line in stdout.getvalue().splitlines()]
    assert [a["id"] for a in answers] == [1, 2, 3, 4, None]
    assert answers[0]["result"]["protocolVersion"] == "2025-03-26"
    assert {t["name"] for t in answers[1]["result"]["tools"]} >= {"xemu_status", "xemu_disconnect"}
    assert answers[2]["result"]["isError"] is False
    assert answers[3]["error"]["code"] == -32601 and answers[4]["error"]["code"] == -32700
