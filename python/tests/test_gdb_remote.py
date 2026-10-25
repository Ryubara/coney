# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.gdb_remote and coney_tools.qemu_monitor against the fake stub and monitor of xemu_support:
packets and checksums, acknowledgements, memory, registers, breakpoints, stopping and running."""

import socket

import pytest
from xemu_support import FakeGdbStub, FakeMonitor, eventually, fake_xemu

from coney_tools.gdb_remote import GdbClient, GdbError, decode_rle, encode_packet, parse_stop
from coney_tools.qemu_monitor import MonitorClient, MonitorError, strip_terminal


def test_a_packet_carries_its_checksum() -> None:
    assert encode_packet("m10000,4") == b"$m10000,4#be"
    assert encode_packet("") == b"$#00"


def test_run_length_encoding_is_expanded() -> None:
    # `0* ` is "0" and three more (" " is 32, 32 - 29 = 3).
    assert decode_rle("0* 1") == "00001"
    assert decode_rle("abc") == "abc"


def test_stop_replies_are_read() -> None:
    stop = parse_stop("T05thread:01;watch:0001f000;")
    assert stop.signal == 5 and stop.watch == ("watch", 0x0001F000)
    assert parse_stop("S02").signal == 2 and parse_stop("S02").watch is None
    assert parse_stop("W00").exited


@pytest.mark.parametrize("ack", [True, False])
def test_connecting_stops_the_guest_and_reads_memory(ack: bool) -> None:
    with fake_xemu(ack=ack) as (stub, _):
        stub.poke(0x00010010, bytes([1, 2, 3, 4]))
        with GdbClient(stub.port) as gdb:
            assert not gdb.running
            assert gdb.read_memory(0x00010010, 4) == bytes([1, 2, 3, 4])
        assert stub.no_ack is ack


def test_a_long_read_is_split_into_packets() -> None:
    with fake_xemu() as (stub, _):
        stub.poke(0x00010000, bytes(range(256)) * 12)
        with GdbClient(stub.port) as gdb:
            assert gdb.read_memory(0x00010000, 3072) == bytes(range(256)) * 12
        assert sum(1 for p in stub.packets if p.startswith("m")) == 3


def test_an_unreadable_address_is_an_error_naming_it() -> None:
    with fake_xemu() as (stub, _), GdbClient(stub.port) as gdb, pytest.raises(GdbError, match=r"0x00000004.*E14"):
        gdb.read_memory(4, 4)


def test_memory_is_written() -> None:
    with fake_xemu() as (stub, _):
        with GdbClient(stub.port) as gdb:
            gdb.write_memory(0x00010020, b"\xaa\xbb")
        assert stub.peek(0x00010020, 2) == b"\xaa\xbb"


def test_registers_are_the_i386_core_set() -> None:
    with fake_xemu() as (stub, _):
        stub.registers["eax"] = 0x12345678
        with GdbClient(stub.port) as gdb:
            registers = gdb.registers()
            assert registers["eax"] == 0x12345678 and registers["eip"] == 0x00011000
            assert list(registers)[:3] == ["eax", "ecx", "edx"]
            gdb.write_register("ebx", 7)
        assert stub.registers["ebx"] == 7


def test_an_unknown_register_is_refused() -> None:
    with fake_xemu() as (stub, _), GdbClient(stub.port) as gdb, pytest.raises(GdbError, match="rax"):
        gdb.write_register("rax", 1)


def test_breakpoints_and_watchpoints_are_set_and_removed() -> None:
    with fake_xemu() as (stub, _):
        with GdbClient(stub.port) as gdb:
            gdb.set_point("break", 0x00011000)
            gdb.set_point("write", 0x0001F000, 4)
            assert stub.breakpoints == {(0, 0x00011000, 1), (2, 0x0001F000, 4)}
            gdb.remove_point("break", 0x00011000)
        assert stub.breakpoints == {(2, 0x0001F000, 4)}
    with pytest.raises(GdbError, match="kind"):
        GdbClient.point_packet("Z", "jump", 0, 1)


def test_continue_then_a_hit_is_seen_as_a_stop() -> None:
    with fake_xemu() as (stub, _), GdbClient(stub.port) as gdb:
        gdb.cont()
        assert gdb.running and eventually(lambda: stub.running)
        assert gdb.poll_stop() is None
        stub.hit("T05thread:01;watch:0001f000;")
        stop = gdb.wait_stop(2.0)
        assert stop is not None and stop.watch == ("watch", 0x0001F000) and not gdb.running


def test_pausing_a_running_guest_interrupts_it() -> None:
    with fake_xemu() as (stub, _), GdbClient(stub.port) as gdb:
        gdb.cont()
        stop = gdb.interrupt()
        assert stop.signal == 2 and not stub.running and not gdb.running


def test_a_read_while_running_pauses_and_resumes() -> None:
    with fake_xemu() as (stub, _):
        stub.poke(0x00010000, b"\x05")
        with GdbClient(stub.port) as gdb:
            gdb.cont()
            assert gdb.read_memory(0x00010000, 1, resume=True) == b"\x05"
            assert gdb.running and eventually(lambda: stub.running)


def test_a_step_moves_one_instruction() -> None:
    with fake_xemu() as (stub, _), GdbClient(stub.port) as gdb:
        assert gdb.step().signal == 5
        assert stub.registers["eip"] == 0x00011001


def test_a_refused_connection_is_an_error_naming_the_port() -> None:
    with socket.create_server(("127.0.0.1", 0)) as server:
        port = server.getsockname()[1]
    with pytest.raises(GdbError, match=str(port)):
        GdbClient(port, timeout=0.5).connect()


# --- the monitor ------------------------------------------------------------------------------------------------------


def test_terminal_escapes_are_removed() -> None:
    assert strip_terminal("info status\x1b[K\r\nVM status: paused\r\n") == "info status\nVM status: paused\n"


def test_the_monitor_answers_without_its_echo() -> None:
    monitor = FakeMonitor()
    try:
        with MonitorClient(monitor.port) as client:
            assert client.command("info status") == "VM status: running"
            assert client.running() is True
            client.command("stop")
            assert client.running() is False
            assert client.version() == "8.2.50 (xemu-v0.8.110)"
    finally:
        monitor.close()


def test_a_monitor_error_is_raised() -> None:
    monitor = FakeMonitor()
    try:
        with MonitorClient(monitor.port) as client, pytest.raises(MonitorError, match="does not exist"):
            client.checked("loadvm nothing")
    finally:
        monitor.close()


def test_a_command_with_a_newline_is_refused() -> None:
    monitor = FakeMonitor()
    try:
        with MonitorClient(monitor.port) as client, pytest.raises(MonitorError, match="one line"):
            client.command("stop\ncont")
    finally:
        monitor.close()


def test_snapshots_are_listed_by_tag() -> None:
    stub = FakeGdbStub()
    monitor = FakeMonitor(stub)
    try:
        with MonitorClient(monitor.port) as client:
            assert client.snapshots() == []
            client.checked("savevm shadow-edge")
            assert client.snapshots() == ["shadow-edge"]
    finally:
        monitor.close()
        stub.close()
