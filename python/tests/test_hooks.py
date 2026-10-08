# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the call-logging hooks and the recorder features they brought (fields followed through pointers, game
function calls): the caves' instructions, the patch that installs them, reading the ring, and the recorder writing a
call. Nothing here needs PCSX2 or game data; the instruction words are checked against hand-assembled MIPS."""

from __future__ import annotations

import struct
from collections.abc import Sequence
from pathlib import Path

import pytest
from test_pcsx2 import PLAYER, PLAYER_INDEX, RECORD, TICK_COUNTER, FakeGame, MemoryOf, _repo

from coney_tools import hooks
from coney_tools.game_memory import GameMemory
from coney_tools.hooks import CALL_BASE, RING_BASE, HookError, RingReader, build, load_hooks, parse_hook
from coney_tools.pine import Read, Write
from coney_tools.recorder import Recorder, entry_step
from coney_tools.scenario import load_scenario

REPO = Path(__file__).resolve().parents[2]

#: A hook at a function entry whose two displaced instructions are `addiu sp,sp,-0xa0; lui v1,0x51`.
ENTRY = {
    "address": 0x003A2EA0,
    "original": [0x27BDFF60, 0x3C030051],
    "log": ["count", {"value": "[0x005104f4]", "name": "ticks"}, "ra", {"value": "[a0 + 0x14]", "type": "s16"}],
}


def test_a_cave_saves_logs_counts_restores_and_jumps_back() -> None:
    hook = parse_hook("tick", ENTRY, "test")
    code = hook.cave(0x000A0000, 3)
    # Frame and saves: addiu sp,sp,-64; sq at,0(sp); sq v1,16(sp); sq t8,32(sp); sq t9,48(sp).
    assert code[:5] == [0x27BDFFC0, 0x7FA10000, 0x7FA30010, 0x7FB80020, 0x7FB90030]
    # The entry: lui at,0xb; lw t8,0(at); andi t8,t8,0xfff; sll t8,t8,5; addu t8,t8,at; the id 3 at +0x10.
    assert code[5:12] == [0x3C01000B, 0x8C380000, 0x33180FFF, 0x0018C140, 0x0301C021, 0x24190003, 0xAF190010]
    # count is mfc0 t9,Count; [0x005104f4] a lui and a lw; ra an addu; [a0 + 0x14] as s16 an lh.
    assert code[12:14] == [0x40194800, 0xAF190014]
    assert code[14:17] == [0x3C190051, 0x8F3904F4, 0xAF190018]
    assert code[17:19] == [0x03E0C821, 0xAF19001C]
    assert code[19:22] == [0x0080C821, 0x87390014, 0xAF190020]
    # Counted after the entry is written, then the registers come back and the displaced pair runs.
    assert code[22:25] == [0x8C380000, 0x27180001, 0xAC380000]
    assert code[25:30] == [0x7BB90030, 0x7BB80020, 0x7BA30010, 0x7BA10000, 0x27BD0040]
    assert code[30:] == [0x27BDFF60, 0x3C030051, 0x080E8BAA, 0]


def test_a_jump_and_its_delay_slot_end_the_cave_without_a_jump_back() -> None:
    hook = parse_hook("setter", {"address": 0x00147EF0, "original": [0x03E00008, 0xAC850020], "log": ["a1"]}, "t")
    assert hook.cave(0x000A0000, 1)[-2:] == [0x03E00008, 0xAC850020]


def test_hooks_refuse_branches_overlaps_and_bad_values() -> None:
    with pytest.raises(HookError, match="branch"):
        parse_hook("b", {"address": 0x1000, "original": [0x10400003, 0]}, "t")
    with pytest.raises(HookError, match="branch"):
        parse_hook("b", {"address": 0x1000, "original": [0, 0x03E00008]}, "t")
    with pytest.raises(HookError, match="not a register"):
        parse_hook("b", {"address": 0x1000, "original": [0, 0], "log": ["[nope + 4]"]}, "t")
    with pytest.raises(HookError, match="only a load"):
        parse_hook("b", {"address": 0x1000, "original": [0, 0], "log": [{"value": "a0", "type": "s16"}]}, "t")
    first = parse_hook("a", {"address": 0x1000, "original": [0, 0]}, "t")
    second = parse_hook("b", {"address": 0x1004, "original": [0, 0]}, "t")
    with pytest.raises(HookError, match="overlap"):
        build([first, second])


def test_the_patch_puts_the_jumps_first_and_checks_the_displaced_words() -> None:
    hook = parse_hook("tick", ENTRY, "test")
    patch = build([hook])
    jump = patch.edits[0]
    assert jump.address == 0x003A2EA0
    assert jump.data == struct.pack("<II", 0x08028000, 0)
    assert jump.expected == struct.pack("<II", 0x27BDFF60, 0x3C030051)
    assert any(edit.address == RING_BASE and edit.data == bytes(16) for edit in patch.edits)


def test_a_call_hook_calls_the_waiting_function_once() -> None:
    hook = parse_hook("call", {"address": 0x00293B28, "original": [0x27BDFFA0, 0x3C020051], "call": True}, "t")
    code = hook.cave(0x000A0000, 1)
    # lw t9,fn; beq t9,zero over the call; the call clears the address before jalr t9.
    at_fn = code.index(0x8C39F000)
    skip = code[at_fn + 1]
    assert skip >> 26 == 0x04
    call_end = at_fn + 3 + (skip & 0xFFFF) - 1
    assert 0x0320F809 in code[at_fn:call_end]
    assert code.index(0xAC20F000) < code.index(0x0320F809)
    assert any(edit.address == CALL_BASE for edit in build([hook]).edits)
    with pytest.raises(HookError, match="logs nothing"):
        parse_hook("call", {"address": 0x1000, "original": [0, 0], "call": True, "log": ["a0"]}, "t")


def test_the_ring_reader_keeps_entries_in_order_and_counts_overflow() -> None:
    game = FakeGame()
    hook = parse_hook("tick", ENTRY, "test")
    for n in range(3):
        entry = RING_BASE + 0x10 + n * 32
        for k, word in enumerate([1, 100 + n, 0x4E47 + n, 0x3A3198, 0xFFFFFFFE]):
            game.write(entry + 4 * k, 4, word)
    reader = RingReader(MemoryOf(game), [hook])
    reader.drain(3, step=7)
    entries = reader.log.entries["tick"]
    assert [(seq, step, values[1]) for seq, step, values in entries] == [(0, 7, 0x4E47), (1, 7, 0x4E48), (2, 7, 0x4E49)]
    # The s16 load (sign-extended by lh) comes back signed.
    assert entries[0][2][3] == -2
    assert "seq,step,count,ticks,ra,[a0+0x14]" in reader.log.csv(hook)
    reader.drain(3 + hooks.RING_ENTRIES + 5, step=8)
    assert reader.log.lost == 5


def test_the_repository_hooks_build() -> None:
    known = load_hooks(REPO / "research/traces/patches.toml")
    assert {"tick-game", "humans-update", "state-code", "call-brains"} <= set(known)
    calls = [hook for hook in known.values() if hook.call]
    others = [hook for hook in known.values() if not hook.call]
    # Hooks of different passes may share an address (the event hooks and the hint ones); each pass builds alone.
    events = [hook for hook in others if hook.name.startswith("event-")]
    build([hook for hook in others if hook not in events] + [calls[0]])
    build(events)


def test_a_followed_field_reads_through_the_pointer_of_the_moment(tmp_path: Path) -> None:
    extra = """
[[field]]
name = "top"
address = "u32(rec(player) + 0x40) + 0x4"
type = "u32"
follow = true
"""
    scenario = load_scenario(_repo(tmp_path, "", extra), tmp_path)
    game = FakeGame()
    record = RECORD + PLAYER_INDEX * 0x180
    game.write(record + 0x40, 4, 0x00400000)
    game.write(0x00400004, 4, 11)
    game.write(0x00500004, 4, 22)
    recorder = Recorder(GameMemory(MemoryOf(game)), scenario)
    bound, reads = recorder.resolve()
    assert reads[-1][1] is None
    assert recorder._sample(reads, [0, 0], bound)[-1] == 11
    game.write(record + 0x40, 4, 0x00500000)
    assert recorder._sample(reads, [0, 0], bound)[-1] == 22


def test_a_call_is_written_arguments_first(tmp_path: Path) -> None:
    extra = """
[original]
calls = [{ function = "0x002b2b90", args = ["u32(player + 0x90)", "7"], frame = 2 }]
"""
    scenario = load_scenario(_repo(tmp_path, "", extra), tmp_path)
    call_hook = parse_hook("call", {"address": 0x00293B28, "original": [0, 0], "call": True}, "t")
    game = FakeGame()
    recorder = Recorder(GameMemory(MemoryOf(game)), scenario, hooks=[call_hook])
    assert recorder._call_writes(1, {}) == []
    writes = recorder._call_writes(2, {})
    assert [(w.address, w.value) for w in writes] == [
        (CALL_BASE + 4, game.read(PLAYER + 0x90, 4)),
        (CALL_BASE + 8, 7),
        (CALL_BASE, 0x002B2B90),
    ]


def test_a_float_register_is_moved_with_mfc1_and_logged_as_f32() -> None:
    hook = parse_hook("speed", {"address": 0x00241300, "original": [0, 0], "log": ["f28", "f0"]}, "t")
    assert [item.type for item in hook.log] == ["f32", "f32"]
    code = hook.cave(0x000A0000, 1)
    # mfc1 t9,f28 then the store; mfc1 t9,f0.
    assert code[12:14] == [0x4419E000, 0xAF190014]
    assert code[14] == 0x44190000


class _HookedGame(FakeGame):
    """A FakeGame whose character updates take two messages, as the original's pair of ticks does: on the first the
    count turns even and a hook in `Humans_Update` logs the update's number to the ring (hook 1); on the second the
    update finishes (its fields and the odd count), so a poll can see the entry before the update's sample is due."""

    def tick(self) -> None:
        """Start the update due on the next message, then let FakeGame count this one (and finish an update)."""
        if (self.messages + 2) % self.polls_per_update == 0:
            number = self.updates + 1
            self.write32(TICK_COUNTER, 2 * number)
            count = self.read(RING_BASE, 4)
            entry = RING_BASE + 0x10 + (count % hooks.RING_ENTRIES) * 32
            self.write32(entry, 1)
            self.write32(entry + 4, number)
            self.write32(RING_BASE, count + 1)
        super().tick()


class _TickingMemory(MemoryOf):
    """MemoryOf with the game running between a message's writes and its reads."""

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """Apply the writes, let the game run, then answer the reads."""
        for write in writes:
            self.game.write(write.address, write.size, write.value)
        self.game.tick()
        return [self.game.read(read.address, read.size) for read in reads]


@pytest.mark.parametrize("polls_per_update", [2, 3])
def test_a_hook_entry_carries_the_step_of_the_update_it_was_made_in(tmp_path: Path, polls_per_update: int) -> None:
    scenario = load_scenario(_repo(tmp_path, ""), tmp_path)
    hook = parse_hook("update", {"address": 0x00249108, "original": [0, 0], "log": ["a0"]}, "t")
    game = _HookedGame(polls_per_update)
    recording = Recorder(GameMemory(_TickingMemory(game)), scenario, hooks=[hook]).record()
    # The fake game counts its updates in the clip column, so each row says which update its step shows.
    step_of_update = {int(row[2]): int(row[0]) for row in recording.rows}
    assert recording.hooks is not None
    entries = recording.hooks.entries["update"]
    tagged = [(step, values[0]) for _, step, values in entries if values[0] in step_of_update]
    assert len(tagged) >= scenario.updates
    assert all(step == step_of_update[int(number)] for step, number in tagged)
    assert entry_step(2 * 7, 1) == entry_step(2 * 7 + 1, 1) == 7
