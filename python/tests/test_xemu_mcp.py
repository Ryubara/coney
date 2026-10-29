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
from PIL import Image
from xemu_support import FakeGdbStub, FakeMonitor, eventually, fake_xemu, make_copy

from coney_tools import xemu_claims, xemu_debug
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
    assert not failed, text
    status = json.loads(text)
    assert status["copy"] == "xemu" and status["running"] is True
    assert status["version"] == "8.2.50 (xemu-v0.8.110)"


def test_a_monitor_a_moment_behind_the_stub_is_not_believed(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], monkeypatch: pytest.MonkeyPatch
) -> None:
    # The monitor and the stub are two sockets: right after the resume the monitor can still say "paused" while the
    # stub has taken the `c` (seen once in a full run). One stale answer must not turn a running game into a paused one.
    server, stub, monitor, _ = rig
    stub.running = True
    real, calls = monitor._running, []

    def lagging() -> bool:
        # The first ask comes before the attach (truthfully); the second, right after the resume, is the stale one.
        calls.append(1)
        return False if len(calls) == 2 else real()

    monkeypatch.setattr(monitor, "_running", lagging)
    text, failed = call(server, "xemu_status", agent="alice")
    assert not failed, text
    assert json.loads(text)["running"] is True and len(calls) >= 2


def test_status_names_xemus_own_release_from_the_copys_log(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry],
) -> None:
    # A real xemu's `info version` is QEMU's ("10.2.0"); its own release is the first lines of its log.
    server, _, monitor, registry = rig
    monitor.version = "10.2.0"
    (registry.copy("xemu").path / "xemu.log").write_text("xemu_version: 0.8.139\nxemu_commit: abc\n", encoding="utf-8")
    text, failed = call(server, "xemu_status", agent="alice")
    assert not failed and json.loads(text)["version"] == "xemu v0.8.139 (QEMU 10.2.0)"


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


def test_closing_a_session_removes_its_breakpoints(rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry]) -> None:
    server, stub, *_ = rig
    assert not call(server, "xemu_set_breakpoint", agent="alice", address="0x00011000")[1]
    assert stub.breakpoints
    server.close()
    assert stub.breakpoints == set()  # the copy's next holder does not inherit an armed breakpoint


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


def test_a_screenshot_falls_back_to_the_guests_front_buffer(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    # xemu has no `screendump` and its window may be black; the picture is then read from the guest's RAM: the scan-out
    # start register holds an offset, the pixels follow at the kernel's mapping of RAM (the fake maps both low).
    server, stub, monitor, _ = rig
    monitor.screendump = False
    monkeypatch.setattr(xemu_debug, "NV_PCRTC_START", 0x00010000)
    monkeypatch.setattr(xemu_debug, "KERNEL_RAM", 0x00010000)
    stub.poke(0x00010000, (0x100).to_bytes(4, "little"))
    stub.poke(0x00010100, bytes([1, 2, 3, 255, 40, 50, 60, 255]))  # two BGRA pixels
    stub.running = True
    monitor.paused = False
    out = tmp_path / "scratch" / "fb.png"
    text, failed = call(server, "xemu_screenshot", agent="alice", out=str(out), width=2, height=1)
    assert not failed and "2 x 1 (framebuffer)" in text
    with Image.open(out) as image:
        rgb = image.convert("RGB")
        assert [rgb.getpixel((0, 0)), rgb.getpixel((1, 0))] == [(3, 2, 1), (60, 50, 40)]
    assert stub.running  # the guest ran on afterwards
    text, failed = call(server, "xemu_screenshot", agent="alice", out=str(out), width=5000)
    assert failed and "width" in text


def test_a_screenshot_names_every_way_it_failed(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    server, _, monitor, _ = rig
    monitor.screendump = False
    monkeypatch.setattr(xemu_debug, "RAM_SIZE", 16)  # the fake's scan-out start register is outside the fake's memory
    text, failed = call(server, "xemu_screenshot", agent="alice", out=str(tmp_path / "scratch" / "x.png"))
    assert failed and "screendump: " in text and "window: " in text and "framebuffer: " in text


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


@pytest.mark.parametrize(
    ("tool", "arguments"),
    [
        ("xemu_read_memory", {"address": 0x10000, "type": ["u32"]}),
        ("xemu_read_memory", {"address": 0x10000, "type": {"a": 1}}),
        ("xemu_read_memory", {"address": ["0x10000"]}),
        ("xemu_read_memory", {"address": 0x10000, "count": "4"}),
        ("xemu_write_memory", {"address": 0x10000, "type": ["u32"], "value": 1}),
        ("xemu_write_memory", {"address": 0x10000, "type": "u32", "value": [1]}),
        ("xemu_write_memory", {"address": 0x10000, "hex": ["00"]}),
        ("xemu_write_memory", {"address": 0x10000, "hex": 5}),
        ("xemu_write_register", {"name": ["eax"], "value": 1}),
        ("xemu_write_register", {"name": "eax", "value": {"x": 1}}),
        ("xemu_set_breakpoint", {"address": 0x10000, "kind": ["break"]}),
        ("xemu_set_breakpoint", {"address": 0x10000, "length": [1]}),
        ("xemu_continue", {"wait_seconds": "soon"}),
        ("xemu_step", {"count": [1]}),
        ("xemu_screenshot", {"out": 5}),
        ("xemu_screenshot", {"out": ["a.png"]}),
        ("xemu_screenshot", {"width": "wide"}),
        ("xemu_save_snapshot", {"name": ["a"]}),
        ("xemu_save_snapshot", {}),
        ("xemu_load_snapshot", {"name": {"a": 1}}),
    ],
)
def test_a_wrongly_typed_argument_is_the_tools_answer_not_a_crash(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], tool: str, arguments: dict[str, object]
) -> None:
    server, *_ = rig
    text, failed = server.call(tool, {"agent": "alice", **arguments})
    assert failed and text.startswith(tool)


def test_a_tool_that_breaks_unexpectedly_is_still_an_answer(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], monkeypatch: pytest.MonkeyPatch
) -> None:
    server, *_ = rig

    def broken(*args: object) -> object:
        raise RuntimeError("a bug in a handler")

    monkeypatch.setattr(xemu_debug.XemuSession, "registers", broken)
    text, failed = call(server, "xemu_read_registers", agent="alice")
    assert failed and "RuntimeError" in text and "a bug in a handler" in text
    # the connection was dropped, so the next call starts clean
    assert "alice" not in server.held


def test_a_screenshot_stays_in_the_shared_scratch(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], tmp_path: Path
) -> None:
    # The reviewer holds this tool as a read-only one: it must not write wherever it is told.
    server, *_ = rig
    text, failed = call(server, "xemu_screenshot", agent="alice", out=str(tmp_path / "elsewhere" / "x.png"))
    assert failed and "scratch" in text
    assert not (tmp_path / "elsewhere").exists()
    text, failed = call(server, "xemu_screenshot", agent="alice", out=str(tmp_path / "scratch" / ".." / "x.png"))
    assert failed and "scratch" in text
    text, failed = call(server, "xemu_screenshot", agent="alice", out=str(tmp_path / "scratch" / "mine" / "x.png"))
    assert not failed, text
    no_scratch = Server(server.registry, server.session_factory)
    text, failed = no_scratch.call("xemu_screenshot", {"agent": "alice", "out": str(tmp_path / "x.png")})
    assert failed and "scratch" in text


def test_the_protocol_survives_messages_of_the_wrong_shape(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], monkeypatch: pytest.MonkeyPatch
) -> None:
    server, *_ = rig
    requests: list[object] = [
        {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": ["xemu_status"]},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": "xemu_status"},
        {"jsonrpc": "2.0", "id": 3, "method": "initialize", "params": [1]},
        {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": ["x"], "arguments": [1]}},
        {"jsonrpc": "2.0", "id": 5, "method": ["tools/list"]},
        {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "tools/call",
            "params": {"name": "xemu_status", "arguments": {"agent": "alice"}},
        },
    ]
    stdin = io.StringIO("".join(json.dumps(r) + "\n" for r in requests))
    stdout = io.StringIO()
    assert serve(stdin, stdout, server) == 0
    answers = {a["id"]: a for a in map(json.loads, stdout.getvalue().splitlines())}
    assert set(answers) == {1, 2, 3, 4, 5, 6}
    assert answers[1]["error"]["code"] == -32602 and answers[2]["error"]["code"] == -32602
    assert answers[3]["error"]["code"] == -32602
    assert answers[4]["result"]["isError"] is True
    assert answers[5]["error"]["code"] == -32601
    assert answers[6]["result"]["isError"] is False


def test_a_bug_in_handling_a_message_does_not_stop_the_server(
    rig: tuple[Server, FakeGdbStub, FakeMonitor, Registry], monkeypatch: pytest.MonkeyPatch
) -> None:
    server, *_ = rig
    real = server.handle

    def handle(message: dict[str, object]) -> dict[str, object] | None:
        if message.get("id") == 1:
            raise RuntimeError("boom")
        return real(message)

    monkeypatch.setattr(server, "handle", handle)
    stdin = io.StringIO('{"jsonrpc":"2.0","id":1,"method":"ping"}\n{"jsonrpc":"2.0","id":2,"method":"ping"}\n')
    stdout = io.StringIO()
    assert serve(stdin, stdout, server) == 0
    answers = [json.loads(line) for line in stdout.getvalue().splitlines()]
    assert answers[0]["id"] == 1 and answers[0]["error"]["code"] == -32603
    assert answers[1] == {"jsonrpc": "2.0", "id": 2, "result": {}}
