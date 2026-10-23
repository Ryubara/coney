# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.lua_index: the per-script index `lua decompile --all` writes beside the decompiled scripts.

The chunk is our own made-up mission script (`lua_index_fixtures/mission.lua`), compiled with a stock Lua 4.0.1
`luac -s`; the "natives" are a set the tests name, not the game's bindings.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

import lua4
from coney_tools import lua4 as coney_lua4
from coney_tools import lua_decompile, lua_index

FIXTURES = Path(__file__).parent / "lua_index_fixtures"
NATIVES = frozenset(
    {"HudShowText", "CountThings", "ScheduleFunc", "MissionEnd", "SetMsgHandler", "RegisterUpdate", "PadSetHandler"}
)


def _script() -> lua_index.Script:
    return lua_index.index_script("mission.lua", (FIXTURES / "mission.s.luac").read_bytes(), NATIVES)


def test_functions_are_named_by_the_global_or_table_field_that_holds_them() -> None:
    """Each function gets the name it is stored under, its path among the chunk's functions and its source line."""
    script = _script()
    named = {entry.path: entry.name for entry in script.functions}
    assert named == {
        "main": "(main chunk)",
        "main/0": "OnEnter",
        "main/1": "Update",
        "main/2": "Finish",
        "main/3": "Mission.Setup",
        "main/3/0": None,
        "main/4": "LocalHelper",
    }
    lines = {entry.path: entry.line for entry in script.functions}
    assert lines["main/0"] == 3 and lines["main/3"] == 18 and lines["main/3/0"] == 21


def test_the_index_reads_each_chunk_once(monkeypatch: pytest.MonkeyPatch) -> None:
    """The index takes everything, the first lines included, from one reader (`coney_tools.lua4`), not a second parse
    by the decompiler's loader; that reader keeps each function's first line."""

    def refuse(data: bytes) -> object:
        raise AssertionError("the index parsed the chunk a second time")

    monkeypatch.setattr(lua4, "load", refuse)
    lines = {entry.path: entry.line for entry in _script().functions}
    assert lines["main"] == 0 and lines["main/3"] == 18 and lines["main/3/0"] == 21
    main = coney_lua4.parse_chunk((FIXTURES / "mission.s.luac").read_bytes())
    assert main.line_defined == 0 and main.protos[3].line_defined == 18


def test_natives_and_the_scripts_own_calls_are_listed_per_function() -> None:
    """A function's natives are the callees in the binding list, in first-call order; other callees are calls."""
    by_path = {entry.path: entry for entry in _script().functions}
    assert by_path["main/1"].natives == ["CountThings", "ScheduleFunc"]
    assert by_path["main/3"].natives == ["SetMsgHandler", "RegisterUpdate", "PadSetHandler"]
    assert by_path["main/3"].calls == ["LocalHelper"]
    assert by_path["main/3/0"].calls == ["Finish"]
    assert by_path["main/4"].natives == [] and by_path["main/4"].calls == []


def test_handler_registrations_keep_their_constant_arguments() -> None:
    """A native that registers a handler, a callback, an update or a scheduled call is listed with its arguments;
    a function passed as an argument is named by its path."""
    by_path = {entry.path: entry for entry in _script().functions}
    assert by_path["main/1"].handlers == [lua_index.Registration("ScheduleFunc", [1.5, "Finish"])]
    assert by_path["main/3"].handlers == [
        lua_index.Registration("SetMsgHandler", ["MSG_ENTER", "OnEnter"]),
        lua_index.Registration("RegisterUpdate", [{"global": "Update"}]),
        lua_index.Registration("PadSetHandler", [0, {"function": "main/3/0"}]),
    ]
    assert by_path["main/0"].handlers == []


@pytest.mark.parametrize(
    ("name", "registers"),
    [
        ("SetMsgHandler", True),
        ("GangClearHandlers", True),
        ("HuSetWorkoutCallbacks", True),
        ("RegisterUpdate", True),
        ("ScheduleFuncArg", True),
        ("TriggerSphereCfg", True),
        ("HudShowText", False),
        ("CallbackCount", False),
    ],
)
def test_which_natives_register_a_handler(name: str, registers: bool) -> None:
    """The names that register code to run later (lua_index.HANDLER)."""
    assert bool(lua_index.HANDLER.search(name)) is registers


def test_the_index_is_written_as_json_and_markdown(tmp_path: Path) -> None:
    """index.json holds every script's functions; index.md lists them for reading, with the registrations."""
    lua_index.write_index(tmp_path, [_script()], source="2 chunks")
    data = json.loads((tmp_path / lua_index.INDEX_JSON).read_text(encoding="utf-8"))
    functions = data["scripts"]["mission.lua"]["functions"]
    assert functions[4] == {
        "path": "main/3",
        "name": "Mission.Setup",
        "line": 18,
        "natives": ["SetMsgHandler", "RegisterUpdate", "PadSetHandler"],
        "calls": ["LocalHelper"],
        "handlers": [
            {"native": "SetMsgHandler", "args": ["MSG_ENTER", "OnEnter"]},
            {"native": "RegisterUpdate", "args": [{"global": "Update"}]},
            {"native": "PadSetHandler", "args": [0, {"function": "main/3/0"}]},
        ],
    }
    text = (tmp_path / lua_index.INDEX_MD).read_text(encoding="utf-8")
    assert "## mission.lua" in text
    assert '`SetMsgHandler("MSG_ENTER", "OnEnter")`' in text
    assert "`Mission.Setup` (main/3, line 18)" in text


def test_the_index_never_carries_the_guards_marker(tmp_path: Path) -> None:
    """The index names functions and constants; it is not a decompile, so it carries no marker line."""
    lua_index.write_index(tmp_path, [_script()], source="1 chunk")
    guard = lua_decompile.guard()
    for name in (lua_index.INDEX_JSON, lua_index.INDEX_MD):
        assert not guard.MARKER.search((tmp_path / name).read_text(encoding="utf-8"))


def test_the_fixture_is_what_luac_makes_of_its_source() -> None:
    """When a luac is found, the committed chunk is the one its source compiles to (instructions and constants)."""
    luac = lua_decompile.luac_program(lua_decompile.REPO, required=False)
    if luac is None:
        pytest.skip("no Lua 4.0 luac (coney.local.toml's luac_program)")
    again = lua4.compile_source((FIXTURES / "mission.lua").read_bytes(), luac, strip=True)
    assert lua4.compare(lua4.load((FIXTURES / "mission.s.luac").read_bytes()), lua4.load(again)) == []
