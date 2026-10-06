# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the world reference lists' readers (flags, zones, boxes, scenes and movies) on synthetic chunks."""

from __future__ import annotations

import struct
from pathlib import Path

import pytest

from coney_tools import lua4, refs, refs_cli, refs_render, refs_world
from coney_tools.cli import main
from coney_tools.refs import Field, RefList, RefsError, Topic
from coney_tools.refs_topics import TOPICS

_MAXARG_S = ((1 << 26) - 1) >> 1


def _op(name: str, u: int = 0, a: int | None = None, b: int = 0) -> int:
    """One instruction: the opcode in the low 6 bits, then U (or A and B)."""
    code = lua4.OPCODES.index(name)
    return code | (b << 6) | (a << 15) if a is not None else code | (u << 6)


def _int(value: int) -> int:
    """`PUSHINT value`."""
    return _op("PUSHINT", value + _MAXARG_S)


def _string(text: str | None) -> bytes:
    """A Lua 4.0 string: a length counting the trailing NUL, 0 for none."""
    if text is None:
        return struct.pack("<I", 0)
    raw = text.encode("latin-1") + b"\0"
    return struct.pack("<I", len(raw)) + raw


def _facts(strings: list[str], code: list[int]) -> lua4.ChunkFacts:
    """Walk a chunk whose main function has these strings and this code."""
    body = _string(None) + struct.pack("<iiBi", 0, 0, 0, 8) + struct.pack("<ii", 0, 0)
    body += struct.pack("<i", len(strings)) + b"".join(_string(s) for s in strings)
    body += struct.pack("<ii", 0, 0) + struct.pack("<i", len(code)) + b"".join(struct.pack("<I", w) for w in code)
    return lua4.walk_chunk(lua4.parse_chunk(lua4.HEADER + struct.pack("<d", 3.14159265358979e8) + body))


def _table(*values: int) -> list[int]:
    """Code that leaves `{values...}` on the stack."""
    return [_op("CREATETABLE", len(values)), *(_int(v) for v in values), _op("SETLIST", a=0, b=len(values))]


# AddFlag("f1", {1, 2, 0}, 90, 5, 2) twice; AddFlag("f2", {3, 4, 0}, 0, 0, 0)
FLAGS = _facts(
    ["AddFlag", "f1", "f2"],
    [
        *[
            word
            for name, pos, heading, activity, group in (
                (1, (1, 2, 0), 90, 5, 2),
                (1, (1, 2, 0), 90, 5, 2),
                (2, (3, 4, 0), 0, 0, 0),
            )
            for word in (
                _op("GETGLOBAL", 0),
                _op("PUSHSTRING", name),
                *_table(*pos),
                _int(heading),
                _int(activity),
                _int(group),
                _op("CALL", a=0, b=0),
            )
        ],
        _op("END"),
    ],
)

# Zone4 = 4; ObjEnableZone(Zone4, true); vTurf = AddVolumeBox("vTurf", 3, {1, 2, 3}, {4, 5, 6});
# GangAddTurfBox(GangA, vTurf); ObjSpawn("x", nil, nil, -1, Zone4)
LEVEL = _facts(
    ["Zone4", "ObjEnableZone", "true", "vTurf", "AddVolumeBox", "GangAddTurfBox", "GangA", "ObjSpawn", "x"],
    [
        _int(4),
        _op("SETGLOBAL", 0),
        _op("GETGLOBAL", 1),
        _op("GETGLOBAL", 0),
        _op("GETGLOBAL", 2),
        _op("CALL", a=0, b=0),
        _op("GETGLOBAL", 4),
        _op("PUSHSTRING", 3),
        _int(3),
        *_table(1, 2, 3),
        *_table(4, 5, 6),
        _op("CALL", a=0, b=1),
        _op("SETGLOBAL", 3),
        _op("GETGLOBAL", 5),
        _op("GETGLOBAL", 6),
        _op("GETGLOBAL", 3),
        _op("CALL", a=0, b=0),
        _op("GETGLOBAL", 7),
        _op("PUSHSTRING", 8),
        _op("PUSHNIL", 2),
        _int(-1),
        _op("GETGLOBAL", 0),
        _op("CALL", a=0, b=0),
        _op("END"),
    ],
)

# ScenePreload("l11_c2")
PRELOAD = _facts(
    ["ScenePreload", "l11_c2"], [_op("GETGLOBAL", 0), _op("PUSHSTRING", 1), _op("CALL", a=0, b=1), _op("END")]
)


def test_world_flags_keep_call_order_and_number_repeated_names() -> None:
    flags = refs_world.world_flags({"level7_chapter1.lua": FLAGS})
    assert [flag["id"] for flag in flags] == ["level7_chapter1:f1", "level7_chapter1:f1#2", "level7_chapter1:f2"]
    assert flags[0] == {
        "id": "level7_chapter1:f1",
        "level": "level7",
        "name": "f1",
        "pos": [1, 2, 0],
        "heading": 90,
        "activity": 5,
        "group": 2,
        "script": "level7_chapter1",
    }
    assert flags[2]["activity"] is None and flags[2]["group"] is None


def test_object_zones_resolve_names_switches_and_spawns_per_level() -> None:
    zones = refs_world.object_zones({"level7.lua": LEVEL, "global.lua": LEVEL})
    assert zones == [
        {
            "id": "level7:4",
            "level": "level7",
            "zone": 4,
            "names": ["Zone4"],
            "objects": 1,
            "enabled_by": ["level7"],
            "disabled_by": None,
        }
    ]


def test_volume_boxes_name_their_class_holder_and_turf() -> None:
    boxes = refs_world.volume_boxes({"level7.lua": LEVEL})
    assert boxes == [
        {
            "id": "level7:vTurf",
            "level": "level7",
            "name": "vTurf",
            "kind": 3,
            "class": "TurfBox",
            "corner": [1, 2, 3],
            "size": [4, 5, 6],
            "held_in": "vTurf",
            "turf_of": ["GangA"],
            "script": "level7",
        }
    ]


def test_scene_preload_takes_the_first_record_containing_the_name() -> None:
    records_bytes = struct.pack("<I", 3)
    for scene, size, name in ((0, 10, b"l11_c2x"), (1, 20, b"l11_c2"), (2, 30, b"l11_c2xaa")):
        records_bytes += struct.pack("<II", scene, size) + name.ljust(16, b"\0")
    records = refs_world.scene_records(records_bytes)
    assert records == [(0, 10, "l11_c2x"), (1, 20, "l11_c2"), (2, 30, "l11_c2xaa")]
    assert refs_world.preload_id(records, "l11_c2") == 0
    assert refs_world.preload_id(records, "none") == 0
    scenes = refs_world.scenes(records, {"l11_c2xaa"}, {"level11_chapter1.lua": PRELOAD})
    assert [(s["id"], s["scene"], s.get("levels"), s.get("scripts")) for s in scenes] == [
        ("l11_c2x", 0, ["level11"], ["level11_chapter1"]),
        ("l11_c2", 1, None, None),
    ]


def test_movies_come_from_the_boot_the_level_records_and_the_menu() -> None:
    row = lua4.Table(
        items={
            1: "level9",
            2: "w",
            3: 9.0,
            4: 1.0,
            5: 1.0,
            6: lua4.Global("LT_NONE"),
            7: lua4.Global("LT_PLAY"),
            8: lua4.Global("LT_NONE"),
        }
    )
    menu = lua4.Table(items={1: "TRAILER"})
    movies = refs_world.movies([row], menu, {"L9_IN", "LOGO", "EXTRA"})
    by_name = {movie["name"]: movie for movie in movies}
    assert sorted(by_name) == ["EXTRA", "L1_IN", "L9_IN", "LOGO", "PLOGO", "TRAILER"]
    assert by_name["L9_IN"]["played_by"] == ["level9 intro"] and by_name["L9_IN"]["on_disc"] is True
    assert by_name["TRAILER"]["played_by"] == ["front end Menu.movies[1]"] and by_name["TRAILER"]["on_disc"] is False
    assert by_name["EXTRA"]["played_by"] is None
    assert refs_world.movies([], None, None)[0]["on_disc"] is None


# --- split lists (one data file and one page per group) ----------------------------------------------------------

PLACES = Topic(
    "places",
    "id",
    "place",
    (
        Field("id", "str", "The id.", required=True),
        Field("level", "str", "The level.", "Level"),
        Field("name", "str", "A name.", "Name"),
        Field("friends", "list", "Other places.", "Friends", link="places.md#place"),
    ),
    group_by="level",
    compact=True,
    split=True,
)


def _places() -> RefList:
    """A split list with entries in three groups, in page order."""
    entries: list[dict[str, object]] = [
        {"id": "level2:a", "level": "level2", "name": "a"},
        {"id": "level11:b", "level": "level11", "name": "b", "friends": ["level2:a"]},
        {"id": "level11:c", "level": "level11", "name": "c"},
        {"id": "x:d", "name": "d"},
    ]
    return RefList(PLACES, "Places", "About.", "All.", {"source": "made up", "evidence": "inferred"}, entries)


def test_split_list_writes_one_part_per_group_and_reads_them_back_in_order(tmp_path: Path) -> None:
    path = tmp_path / "places.yaml"
    (tmp_path / "places").mkdir()
    (tmp_path / "places" / "gone.yaml").write_text("entries: []\n", encoding="utf-8")
    refs.write(path, _places())
    assert sorted(p.name for p in (tmp_path / "places").iterdir()) == ["level11.yaml", "level2.yaml", "other.yaml"]
    assert "entries: []" in path.read_text(encoding="utf-8")
    assert refs.load(path, PLACES).entries == _places().entries  # level2 before level11, "other" last
    (tmp_path / "places" / "level2.yaml").write_text("title: x\n", encoding="utf-8")
    with pytest.raises(RefsError, match="only a list of entries"):
        refs.load(path, PLACES)


def test_split_list_renders_an_overview_and_a_page_per_group() -> None:
    overview = refs_render.page(_places())
    assert "| [level2](places/level2.md) | 1 |" in overview and "| [other](places/other.md) | 1 |" in overview
    assert "## Fields" in overview and 'id="place-level11-b"' not in overview
    pages = refs_render.group_pages(_places())
    assert sorted(pages) == ["places/level11.md", "places/level2.md", "places/other.md"]
    level11 = pages["places/level11.md"]
    assert level11.startswith("# Places: level11\n")
    assert '<span id="place-level11-b"></span>' in level11 and "place-level2-a" in level11
    assert "[level2:a](../places.md#place-level2-a)" in level11  # links reach above the folder
    assert "(../places.md#fields)" in level11


def test_render_removes_the_page_of_a_group_that_left(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    for item in TOPICS:
        refs.write(tmp_path / refs_cli.REFS_DIR / f"{item.key}.yaml", refs_cli.new_list(item))
    monkeypatch.chdir(tmp_path)
    assert main(["refs", "render"]) == 0
    stray = tmp_path / "docs" / "references" / "flags" / "level999.md"
    stray.parent.mkdir(parents=True, exist_ok=True)
    stray.write_text("old\n", encoding="utf-8")
    assert main(["refs", "render", "--check"]) == 1
    assert main(["refs", "render"]) == 0
    assert not stray.exists()
