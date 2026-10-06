# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the first mission's binding reader, on hand-assembled Lua 4.0 chunks with invented names."""

import struct

from coney_tools import lua4, natives_mission

_MAXARG_S = ((1 << 26) - 1) >> 1


def _op(name: str, u: int = 0, a: int | None = None, b: int = 0) -> int:
    """One instruction: the opcode in the low 6 bits, then U (or A and B)."""
    code = lua4.OPCODES.index(name)
    if a is not None:
        return code | (b << 6) | (a << 15)
    return code | (u << 6)


def _string(text: str) -> bytes:
    """A Lua 4.0 string: a length counting the trailing NUL."""
    raw = text.encode("latin-1") + b"\0"
    return struct.pack("<I", len(raw)) + raw


def _proto(strings: list[str], code: list[int], children: list[bytes] | None = None) -> bytes:
    """One function prototype (no parameters, locals or numbers) with the given child prototypes."""
    kids = children or []
    body = struct.pack("<I", 0) + struct.pack("<iiBi", 0, 0, 0, 8)
    body += struct.pack("<ii", 0, 0)
    body += struct.pack("<i", len(strings)) + b"".join(_string(s) for s in strings)
    body += struct.pack("<ii", 0, len(kids)) + b"".join(kids)
    return body + struct.pack("<i", len(code)) + b"".join(struct.pack("<I", word) for word in code)


def _chunk(main: bytes) -> lua4.Proto:
    """Parse a chunk whose main function is `main`."""
    return lua4.parse_chunk(lua4.HEADER + struct.pack("<d", 3.14159265358979e8) + main)


def _calls(name: str) -> bytes:
    """A function whose body is `name()`."""
    return _proto([name], [_op("GETGLOBAL", 0), _op("CALL", a=0, b=0), _op("END")])


# global.lua: function Helper() BindB() end; function Unused() BindC() end; function UnlockAndLoad() BindD() end;
# Menu = {cb = function() BindE() end}
HELPERS = _chunk(
    _proto(
        ["Helper", "Unused", "UnlockAndLoad", "cb", "Menu"],
        [
            _op("CLOSURE", a=0, b=0),
            _op("SETGLOBAL", 0),
            _op("CLOSURE", a=1, b=0),
            _op("SETGLOBAL", 1),
            _op("CLOSURE", a=2, b=0),
            _op("SETGLOBAL", 2),
            _op("CREATETABLE", 1),
            _op("PUSHSTRING", 3),
            _op("CLOSURE", a=3, b=0),
            _op("SETMAP", 1),
            _op("SETGLOBAL", 4),
            _op("END"),
        ],
        [_calls("BindB"), _calls("BindC"), _calls("BindD"), _calls("BindE")],
    )
)

# The mission: BindA(); Helper(); ScheduleFunc("Menu.cb", 10)
MISSION = _chunk(
    _proto(
        ["BindA", "Helper", "ScheduleFunc", "Menu.cb"],
        [
            _op("GETGLOBAL", 0),
            _op("CALL", a=0, b=0),
            _op("GETGLOBAL", 1),
            _op("CALL", a=0, b=0),
            _op("GETGLOBAL", 2),
            _op("PUSHSTRING", 3),
            _op("PUSHINT", 10 + _MAXARG_S),
            _op("CALL", a=0, b=0),
            _op("END"),
        ],
    )
)

BINDINGS = ("BindA", "BindB", "BindC", "BindD", "BindE", "ScheduleFunc")


def test_names_in_reads_globals_dotted_fields_and_strings() -> None:
    proto = _chunk(_proto(["Menu", "cb", "say"], [_op("GETGLOBAL", 0), _op("GETDOTTED", 1), _op("END")]))
    assert natives_mission.names_in(proto) == {"Menu", "Menu.cb", "cb", "say"}


def test_helper_functions_name_globals_and_table_fields() -> None:
    helpers = natives_mission.helper_functions(lua4.walk_chunk(HELPERS))
    assert helpers == {"Helper": "main/0", "Unused": "main/1", "UnlockAndLoad": "main/2", "Menu.cb": "main/3"}


def test_reached_follows_helpers_callbacks_and_engine_roots() -> None:
    names = natives_mission.reached_names([MISSION], HELPERS)
    assert natives_mission.mission_bindings(names, BINDINGS) == {"BindA", "BindB", "BindD", "BindE", "ScheduleFunc"}
    names = natives_mission.reached_names([MISSION], HELPERS, roots=())
    assert "BindD" not in natives_mission.mission_bindings(names, BINDINGS)


def test_set_mission1_changes_only_the_marker() -> None:
    text = (
        "- name: BindA  # first\n"
        "  usage: {chunks: 1, calls: 2, boot: false, mission1: false, result_used: true}\n"
        "- name: BindC\n"
        "  usage: {chunks: 1, calls: 1, boot: true, mission1: true, result_used: false}\n"
    )
    assert natives_mission.set_mission1(text, {"BindA"}) == (
        "- name: BindA  # first\n"
        "  usage: {chunks: 1, calls: 2, boot: false, mission1: true, result_used: true}\n"
        "- name: BindC\n"
        "  usage: {chunks: 1, calls: 1, boot: true, mission1: false, result_used: false}\n"
    )


def test_scripts_from_needs_every_chunk() -> None:
    raw = lua4.HEADER + struct.pack("<d", 3.14159265358979e8) + _calls("BindA")
    chunks = {name: raw for name in (*natives_mission.MISSION1_SCRIPTS, natives_mission.HELPERS_SCRIPT)}
    scripts, helpers = natives_mission.scripts_from(chunks)
    assert len(scripts) == 4
    assert natives_mission.names_in(helpers) == {"BindA"}


def test_level_scripts_keep_chapters_and_drop_strings_and_scene_tests() -> None:
    names = ["level8.lua", "level80.lua", "level80_chapter1.lua", "level80_strings_en.lua", "level80_scenetest.lua"]
    assert natives_mission.level_scripts(80, [*names, "level800.lua", "global.lua"]) == [
        "level80.lua",
        "level80_chapter1.lua",
    ]
    assert natives_mission.level_scripts(8, names) == ["level8.lua"]


def test_set_levels_adds_replaces_and_drops_the_list() -> None:
    text = (
        "- name: BindA\n"
        "  usage: {chunks: 1, calls: 2, boot: false, mission1: false, result_used: true}\n"
        "- name: BindB\n"
        "  usage: {chunks: 1, calls: 1, boot: true, mission1: true, result_used: false, levels: [80]}\n"
        "  coney: implemented\n"
    )
    assert natives_mission.set_levels(text, {"BindA": [80, 95]}) == (
        "- name: BindA\n"
        "  usage: {chunks: 1, calls: 2, boot: false, mission1: false, result_used: true, levels: [80, 95]}\n"
        "- name: BindB\n"
        "  usage: {chunks: 1, calls: 1, boot: true, mission1: true, result_used: false}\n"
        "  coney: implemented\n"
    )
