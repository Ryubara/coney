# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.light_trace (`trace lights`): the original's light selection traced through the PCSX2-MCP
fork's debug server, against a fake debug server that plays a made-up game, and the run record it writes. Every
address other than the selection's, its callers' and its lists' (docs/research/lighting.md#select), every light and
every number is made up; no PCSX2 and no game data."""

from __future__ import annotations

import hashlib
import json
import socket
import struct
import threading
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import pytest
import test_run_records_cli as record_tests
from pine_support import fake_pcsx2

from coney_tools import light_trace, light_trace_cli, run_records, run_records_cli
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.debug_server import DebugClient, DebugServerError
from coney_tools.light_trace import Light, LightTraceError

#: The made-up checkout of the run-record tests, a fixture.
checkout = record_tests.checkout

# The selection and its callers' return addresses: docs/research/lighting.md#select.
ENTRY = 0x0017DE10
OBJECT_RETURN = 0x001803A4
HUMAN_RETURN = 0x001746F8
SECTOR_RETURN = 0x00411AA8

MANAGER = 0x00800000
SPHERE = 0x00830000
HUMAN = 0x00840000
OBJECT_POINTS = 0x00801000
WORLD_POINTS = 0x00802000


def _f32(value: float) -> bytes:
    return struct.pack("<f", value)


def _u32(value: int) -> bytes:
    return struct.pack("<I", value)


@dataclass
class MadeUpLight:
    """A light of the made-up game: an RpLight at `address`, its frame at `address + 0x1000`."""

    address: int
    type: int
    radius: float
    colour: tuple[float, float, float, float]
    position: tuple[float, float, float]

    def write(self, memory: dict[int, int]) -> None:
        frame = self.address + 0x1000
        rp = bytearray(0x28)
        rp[1] = self.type
        rp[4:8] = _u32(frame)
        rp[0x14:0x18] = _f32(self.radius)
        rp[0x18:0x28] = b"".join(_f32(c) for c in self.colour)
        _poke(memory, self.address, bytes(rp))
        _poke(memory, frame + 0x40, b"".join(_f32(c) for c in self.position))


def _poke(memory: dict[int, int], address: int, data: bytes) -> None:
    for i, byte in enumerate(data):
        memory[address + i] = byte


def _list(memory: dict[int, int], address: int, lights: list[MadeUpLight]) -> None:
    """Eight pointers and a count at `address`, as lists A, B, C and the upload list hold them."""
    words = [light.address for light in lights] + [0] * (8 - len(lights))
    _poke(memory, address, b"".join(_u32(w) for w in words) + _u32(len(lights)))


AMBIENT = MadeUpLight(0x00810000, 2, 0.0, (0.25, 0.25, 0.25, 1.0), (0.0, 0.0, 0.0))
MOON = MadeUpLight(0x00810100, 1, 0.0, (0.2, 0.3, 0.4, 1.0), (0.0, 0.0, 0.0))
OBJECT_AMBIENT = MadeUpLight(0x00810200, 2, 0.0, (0.1, 0.1, 0.1, 1.0), (0.0, 0.0, 0.0))
LAMP = MadeUpLight(0x00810300, 0x80, 10.0, (1.0, 0.9, 0.8, 1.0), (5.0, 6.0, 7.0))
CORONA = MadeUpLight(0x00810400, 0x80, 0.0, (0.5, 0.5, 0.5, 1.0), (50.0, 60.0, 70.0))
STREET = MadeUpLight(0x00810500, 0x80, 15.0, (0.7, 0.7, 0.7, 1.0), (-1.0, -2.0, -3.0))
GLOW = MadeUpLight(0x00810600, 0x80, 0.4, (1.0, 1.0, 1.0, 1.0), (1.5, 2.5, 3.5))


def made_up_memory() -> dict[int, int]:
    """The made-up game's memory: the lights, lists A (world ambient), B (moon, object ambient), C (empty), the
    manager's point lists (objects: lamp and corona; world: street), a sphere, a human whose glow byte is 20."""
    memory: dict[int, int] = {}
    for light in (AMBIENT, MOON, OBJECT_AMBIENT, LAMP, CORONA, STREET, GLOW):
        light.write(memory)
    _list(memory, 0x00715324, [AMBIENT])
    _list(memory, 0x00715348, [MOON, OBJECT_AMBIENT])
    _list(memory, 0x0071536C, [])
    _poke(memory, OBJECT_POINTS, _u32(LAMP.address) + _u32(CORONA.address))
    _poke(memory, WORLD_POINTS, _u32(STREET.address))
    _poke(memory, MANAGER + 0x24, _u32(OBJECT_POINTS) + _u32(OBJECT_POINTS + 8))
    _poke(memory, MANAGER + 0x34, _u32(WORLD_POINTS) + _u32(WORLD_POINTS + 4))
    _poke(memory, SPHERE, _f32(1.0) + _f32(2.0) + _f32(3.0) + _f32(4.0))
    _poke(memory, HUMAN + 0x647, bytes([20]))
    _poke(memory, 0x005104F4, _u32(1234))
    return memory


@dataclass
class Stop:
    """Where the made-up game stops next: the pc, the argument registers (by GPR index) and f12 at an entry, and the
    memory written as it gets there (the upload list at a return)."""

    pc: int
    gpr: dict[int, int] = field(default_factory=dict)
    f12: float = 0.0
    writes: list[tuple[int, list[MadeUpLight]]] = field(default_factory=list)


def entry(ra: int, *, dist_sq: float, flags: int, points: int, pulse: int = 0, human: int = 0) -> Stop:
    """A stop at the selection's entry, called from `ra` with these arguments."""
    gpr = {4: MANAGER, 5: SPHERE, 6: flags, 7: points, 8: pulse, 9: human, 31: ra}
    return Stop(ENTRY, gpr, dist_sq)


def back(ra: int, chosen: list[MadeUpLight]) -> Stop:
    """A stop at the return address `ra`, the upload list holding `chosen`."""
    return Stop(ra, writes=[(0x00715300, chosen)])


class MadeUpGame:
    """A debug server's answers for a made-up game that stops at each of `stops` in turn, as breakpoints would make
    it; after the last it runs on without stopping. Keeps the breakpoints set and every command."""

    def __init__(self, stops: list[Stop]) -> None:
        self.memory = made_up_memory()
        self.stops = stops
        self.at = 0
        self.breakpoints: set[int] = set()
        self.ever_set: set[int] = set()
        self.commands: list[dict[str, Any]] = []
        self._arrive()

    def _arrive(self) -> None:
        if self.at < len(self.stops):
            for address, lights in self.stops[self.at].writes:
                _list(self.memory, address, lights)

    def _registers(self, category: int) -> dict[str, Any]:
        stop = self.stops[self.at]
        if category == 0:
            values = [stop.gpr.get(i, 0) for i in range(35)]
            name = "GPR"
        else:
            values = [struct.unpack("<I", _f32(stop.f12))[0] if i == 12 else 0 for i in range(32)]
            name = "FPR"
        regs = [{"name": f"r{i}", "value": f"{v:032x}", "display": ""} for i, v in enumerate(values)]
        return {"ok": True, "data": {name: {"size": 128, "count": len(regs), "regs": regs}}}

    def answer(self, command: dict[str, Any]) -> dict[str, Any]:
        """The answer to one command."""
        self.commands.append(command)
        name = command["cmd"]
        stopped = self.at < len(self.stops)
        if name == "set_breakpoint":
            self.breakpoints.add(command["address"])
            self.ever_set.add(command["address"])
            return {"ok": True, "address": f"0x{command['address']:08x}"}
        if name == "remove_breakpoint":
            self.breakpoints.discard(command["address"])
            return {"ok": True}
        if name == "status":
            pc = self.stops[self.at].pc if stopped else 0x00100000
            return {"ok": True, "data": {"alive": True, "paused": stopped, "pc": f"0x{pc:08x}", "cycles": 0}}
        if name == "read_registers":
            return self._registers(int(command["category"]))
        if name == "read_memory":
            start, length = int(command["address"]), int(command["length"])
            data = bytes(self.memory.get(start + i, 0) for i in range(length))
            return {"ok": True, "address": f"0x{start:08x}", "length": length, "hex": data.hex()}
        if name == "resume":
            self.at += 1
            self._arrive()
            return {"ok": True}
        return {"ok": False, "error": f"unknown command {name}"}


class Server:
    """A debug server on a free local port answering with `game`, one client at a time."""

    def __init__(self, game: MadeUpGame) -> None:
        self.game = game
        self.server = socket.create_server(("127.0.0.1", 0))
        self.port = self.server.getsockname()[1]
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self) -> None:
        while True:
            try:
                connection, _ = self.server.accept()
            except OSError:
                return
            with connection, connection.makefile("rb") as lines:
                for line in lines:
                    connection.sendall(json.dumps(self.game.answer(json.loads(line))).encode() + b"\n")


@pytest.fixture
def serve() -> Iterator[Any]:
    """Serves a made-up game and closes the servers afterwards."""
    made: list[Server] = []

    def make(stops: list[Stop]) -> tuple[MadeUpGame, DebugClient]:
        made.append(Server(MadeUpGame(stops)))
        return made[-1].game, DebugClient(made[-1].port)

    yield make
    for server in made:
        server.server.close()


def two_calls() -> list[Stop]:
    """A return with no entry before it (the trace began mid-call), an object's call and a human's call."""
    return [
        back(SECTOR_RETURN, [AMBIENT]),
        entry(OBJECT_RETURN, dist_sq=100.0, flags=1, points=1),
        back(OBJECT_RETURN, [MOON, OBJECT_AMBIENT, LAMP]),
        entry(HUMAN_RETURN, dist_sq=36.0, flags=1, points=1, human=HUMAN),
        back(HUMAN_RETURN, [MOON, OBJECT_AMBIENT, GLOW, LAMP]),
    ]


# --- the client's debugger commands -----------------------------------------------------------------------------------


def test_the_client_reads_memory_registers_and_the_pc(serve: Any) -> None:
    game, client = serve([entry(OBJECT_RETURN, dist_sq=2.5, flags=1, points=1)])
    with client:
        assert client.read_memory(SPHERE, 8) == _f32(1.0) + _f32(2.0)
        assert client.cpu_status() == (True, ENTRY)
        assert client.registers(0)[5] == SPHERE
        assert client.registers(2)[12] == struct.unpack("<I", _f32(2.5))[0]
        client.set_breakpoint(ENTRY)
        assert game.breakpoints == {ENTRY}
        client.remove_breakpoint(ENTRY)
        assert game.breakpoints == set()


# --- reading one call -------------------------------------------------------------------------------------------------


def test_a_light_is_read_as_values_never_pointers(serve: Any) -> None:
    _, client = serve([])
    with client:
        light = light_trace.read_light(light_trace.DebugMemory(client), LAMP.address)
    assert (light.type, light.radius, light.position) == (0x80, 10.0, (5.0, 6.0, 7.0))
    assert light.colour == pytest.approx((1.0, 0.9, 0.8, 1.0))


def test_a_point_list_longer_than_the_cap_is_refused(serve: Any) -> None:
    game, client = serve([])
    _poke(game.memory, MANAGER + 0x24, _u32(OBJECT_POINTS) + _u32(OBJECT_POINTS + 4 * 1000))
    with client, pytest.raises(LightTraceError, match="1000 point lights"):
        light_trace.read_vector(light_trace.DebugMemory(client), MANAGER + 0x20)


# --- the trace --------------------------------------------------------------------------------------------------------


def test_each_call_gives_its_inputs_and_the_lights_chosen_in_order(serve: Any) -> None:
    _, client = serve(two_calls())
    with client:
        calls = light_trace.trace(client, calls=2, wait=5.0)
    assert len(calls) == 2
    first, second = calls
    assert (first.caller, first.dist_sq, first.sphere, first.flags) == (OBJECT_RETURN, 100.0, (1.0, 2.0, 3.0, 4.0), 1)
    assert (first.point_lights, first.pulse, first.human, first.glow) == (True, False, False, None)
    assert [light.type for light in first.chosen] == [1, 2, 0x80]
    assert first.chosen[2].position == (5.0, 6.0, 7.0)
    assert [len(first.lists[name]) for name in light_trace.LIST_NAMES] == [1, 2, 0, 2, 1]
    assert first.lists["objects"][1].radius == 0.0
    assert first.tick == 1234
    assert (second.caller, second.human, second.glow) == (HUMAN_RETURN, True, 20)
    assert second.chosen[2].radius == pytest.approx(0.4)


def test_the_trace_removes_its_breakpoints_and_leaves_the_game_running(serve: Any) -> None:
    game, client = serve(two_calls())
    with client:
        light_trace.trace(client, calls=2, wait=5.0)
    assert {ENTRY, OBJECT_RETURN, HUMAN_RETURN, SECTOR_RETURN} <= game.ever_set
    assert game.breakpoints == set()
    assert game.commands[-1]["cmd"] in ("resume", "remove_breakpoint", "status")
    assert game.at >= len(game.stops)


def test_a_trace_stops_when_no_call_comes(serve: Any) -> None:
    _, client = serve(two_calls()[:3])
    with client:
        calls = light_trace.trace(client, calls=10, wait=0.2)
    assert len(calls) == 1


def test_a_caller_the_page_does_not_name_is_followed_to_its_return(serve: Any) -> None:
    stray = 0x00123458
    game, client = serve([entry(stray, dist_sq=0.0, flags=2, points=0), back(stray, [AMBIENT])])
    with client:
        calls = light_trace.trace(client, calls=1, wait=5.0)
    assert stray in game.ever_set
    assert calls[0].caller == stray
    assert calls[0].chosen == (Light(2, 0.0, (0.25, 0.25, 0.25, 1.0), (0.0, 0.0, 0.0)),)


class FlakyClient(DebugClient):
    """A client whose commands fail as a real link can: `late` names commands (cmd, and address or None for any)
    that reach the game but whose answer never comes back (a timeout after the server acted), `refused` ones the
    server refuses without acting."""

    def __init__(self, port: int, late: set[tuple[str, int | None]], refused: set[tuple[str, int | None]]) -> None:
        super().__init__(port)
        self.late = late
        self.refused = refused

    # Whether `command` is one of `faults`, by name and address or by name alone.
    def _matches(self, faults: set[tuple[str, int | None]], command: dict[str, Any]) -> bool:
        return (command["cmd"], command.get("address")) in faults or (command["cmd"], None) in faults

    def request(self, command: dict[str, Any], timeout: float = 5.0) -> dict[str, Any]:
        """Answer as the server does, or fail the way `late` and `refused` say."""
        if self._matches(self.refused, command):
            return {"ok": False, "error": "refused"}
        answer = super().request(command, timeout)
        if self._matches(self.late, command):
            raise DebugServerError(f"{command['cmd']}: timed out")
        return answer


def flaky(serve: Any, stops: list[Stop], **faults: set[tuple[str, int | None]]) -> tuple[MadeUpGame, FlakyClient]:
    """A made-up game behind a FlakyClient with these faults."""
    game, plain = serve(stops)
    plain.close()
    return game, FlakyClient(plain.port, faults.get("late", set()), faults.get("refused", set()))


def test_a_breakpoint_whose_set_timed_out_but_took_effect_is_removed(serve: Any) -> None:
    game, client = flaky(serve, two_calls(), late={("set_breakpoint", HUMAN_RETURN)})
    with client, pytest.raises(DebugServerError):
        light_trace.trace(client, calls=2, wait=5.0)
    assert HUMAN_RETURN in game.ever_set
    assert game.breakpoints == set()


def test_breakpoints_are_removed_after_an_error_mid_trace(serve: Any) -> None:
    game, client = flaky(serve, two_calls(), late={("read_memory", None)})
    with client, pytest.raises(DebugServerError):
        light_trace.trace(client, calls=2, wait=5.0)
    assert game.breakpoints == set()


def test_one_refused_removal_does_not_keep_the_others(serve: Any, capsys: pytest.CaptureFixture[str]) -> None:
    game, client = flaky(
        serve,
        two_calls(),
        refused={("remove_breakpoint", ENTRY)},
        late={("remove_breakpoint", OBJECT_RETURN)},
    )
    with client:
        light_trace.trace(client, calls=2, wait=5.0)
    assert game.breakpoints == {ENTRY}
    warning = capsys.readouterr().out
    assert "0x0017de10" in warning and "0x001803a4" in warning


def test_breakpoints_are_removed_after_a_trace_that_timed_out(serve: Any) -> None:
    game, client = serve(two_calls()[:3])
    with client:
        light_trace.trace(client, calls=10, wait=0.2)
    assert game.breakpoints == set()


# --- the files --------------------------------------------------------------------------------------------------------


def test_the_selections_have_the_columns_of_coneys_lab_dump(serve: Any) -> None:
    _, client = serve(two_calls())
    with client:
        calls = light_trace.trace(client, calls=2, wait=5.0)
    lines = light_trace.selections_csv(calls).splitlines()
    header = lines[0].split(",")
    # The columns of Coney's lab dump, src/debug/lighting_lab.cpp selectionColumns()
    # (docs/guides/realtime-lighting.md#lab-built).
    expected = ["caller", "dist_sq", "sphere_x", "sphere_y", "sphere_z", "sphere_radius", "flags", "point_lights"]
    expected += ["pulse", "human", "count"]
    expected += [f"light{i}_{f}" for i in range(8) for f in ("type", "radius", "r", "g", "b", "a", "x", "y", "z")]
    assert header == expected
    assert lines[1].split(",")[:14] == [
        "0x001803a4",
        "100",
        "1",
        "2",
        "3",
        "4",
        "1",
        "1",
        "0",
        "0",
        "3",
        "1",
        "0",
        "0.2",
    ]
    assert len(lines[1].split(",")) == len(expected)
    assert lines[2].split(",")[9:11] == ["1", "4"]


def test_the_inputs_files_list_each_calls_lists_and_glow(serve: Any) -> None:
    _, client = serve(two_calls())
    with client:
        calls = light_trace.trace(client, calls=2, wait=5.0)
    assert light_trace.calls_csv(calls).splitlines() == [
        "call,tick,caller,glow,a,b,c,objects,world",
        "0,1234,0x001803a4,,1,2,0,2,1",
        "1,1234,0x001746f8,20,1,2,0,2,1",
    ]
    lists = light_trace.lists_csv(calls).splitlines()
    assert lists[0] == "call,list,slot,type,radius,r,g,b,a,x,y,z"
    assert lists[1] == "0,a,0,2,0,0.25,0.25,0.25,1,0,0,0"
    assert "0,objects,0,128,10,1,0.9,0.8,1,5,6,7" in lists
    assert len(lists) == 1 + 2 * 6


def test_numbers_are_the_shortest_text_of_the_float() -> None:
    assert [light_trace.number(v) for v in (0.0, 1.0, 0.9, 1600.0, -2.5)] == ["0", "1", "0.9", "1600", "-2.5"]
    assert light_trace.number(struct.unpack("<f", _f32(0.227))[0]) == "0.227"
    assert [light_trace.number(v) for v in (1e20, 1e-05, 123456.0)] == ["1e+20", "1e-05", "123456"]


def test_the_facts_count_calls_by_caller_and_lights_by_kind(serve: Any) -> None:
    _, client = serve(two_calls())
    with client:
        calls = light_trace.trace(client, calls=2, wait=5.0)
    facts = light_trace.facts(calls)
    assert facts["calls"] == 2
    assert (facts["calls_object"], facts["calls_human"], facts["calls_sector"]) == (1, 1, 0)
    assert (facts["chosen_directional"], facts["chosen_ambient"], facts["chosen_point"]) == (2, 2, 3)
    assert facts["glow_calls"] == 1
    assert facts["max_count"] == 4
    assert facts["frames"] == 1


def test_the_trace_files_are_refused_inside_the_repository() -> None:
    inside = Path(light_trace.__file__).resolve().parent / "lights.csv"
    with pytest.raises(ConfigError):
        light_trace.write_files([], inside)


def test_the_files_go_beside_the_selections_and_their_hashes_are_kept(tmp_path: Path, serve: Any) -> None:
    _, client = serve(two_calls())
    with client:
        calls = light_trace.trace(client, calls=2, wait=5.0)
    out = tmp_path / "lights.csv"
    hashes = light_trace.write_files(calls, out)
    assert sorted(p.name for p in tmp_path.iterdir()) == ["lights.calls.csv", "lights.csv", "lights.lists.csv"]
    assert set(hashes) == {"selections_csv", "calls_csv", "lists_csv"}
    assert all(len(h) == 64 for h in hashes.values())


# --- the command and its run record -----------------------------------------------------------------------------------


@pytest.mark.reads("docs_only")
def test_a_traced_run_writes_its_files_and_a_record_of_counts_and_hashes(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], serve: Any
) -> None:
    _, client = serve(two_calls())
    monkeypatch.setattr(light_trace_cli, "debug_client", lambda agent, copy: client)
    out = tmp_path / "scratch" / "lights.csv"
    with fake_pcsx2() as port:
        record_tests.hold_copy(tmp_path, monkeypatch, port)
        request = run_records_cli.RecordRequest(state="unknown", name=None)
        status = light_trace_cli.run_lights(out, "tooling-1", "pcsx2-z", (2, 5.0), request)
    assert status == 0
    record = run_records.load(checkout, "r-20261023-light-selection")
    assert (record.kind, record.name, record.tool, record.copy) == (
        "trace",
        "light-selection",
        "trace lights",
        "pcsx2-z",
    )
    assert record.values["calls"] == 2 and record.values["calls_human"] == 1
    assert record.addresses["LightManager_SelectLights"] == ENTRY
    assert record.hashes["selections_csv"] == hashlib.sha256(out.read_bytes()).hexdigest()
    assert "2 calls" in capsys.readouterr().out


@pytest.mark.reads("docs_only")
def test_a_run_with_no_call_writes_no_record(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, serve: Any
) -> None:
    _, client = serve([])
    monkeypatch.setattr(light_trace_cli, "debug_client", lambda agent, copy: client)
    with fake_pcsx2() as port:
        record_tests.hold_copy(tmp_path, monkeypatch, port)
        request = run_records_cli.RecordRequest(state="unknown", name=None)
        status = light_trace_cli.run_lights(tmp_path / "lights.csv", "tooling-1", "pcsx2-z", (5, 0.1), request)
    assert status == 1
    assert not (checkout / "research" / "runs").exists()


def test_the_record_flags_go_together(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["trace", "lights", "--out", "x.csv", "--agent", "a", "--library-state", "unknown"]) != 0
    assert "--library-state and --run-name belong to --run-record" in capsys.readouterr().err
    assert main(["trace", "lights", "--out", "x.csv", "--agent", "a", "--run-record"]) != 0
    assert "--run-record needs --library-state" in capsys.readouterr().err
