# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the PCSX2 side of the trace tools: PINE messages, patched state copies, address expressions and the
recorder, against a fake PINE server (a socket stub with a made-up game). Nothing here needs PCSX2 or game data."""

from __future__ import annotations

import math
import socket
import struct
import threading
import zipfile
from collections.abc import Iterator, Sequence
from pathlib import Path

import pytest

from coney_tools import pcsx2_state
from coney_tools.config import ConfigError
from coney_tools.game_memory import (
    CAMERA_POINTER,
    GAME_TIMER_POINTER,
    HUMAN_SIZE,
    HUMAN_TABLE,
    PER_PLAYER_TABLE,
    TRANSFORM_TABLE,
    ExpressionError,
    GameMemory,
    parse,
)
from coney_tools.pcsx2_cli import _scenario_state
from coney_tools.pine import PineClient, PineError, Read, Write, decode, encode
from coney_tools.recorder import PAD_BYTES, TICK_COUNTER, Recorder
from coney_tools.scenario import ScenarioError, load_scenario

REPO = Path(__file__).resolve().parents[2]

# --- a made-up game behind a fake PINE server ----------------------------------------------------------------------

TIMER = 0x00200000
CAMERA = 0x00300000
RECORD = 0x00100000
PLAYER = HUMAN_TABLE + 1 * HUMAN_SIZE
CIVILIAN = HUMAN_TABLE + 0 * HUMAN_SIZE
PLAYER_INDEX = 3
CIVILIAN_INDEX = 5


class FakeGame:
    """Sparse EE memory with two humans, a camera and a game clock. Every `polls_per_update` messages the game runs
    one character update of 1/30 s: it copies the pad's left stick y byte into the player's speed and counts updates
    in the player's clip, so a test can see which input each update read."""

    def __init__(self, polls_per_update: int = 2) -> None:
        """Two humans (the player is human 1), a camera and a clock; one update every `polls_per_update` messages."""
        self.memory: dict[int, int] = {}
        self.messages = 0
        self.updates = 0
        self.polls_per_update = polls_per_update
        # When set, the first update that sees cross held runs twice, as if a poll had missed one.
        self.skip_on_cross = False
        self.write32(GAME_TIMER_POINTER, TIMER)
        self.write32(TIMER + 0x48, 100_000)
        self.write32(TICK_COUNTER, 1)
        self.write32(CAMERA_POINTER, CAMERA)
        self.write_float(CAMERA + 0x10, 1.5)
        for human, index, name, number in (
            (CIVILIAN, CIVILIAN_INDEX, b"PoizoCiv", 0xFF),
            (PLAYER, PLAYER_INDEX, b"Rembrandt", 0),
        ):
            self.write32(human + 0xD4, RECORD + index * 0x180)
            self.write32(human + 0x90, index << 16)
            self.memory[human + 0x1B0] = number
            for k, byte in enumerate(name):
                self.memory[human + 0x80 + k] = byte
        self.write_float(TRANSFORM_TABLE + PLAYER_INDEX * 0x20 + 0x1C, 1.0)
        for k, byte in enumerate(b"\xff\xff\x80\x80\x80\x80"):
            self.memory[PAD_BYTES + k] = byte

    def read(self, address: int, size: int) -> int:
        """`size` bytes at `address`, little-endian; unwritten memory reads 0."""
        return int.from_bytes(bytes(self.memory.get(address + k, 0) for k in range(size)), "little")

    def write(self, address: int, size: int, value: int) -> None:
        """Store `value` as `size` little-endian bytes."""
        for k, byte in enumerate(value.to_bytes(size, "little")):
            self.memory[address + k] = byte

    def write32(self, address: int, value: int) -> None:
        """Store a 32-bit word."""
        self.write(address, 4, value)

    def write_float(self, address: int, value: float) -> None:
        """Store an f32."""
        self.write32(address, struct.unpack("<I", struct.pack("<f", value))[0])

    def tick(self) -> None:
        """Count a message; run an update (two at the first cross, with skip_on_cross) when one is due."""
        self.messages += 1
        if self.messages % self.polls_per_update:
            return
        runs = 1
        if self.skip_on_cross and not self.memory[PAD_BYTES + 1] & 0x40:
            runs, self.skip_on_cross = 2, False
        for _ in range(runs):
            self.updates += 1
            # 1000 / 30 ms per update, as whole milliseconds that add up; two 60 Hz ticks, the count left odd.
            self.write32(TIMER + 0x48, 100_000 + (self.updates * 1000) // 30)
            self.write32(TICK_COUNTER, 2 * self.updates + 1)
            stick_y = self.memory[PAD_BYTES + 5]
            self.write_float(PLAYER + 0x1AC, float(0x80 - stick_y))
            self.write32(RECORD + PLAYER_INDEX * 0x180 + 0x20, self.updates)


def _serve(game: FakeGame, server: socket.socket) -> None:
    """Answer PINE messages for one client until it goes away: a message's writes land, then the game may run an
    update, then its reads are answered, as when the emulator runs between two messages."""
    connection, _ = server.accept()
    with connection:
        while True:
            head = connection.recv(4, socket.MSG_WAITALL)
            if len(head) < 4:
                return
            body = connection.recv(struct.unpack("<I", head)[0] - 4, socket.MSG_WAITALL)
            reads: list[tuple[int, int]] = []
            status, at = False, 0
            while at < len(body):
                opcode = body[at]
                address = struct.unpack_from("<I", body, at + 1)[0] if opcode != 0x0F else 0
                at += 5 if opcode != 0x0F else 1
                if opcode <= 3:
                    reads.append((address, 1 << opcode))
                elif opcode <= 7:
                    size = 1 << (opcode - 4)
                    game.write(address, size, int.from_bytes(body[at : at + size], "little"))
                    at += size
                elif opcode == 0x0F:
                    status = True
                else:
                    connection.sendall(struct.pack("<IB", 5, 0xFF))
                    return
            game.tick()
            reply = bytearray([0])
            for address, size in reads:
                reply += game.read(address, size).to_bytes(size, "little")
            if status:
                reply += struct.pack("<I", 0)
            connection.sendall(struct.pack("<I", len(reply) + 4) + bytes(reply))


@pytest.fixture
def fake_pine() -> Iterator[tuple[FakeGame, int]]:
    """A FakeGame behind a PINE server on a free local port; yields the game and the port."""
    game = FakeGame()
    server = socket.create_server(("127.0.0.1", 0))
    thread = threading.Thread(target=_serve, args=(game, server), daemon=True)
    thread.start()
    yield game, server.getsockname()[1]
    server.close()


# --- PINE messages ---------------------------------------------------------------------------------------------------


def test_a_batch_is_one_message_of_writes_then_reads() -> None:
    message = encode([Read(0x10, 4), Read(0x20, 2)], [Write(0x30, 1, 0xAB)])
    assert struct.unpack_from("<I", message)[0] == len(message)
    # Write8 (4) with its byte, then Read32 (2), then Read16 (1).
    assert message[4:] == bytes([4, 0x30, 0, 0, 0, 0xAB, 2, 0x10, 0, 0, 0, 1, 0x20, 0, 0, 0])


def test_a_reply_gives_each_read_unsigned() -> None:
    reads = [Read(0, 4), Read(0, 1)]
    assert decode(bytes([0, 0xFF, 0xFF, 0xFF, 0xFF, 0x80]), reads) == [0xFFFFFFFF, 0x80]
    with pytest.raises(PineError, match="refused"):
        decode(bytes([0xFF]), reads)
    with pytest.raises(PineError, match="shorter"):
        decode(bytes([0, 1, 2]), reads)


def test_the_client_reads_and_writes_over_a_socket(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    with PineClient(port) as client:
        assert client.status() == "running"
        client.batch([], [Write(0x40, 2, 0xBEEF)])
        assert client.batch([Read(0x40, 2), Read(GAME_TIMER_POINTER, 4)]) == [0xBEEF, TIMER]
    assert game.read(0x40, 2) == 0xBEEF


def test_no_server_is_a_clear_error() -> None:
    with socket.create_server(("127.0.0.1", 0)) as probe:
        port = probe.getsockname()[1]
    with pytest.raises(PineError, match="no PINE server"):
        PineClient(port, timeout=1.0)


# --- patched state copies --------------------------------------------------------------------------------------------

PATCHES = """
[patch.demo]
description = "test"
research = "docs/guides/research-workflow.md#driving-pcsx2"
edits = [
  { address = 0x1000, original = [0x92220002], replacement = [0x92220022] },
  { address = 0x2000, bytes = "ff ff 80" },
]
"""


def _state(path: Path, word: int) -> Path:
    """A made-up save state: a zip with a 32 MB eeMemory.bin holding `word` at 0x1000, and one other entry."""
    memory = bytearray(pcsx2_state.EE_MEMORY_SIZE)
    struct.pack_into("<I", memory, 0x1000, word)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as state:
        state.writestr("PCSX2 Savestate Version.id", b"made up")
        state.writestr(pcsx2_state.EE_MEMORY, bytes(memory))
    return path


def _patches(tmp_path: Path) -> list[pcsx2_state.Patch]:
    """The made-up patch `demo`, from a file written under `tmp_path`."""
    file = tmp_path / "patches.toml"
    file.write_text(PATCHES, encoding="utf-8")
    return pcsx2_state.pick(pcsx2_state.load_patches(file), ["demo"])


def test_a_copy_gets_the_edits_and_the_source_is_untouched(tmp_path: Path) -> None:
    source = _state(tmp_path / "source.p2s", 0x92220002)
    before = source.read_bytes()
    count = pcsx2_state.prepare(source, tmp_path / "out" / "copy.p2s", _patches(tmp_path), [])
    assert count == 2
    assert source.read_bytes() == before
    with zipfile.ZipFile(tmp_path / "out" / "copy.p2s") as copy:
        assert copy.namelist() == ["PCSX2 Savestate Version.id", pcsx2_state.EE_MEMORY]
        memory = copy.read(pcsx2_state.EE_MEMORY)
    assert struct.unpack_from("<I", memory, 0x1000)[0] == 0x92220022
    assert memory[0x2000:0x2003] == b"\xff\xff\x80"


def test_a_copy_is_refused_when_the_original_bytes_differ(tmp_path: Path) -> None:
    source = _state(tmp_path / "source.p2s", 0x12345678)
    with pytest.raises(pcsx2_state.StateError, match="not the expected"):
        pcsx2_state.prepare(source, tmp_path / "copy.p2s", _patches(tmp_path), [])
    assert not (tmp_path / "copy.p2s").exists()
    assert not (tmp_path / "copy.p2s.partial").exists()


def test_a_copy_never_goes_into_sstates_or_over_its_source(tmp_path: Path) -> None:
    source = _state(tmp_path / "source.p2s", 0x92220002)
    slots = tmp_path / "pcsx2" / "sstates"
    with pytest.raises(pcsx2_state.StateError, match="quick-save"):
        pcsx2_state.prepare(source, slots / "copy.p2s", _patches(tmp_path), [slots])
    with pytest.raises(pcsx2_state.StateError, match="source"):
        pcsx2_state.prepare(source, source, _patches(tmp_path), [])


def test_a_repacked_copy_has_the_same_entries_in_deflate(tmp_path: Path) -> None:
    source = tmp_path / "source.p2s"
    with zipfile.ZipFile(source, "w") as state:
        state.writestr("PCSX2 Savestate Version.id", b"made up")
        state.writestr(zipfile.ZipInfo(pcsx2_state.EE_MEMORY), b"" * 4096, zipfile.ZIP_BZIP2)
        state.writestr(zipfile.ZipInfo("Screenshot.png"), b"not a png", zipfile.ZIP_STORED)
    before = source.read_bytes()
    count = pcsx2_state.repack(source, tmp_path / "out" / "copy.p2s", [])
    assert count == 1
    assert source.read_bytes() == before
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(tmp_path / "out" / "copy.p2s") as copy:
        assert copy.namelist() == original.namelist()
        methods = {info.filename: info.compress_type for info in copy.infolist()}
        for name in original.namelist():
            assert copy.read(name) == original.read(name)
    assert methods[pcsx2_state.EE_MEMORY] == zipfile.ZIP_DEFLATED
    assert methods["Screenshot.png"] == zipfile.ZIP_STORED
    assert not (tmp_path / "out" / "copy.p2s.partial").exists()


def test_a_repacked_copy_never_goes_into_sstates_or_over_its_source(tmp_path: Path) -> None:
    source = _state(tmp_path / "source.p2s", 0x92220002)
    slots = tmp_path / "pcsx2" / "sstates"
    with pytest.raises(pcsx2_state.StateError, match="quick-save"):
        pcsx2_state.repack(source, slots / "copy.p2s", [slots])
    with pytest.raises(pcsx2_state.StateError, match="source"):
        pcsx2_state.repack(source, source, [])
    assert not slots.exists()


def test_a_repack_refuses_a_zip_that_is_not_a_state(tmp_path: Path) -> None:
    source = tmp_path / "other.zip"
    with zipfile.ZipFile(source, "w") as other:
        other.writestr("readme.txt", b"hello")
    with pytest.raises(pcsx2_state.StateError, match="not a PCSX2 save state"):
        pcsx2_state.repack(source, tmp_path / "copy.p2s", [])
    assert not (tmp_path / "copy.p2s").exists()


def test_bad_patch_files_are_named(tmp_path: Path) -> None:
    file = tmp_path / "patches.toml"
    file.write_text("[patch.x]\nedits = [{ address = 0x10, original = [1], replacement = [1, 2] }]\n", encoding="utf-8")
    with pytest.raises(pcsx2_state.StateError, match="as many words"):
        pcsx2_state.load_patches(file)
    with pytest.raises(pcsx2_state.StateError, match="known"):
        pcsx2_state.pick({}, ["nope"])


def test_the_repository_patches_load_and_check_their_words() -> None:
    patches = pcsx2_state.load_patches(REPO / "research/traces/patches.toml")
    assert {"scripted-pad", "right-stick", "puppet"} <= set(patches)
    for patch in patches.values():
        assert patch.research.startswith("docs/")
        for edit in patch.edits:
            # Code edits only change an lbu's offset or turn a store into a nop: never more than one word each.
            assert edit.expected is None or len(edit.expected) == len(edit.data) == 4


# --- address expressions ----------------------------------------------------------------------------------------------


class MemoryOf:
    """A Memory over a FakeGame, without a socket."""

    def __init__(self, game: FakeGame) -> None:
        """Serve `game` directly."""
        self.game = game

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """Apply the writes, then answer the reads, with no update in between."""
        for write in writes:
            self.game.write(write.address, write.size, write.value)
        return [self.game.read(read.address, read.size) for read in reads]


def test_expressions_find_the_player_and_his_tables() -> None:
    game = GameMemory(MemoryOf(FakeGame()))
    names = game.names()
    assert parse("player").evaluate(names) == PLAYER
    assert parse("tf(player) + 0x4").evaluate(names) == TRANSFORM_TABLE + PLAYER_INDEX * 0x20 + 4
    assert parse("prec(human('PoizoCiv')) + 0x1e").evaluate(names) == PER_PLAYER_TABLE + CIVILIAN_INDEX * 0x2C + 0x1E
    assert parse("rec(player)").evaluate(names) == RECORD + PLAYER_INDEX * 0x180
    assert parse("camera + 0x10").evaluate(names) == CAMERA + 0x10
    assert parse("f32(camera + 0x10) * 2").evaluate(names) == 3.0
    assert parse("game_time").evaluate(names) == TIMER + 0x48
    assert parse("deg(heading(player))").evaluate(names) == 0.0
    assert math.isclose(parse("wrap(270)").evaluate(names), -90.0)


def test_expressions_refuse_anything_but_arithmetic_and_calls() -> None:
    for text in ("__import__('os')", "player.x", "[1][0]", "1 if 2 else 3", "f(x=1)", "a < b"):
        with pytest.raises(ExpressionError):
            # A call by name is allowed by the grammar but fails as an unknown name.
            parse(text).evaluate(GameMemory(MemoryOf(FakeGame())).names())
    with pytest.raises(ExpressionError, match="Rembrandt"):
        parse("human('Nobody')").evaluate(GameMemory(MemoryOf(FakeGame())).names())


# --- the recorder -----------------------------------------------------------------------------------------------------


def _repo(tmp_path: Path, script: str, extra: str = "") -> Path:
    """A made-up checkout with a field set and one scenario; returns the scenario's path."""
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    traces = tmp_path / "research" / "traces"
    (traces / "scenarios").mkdir(parents=True)
    (traces / "fields.toml").write_text(
        """
[[demo]]
name = "speed"
address = "player + 0x1ac"
type = "f32"
digits = 1
[[demo]]
name = "clip"
address = "rec(player) + 0x20"
type = "s32"
[[demo]]
name = "double"
formula = "speed * 2"
digits = 1
""",
        encoding="utf-8",
    )
    (traces / "scenarios" / "demo.txt").write_text(script, encoding="utf-8")
    scenario = traces / "scenarios" / "demo.toml"
    scenario.write_text(f'input = "demo.txt"\nupdates = 6\nfields = ["demo"]\n{extra}', encoding="utf-8")
    return scenario


def test_one_row_per_update_with_the_input_of_the_frame_before(tmp_path: Path, fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    path = _repo(tmp_path, "2 stick left 0 100\n4 stick left 0 0\n")
    scenario = load_scenario(path, tmp_path)
    with PineClient(port) as client:
        recording = Recorder(GameMemory(client), scenario).record(timeout=5)
    assert recording.header == ["step", "speed", "clip", "double"]
    assert [row[0] for row in recording.rows] == [0, 1, 2, 3, 4, 5, 6]
    assert recording.missed == []
    # Frame 2's stick (raw 0x00 for 100 % up) is read by step 3, frame 4's release by step 5, as in Coney.
    speeds = [row[1] for row in recording.rows]
    assert speeds == ["0.0", "0.0", "0.0", "128.0", "128.0", "0.0", "0.0"]
    assert recording.rows[3][3] == "256.0"
    # The pad is left at rest afterwards.
    assert game.read(PAD_BYTES, 4) == 0x8080FFFF


def test_a_missed_update_is_reported(tmp_path: Path, fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    game.skip_on_cross = True
    scenario = load_scenario(_repo(tmp_path, "1 tap cross\n"), tmp_path)
    with PineClient(port) as client:
        recording = Recorder(GameMemory(client), scenario).record(timeout=5)
    # The tap of frame 1 reaches the update of step 2, which runs with step 3 before the next poll.
    assert recording.missed == [2]
    assert 2 not in [row[0] for row in recording.rows]


def test_setup_writes_land_on_their_frames(tmp_path: Path, fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    extra = """
[original]
let = { civ = 'human("PoizoCiv")' }
setup = [{ address = "prec(civ) + 0x1e", type = "u8", value = "7", frame = 1, until = 2 }]
"""
    scenario = load_scenario(_repo(tmp_path, "", extra), tmp_path)
    with PineClient(port) as client:
        Recorder(GameMemory(client), scenario).record(timeout=5)
    assert game.read(PER_PLAYER_TABLE + CIVILIAN_INDEX * 0x2C + 0x1E, 1) == 7


def test_scenarios_name_their_problems(tmp_path: Path) -> None:
    path = _repo(tmp_path, "").with_name("bad.toml")
    path.write_text('input = "demo.txt"\nupdates = 6\nfields = ["nope"]\n', encoding="utf-8")
    with pytest.raises(ScenarioError, match="unknown field set"):
        load_scenario(path, tmp_path)
    path.write_text('input = "demo.txt"\nupdates = 6\n[[field]]\nname = "a"\nformula = "b + 1"\n', encoding="utf-8")
    with pytest.raises(ScenarioError, match="not a field before it"):
        load_scenario(path, tmp_path)


def test_a_scenario_may_name_a_state_file_under_scratch(tmp_path: Path) -> None:
    scenario = load_scenario(_repo(tmp_path, "", '[original]\nstate = "states/x.p2s"\n'), tmp_path)
    assert scenario.slot is None
    assert scenario.state == "states/x.p2s"
    scratch = tmp_path / "scratch"
    # The file must exist under the scratch folder; --state wins over the scenario's own.
    with pytest.raises(ConfigError, match="does not exist"):
        _scenario_state(scenario, None, scratch)
    (scratch / "states").mkdir(parents=True)
    (scratch / "states" / "x.p2s").write_bytes(b"")
    assert _scenario_state(scenario, None, scratch) == str(scratch / "states" / "x.p2s")
    assert _scenario_state(scenario, "slot:3", scratch) == "slot:3"
    path = tmp_path / "research" / "traces" / "scenarios" / "demo.toml"
    head = 'input = "demo.txt"\nupdates = 6\nfields = ["demo"]\n[original]\n'
    for original, problem in [
        ('state = "/abs.p2s"\n', "relative to scratch_dir"),
        ('slot = 1\nstate = "x.p2s"\n', "both a slot and a state"),
    ]:
        path.write_text(head + original, encoding="utf-8")
        with pytest.raises(ScenarioError, match=problem):
            load_scenario(path, tmp_path)


def test_the_repository_scenarios_load() -> None:
    folder = REPO / "research/traces/scenarios"
    scenarios = sorted(folder.glob("*.toml"))
    assert len(scenarios) >= 3
    for path in scenarios:
        scenario = load_scenario(path, REPO)
        # A scenario plays input, unless it says it has none (a run that only logs calls).
        assert scenario.events or "no input" in scenario.description, path
        assert scenario.coney_level, path
        assert scenario.slot is not None or scenario.state is not None, path
        assert "scripted-pad" in scenario.patches, path
