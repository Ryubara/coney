# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the environment reference lists' readers: glass types, doors, tints, lights, spawner states, crimes.

Synthetic script facts and synthetic executable bytes only; no game data.
"""

from __future__ import annotations

import struct

from coney_tools import refs_env
from coney_tools.lua4 import Assignment, Call, CallResult, ChunkFacts, Global, Table


def _call(callee: str, *args: object) -> Call:
    """A call in a script's main function."""
    return Call("main", 0, callee, list(args))  # type: ignore[arg-type]


def _vec(*values: float) -> Table:
    """A literal list table `{values...}`."""
    return Table(items={i + 1: float(v) for i, v in enumerate(values)})


def test_glass_types_read_the_config_and_count_panes() -> None:
    """Each configured type keeps its flags and sprite words; panes are counted per type and script."""
    config = ChunkFacts(
        calls=[
            _call("CfgSetGlassProperties", 0.0, Global("FALSE"), Global("TRUE"), 65556.0, 65557.0),
            _call("CfgSetGlassProperties", 1.0, Global("TRUE"), Global("FALSE"), 65558.0, 65559.0),
        ]
    )
    level = ChunkFacts(calls=[_call("SpawnBreakableGlass", 1.0), _call("SpawnBreakableGlass", 1.0)])
    entries = refs_env.glass_types({"config_preload2.lua": config, "level9.lua": level})
    assert [e["id"] for e in entries] == [0, 1]
    first, second = entries
    assert first == {
        "id": 0, "window_link": False, "alarm": True, "sprite": 0x10014, "broken_sprite": 0x10015,
        "panes": None, "scripts": None,
    }  # fmt: skip
    assert second["window_link"] is True and second["panes"] == 2 and second["scripts"] == ["level9"]


def test_doors_take_level_number_and_marking_from_the_class() -> None:
    """A door's key is its level and number; a breakable class or type marks its links."""
    config = ChunkFacts(
        calls=[_call("CfgObj", "dyn_door_a", Global("dyn_door_fence")), _call("CfgObj", "dyn_door_b", "x")]
    )
    level = ChunkFacts(
        calls=[
            _call("SpawnDoor", "dyn_door_b", _vec(1, 2, 3), _vec(0, 0, 0, 1), _vec(10, 11), 2.0),
            _call("SpawnDoor", "dyn_door_a", _vec(4.5, 5, 6), _vec(0, 0, 0, 1), _vec(12, 13), 1.0),
            _call("SpawnDoor", "dyn_door_liz", _vec(0, 0, 0), _vec(0, 0, 0, 1), _vec(1, 2), 3.0),
        ]
    )
    entries = refs_env.doors({"config_preload3.lua": config, "level11_chapter1.lua": level})
    assert [e["id"] for e in entries] == ["level11:1", "level11:2", "level11:3"]
    assert (
        entries[0]["marks_links"] is True and entries[0]["pos"] == [4.5, 5, 6] and entries[0]["triangles"] == [12, 13]
    )
    assert entries[1]["marks_links"] is None and entries[1]["script"] == "level11_chapter1"
    assert entries[2]["marks_links"] is True


def test_colour_byte_keeps_whole_components() -> None:
    """Two multiplications by 255 keep a whole 0-255 component and wreck a 0-1 one."""
    assert [refs_env.colour_byte(v) for v in (0.0, 100.0, 125.0, 255.0)] == [0, 100, 125, 255]
    assert refs_env.colour_byte(1.0) == 1


def test_tints_list_spawn_words_colours_and_the_icon_default() -> None:
    """Spawn tints skip the default and are grouped by word; ObjColor tables are packed as the game packs them."""
    level = ChunkFacts(
        calls=[
            _call("ObjSpawn", "dyn_a", None, None, -1.0, 0.0, 0.0, float(0xA49E98FF)),
            _call("ObjSpawn", "dyn_a", None, None, -1.0, 0.0, 0.0, float(0xA49E98FF)),
            _call("ObjSpawn", "dyn_b", None, None, -1.0, 0.0, 0.0, float(0xFFFFFFFF)),
            _call("ObjColor", Global("h"), _vec(255, 100, 100, 125)),
        ]
    )
    entries = {e["id"]: e for e in refs_env.tints({"level2.lua": level})}
    assert list(entries) == ["spawn:A49E98FF", "color:FF64647D", "icon:default"]
    assert entries["spawn:A49E98FF"]["calls"] == 2 and entries["spawn:A49E98FF"]["objects"] == ["dyn_a"]
    assert entries["color:FF64647D"]["swatch"] == "FF64647D" and entries["icon:default"]["rgba"] == "FFFFFF00"


def test_lights_are_numbered_per_script_and_named_from_the_lights_table() -> None:
    """Long-form calls only; a call whose arguments a `Lights` key holds takes that key."""
    lamp = (0.0, 0.0, _vec(1, 2, 3), _vec(0, 0, 0), _vec(1, 0.5, 0, 1), 12.0, 0.0, 0.0, 0.5, 3.2, 1.0, 0.0, 4.0, 1.0)
    other = (0.0, 2.0, _vec(0, 0, 0), _vec(0, 0, -1), _vec(0, 0, 1, 1), 0.0, 1.0, 0.0, 0.0, 0.0, 3.0, 2.0, 0.0, 1.0)
    level = ChunkFacts(
        calls=[_call("SetLight", 1.0, 0.0), _call("SetLight", *lamp), _call("SetLight", *other)],
        assignments=[Assignment("main", 0, "Lights", Table(fields={"ania01": CallResult("SetLight", lamp)}))],
    )
    first, second = refs_env.lights({"level101.lua": level})
    assert first["id"] == "level101:1" and first["name"] == "ania01" and first["type"] == "point"
    assert first["swatch"] == "FF8000FF" and first["flicker"] == 4 and first["flicker_params"] == [0, 0.5, 3.2]
    assert second["id"] == "level101:2" and second["name"] is None and second["type"] == "directional"
    assert second["priority"] == 2 and second["flicker_params"] is None and second["level"] == "level101"


def test_spawner_states_count_both_bindings() -> None:
    """Every state is listed; the kind and mode positions are counted separately."""
    add = [None] * 10 + [4.0]
    level = ChunkFacts(calls=[_call("GangAddSpawner", *add), _call("GangStartSpawner", 0.0, "s", 7.0, -1.0)])
    entries = refs_env.spawner_states({"level5.lua": level})
    assert len(entries) == 12 and entries[6]["settable"] is False
    assert entries[4]["added"] == 1 and entries[4]["started"] is None
    assert entries[7]["started"] == 1 and entries[7]["scripts"] == ["level5"]


def test_crime_names_decode_the_switch() -> None:
    """Each jump-table case is `lui; jr; addiu` returning a string's address."""
    memory: dict[int, bytes] = {}
    targets = [0x00400000 + 16 * i for i in range(refs_env.CRIME_TYPES_COUNT)]
    memory[refs_env.CRIME_NAME_JUMPS] = struct.pack(f"<{len(targets)}I", *targets)
    for i, target in enumerate(targets):
        address = 0x0058B010 + 32 * i
        hi, lo = (address + 0x8000) >> 16, address & 0xFFFF
        memory[target] = struct.pack("<3I", 0x3C020000 | hi, 0x03E00008, 0x24420000 | lo)
        memory[address] = f"Crime{i}".encode() + b"\0" * 26

    def read(address: int, size: int) -> bytes:
        """The synthetic executable's bytes at an address."""
        return memory[address][:size]

    names = refs_env.crime_names(read)
    assert names[0] == "Crime0" and names[14] == "Crime14"


def test_crime_types_gather_the_scripts_uses() -> None:
    """Responders, switches and reports per type; SpawnCustomCrime reports type 4."""
    level = ChunkFacts(
        calls=[
            _call("CfgCrimeResponders", 2.0, 3.0),
            _call("CfgEnableCrimeType", 12.0, Global("false")),
            _call("CrimeIsHappening", None, 7.0),
            _call("SpawnCustomCrime", None),
        ]
    )
    entries = refs_env.crime_types({"level9.lua": level}, [f"n{i}" for i in range(15)])
    assert len(entries) == 15 and entries[2]["responders"] == ["level9: 3"]
    assert entries[12]["enabled"] == ["level9: off"] and entries[7]["reported"] == 1 and entries[4]["reported"] == 1
    assert entries[0]["name"] == "n0" and entries[3]["scripts"] is None
