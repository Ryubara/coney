# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the engine reference lists' readers (camera types and switches, screen effects, AI goal types) on
synthetic executable words and script calls."""

from __future__ import annotations

import pytest

from coney_tools import lua4, refs_engine, refs_extract
from coney_tools.refs_topics import TOPICS

JR_RA = refs_engine.JR_RA


def _li_v0(n: int) -> int:
    """`addiu v0, zero, n`."""
    return 0x24020000 | (n & 0xFFFF)


def _lui(reg: int, high: int) -> int:
    """`lui reg, high`."""
    return 0x3C000000 | (reg << 16) | high


def _addiu(reg: int, low: int) -> int:
    """`addiu reg, reg, low`."""
    return 0x24000000 | (reg << 21) | (reg << 16) | (low & 0xFFFF)


def _jal(target: int) -> int:
    """`jal target`."""
    return 0x0C000000 | (target >> 2)


class Memory:
    """Synthetic executable memory: words and strings by address; anything else is outside the sections."""

    def __init__(self) -> None:
        self.words: dict[int, int] = {}
        self.strings: dict[int, str] = {}

    def put(self, address: int, *words: int) -> None:
        """Store consecutive words from `address`."""
        for index, value in enumerate(words):
            self.words[address + 4 * index] = value

    def word(self, address: int) -> int | None:
        """A stored word, 0 inside the data range, else None."""
        if address in self.words:
            return self.words[address]
        return 0 if 0x00500000 <= address < 0x00600000 else None

    def text(self, address: int) -> str | None:
        """A stored string, or None."""
        return self.strings.get(address)


def _call(callee: str, *args: lua4.Value) -> lua4.Call:
    """One script call."""
    return lua4.Call("main", 0, callee, list(args))


def _scripts(**calls: list[lua4.Call]) -> dict[str, lua4.ChunkFacts]:
    """Walked scripts with these calls, by name (`level1` -> `level1.lua`)."""
    return {f"{name}.lua": lua4.ChunkFacts(calls=list(chunk)) for name, chunk in calls.items()}


def test_return_constant_and_address() -> None:
    """The two- and three-instruction getters are recognised in both orders; anything else is None."""
    memory = Memory()
    memory.put(0x00100000, JR_RA, _li_v0(7))
    memory.put(0x00100010, _li_v0(-1), JR_RA)
    memory.put(0x00100020, JR_RA, 0x0000102D)
    memory.put(0x00100030, 0, JR_RA)
    memory.put(0x00100040, _lui(2, 0x56), JR_RA, 0x24426510)
    assert refs_engine.return_constant(memory.word, 0x00100000) == 7
    assert refs_engine.return_constant(memory.word, 0x00100010) == -1
    assert refs_engine.return_constant(memory.word, 0x00100020) == 0
    assert refs_engine.return_constant(memory.word, 0x00100030) is None
    assert refs_engine.return_constant(memory.word, 0x00400000) is None
    assert refs_engine.return_address(memory.word, 0x00100040) == 0x00566510
    assert refs_engine.return_address(memory.word, 0x00100000) is None


def test_referenced_follows_calls_until_a_hit() -> None:
    """A function that builds no wanted address is searched through its callees; the first level with a hit wins."""
    memory = Memory()
    wanted = {0x00542130, 0x00540450}
    # top -> helper (builds 0x00542130) and -> other (calls deeper, which builds 0x00540450)
    memory.put(0x00100000, _jal(0x00100100), 0, _jal(0x00100200), 0, JR_RA, 0)
    memory.put(0x00100100, _lui(4, 0x0054), _addiu(4, 0x2130), JR_RA, 0)
    memory.put(0x00100200, _jal(0x00100300), 0, JR_RA, 0)
    memory.put(0x00100300, _lui(5, 0x0054), _addiu(5, 0x0450), JR_RA, 0)
    assert refs_engine.referenced(memory.word, 0x00100100, wanted) == {0x00542130}
    assert refs_engine.referenced(memory.word, 0x00100000, wanted) == {0x00542130, 0x00540450}
    assert refs_engine.referenced(memory.word, 0x00100000, wanted, depth=1) == {0x00542130}


def _goal_memory() -> Memory:
    """Two goal vtables (types 1 and 15, one with its own event handler), a binding that makes the first, and a
    look-alike with a lower-case name that is not a goal."""
    memory = Memory()
    for vtable, number, name_at, name, event in (
        (0x00542130, 1, 0x00566510, "MoveToFlag", refs_engine.GOAL_DEFAULT_EVENT),
        (0x005402D0, 15, 0x00566600, "Fight", 0x002B7BC0),
        (0x00542200, 3, 0x00566700, "notAGoal", 0),
    ):
        getter = 0x00200000 + number * 0x20
        memory.put(getter, JR_RA, _li_v0(number), _lui(2, name_at >> 16), JR_RA, _addiu(2, name_at & 0xFFFF))
        memory.put(vtable + refs_engine.GOAL_TYPE_SLOT, getter)
        memory.put(vtable + refs_engine.GOAL_NAME_SLOT, getter + 8)
        memory.put(vtable + refs_engine.GOAL_PROCESS_SLOT, 0x002DA588 + number)
        memory.put(vtable + refs_engine.GOAL_EVENT_SLOT, event)
        memory.strings[name_at] = name
    memory.put(0x00100000, _lui(4, 0x0054), _addiu(4, 0x2130), JR_RA, 0)
    return memory


def test_goal_types() -> None:
    """Goal vtables are found by shape and named by themselves; bindings and their script calls are attached."""
    memory = _goal_memory()
    vtables = refs_engine.goal_vtables(memory.word, memory.text, 0x00540000, 0x00543000)
    assert vtables == {0x005402D0: (15, "Fight"), 0x00542130: (1, "MoveToFlag")}
    scripts = _scripts(level1=[_call("GoalMoveToFlag", 1.0), _call("GoalMoveToFlag", 2.0)], level2=[_call("BrFlush")])
    entries = refs_engine.goal_types(
        memory.word, memory.text, (0x00540000, 0x00543000), {"GoalMoveToFlag": [0x00100000], "BrFlush": []}, scripts
    )
    assert [entry["id"] for entry in entries] == [1, 15]
    first, fight = entries
    assert first["name"] == "MoveToFlag" and first["bindings"] == ["GoalMoveToFlag"]
    assert first["calls"] == 2 and first["scripts"] == ["level1.lua"] and first["event"] is None
    assert fight["bindings"] is None and fight["event"] == 0x002B7BC0 and fight["process"] == 0x002DA588 + 15


def test_camera_types_check_the_class_type() -> None:
    """Each class's type comes from its vtable; a vtable that disagrees with the table is an error."""
    memory = Memory()
    for number, (vtable, tag, _made, _size, _bindings) in refs_engine.CAMERA_CLASSES.items():
        getter = 0x00300000 + number * 8
        memory.put(getter, JR_RA, _li_v0(number))
        memory.put(vtable + refs_engine.CAMERA_TYPE_SLOT, getter)
        memory.strings[tag] = f"Cam_{number}"
    scripts = _scripts(level1=[_call("CameraCreateLocked", "c"), _call("CameraCreateLocked", "d")])
    entries = refs_engine.camera_types(memory.word, memory.text, scripts)
    assert [entry["number"] for entry in entries] == list(refs_engine.CAMERA_CLASSES)
    locked = entries[1]
    assert locked["id"] == "type-1" and locked["class"] == "Cam_1" and locked["calls"] == 2
    memory.put(0x00300000 + 8, JR_RA, _li_v0(9))
    with pytest.raises(ValueError, match="gives type 9, not 1"):
        refs_engine.camera_types(memory.word, memory.text, scripts)


def test_camera_switches_count_literal_uses() -> None:
    """Every switch is listed; calls are counted per literal switch number."""
    true = lua4.Global("true")
    scripts = _scripts(
        level1=[_call("CamEnable", 3.0, true), _call("CamEnable", 3.0, true), _call("CamEnable", lua4.Global("X"))],
        level2=[_call("CamEnable", 5.0, true, 0.0)],
    )
    entries = {entry["number"]: entry for entry in refs_engine.camera_switches(scripts)}
    assert sorted(entries) == list(range(14))
    assert entries[3]["calls"] == 2 and entries[3]["scripts"] == ["level1.lua"]
    assert entries[5]["scripts"] == ["level2.lua"] and entries[0]["calls"] is None
    assert entries[1]["default"] == 0 and entries[0]["scope"] == "per player"


def test_screen_effects() -> None:
    """Queue handlers come from the jump table, looks from `CfgScrFx` (both forms), layers from their tags."""
    memory = Memory()
    memory.put(refs_engine.QUEUE_TABLE, *(0x0018D49C + 4 * i for i in range(6)))
    for tag, _bindings in refs_engine.LAYERS.values():
        memory.strings[tag] = f"OE_{tag:x}"
    colour = lua4.Table(items={1: 51.0, 2: 25.0, 3: 0.0, 4: 80.0})
    scripts = _scripts(
        config_preload2=[
            _call("CfgScrFx", 2.0, colour, 1000.0, 5000.0, 150.0, 20000.0, 5000.0),
            _call("CfgScrFx", 5.0, 1250.0, 4000.0, 6000.0, 28.0, 0.002, 0.003),
            _call("CfgScrFx", 9.0, colour),  # too short: ignored
        ],
        level1=[_call("ScreenQueueEffect", 1.0, 0.5), _call("StartRain", 1.0), _call("EndRain")],
    )
    entries = {entry["id"]: entry for entry in refs_engine.screen_effects(memory.word, memory.text, scripts)}
    assert len(entries) == refs_engine.QUEUE_EFFECTS + refs_engine.LOOK_COUNT + len(refs_engine.LAYERS)
    assert entries["queue-1"]["handler"] == 0x0018D4A0 and entries["queue-1"]["calls"] == 1
    assert entries["look-2"]["colour"] == [51, 25, 0, 80] and entries["look-2"]["hold_ms"] == 20000
    assert entries["look-5"]["strength"] == 28 and entries["look-5"]["factors"] == [0.002, 0.003]
    assert "colour" not in entries["look-9"] and entries["look-9"]["set_by"]
    assert entries["layer-0"]["class"] == f"OE_{refs_engine.LAYERS[0][0]:x}" and entries["layer-0"]["calls"] == 2


def test_topics_are_registered() -> None:
    """The three lists have topics and extractors."""
    keys = {item.key for item in TOPICS}
    for key in ("cameras", "screen-effects", "goal-types"):
        assert key in keys and key in refs_extract.EXTRACTORS
