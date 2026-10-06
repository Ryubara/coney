# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the world reference lists from the player's own disc: world flags, object zones, volume boxes, and the
scenes and movies (`coney-tools refs extract`).

Flags, zones and boxes come from the level scripts' calls (`AddFlag`, `ObjEnableZone`, `ObjSpawn`, `AddVolumeBox`,
`GangAddTurfBox`); scenes from the scene list `scene_list.cnk` and the scripts' `ScenePreload` calls; movies from the
level records, the front end's movie table, the boot sequence and the disc's `PSS` folder. Only names, ids and
numbers are kept, never a script's code or a file's bytes.

Each `topic_*` function returns the entries of one list, in page order; the pure functions under them take the walked
scripts so the tests can feed them synthetic chunks.

Research: docs/research/flags.md, docs/research/tasks.md#classes, docs/research/scripting.md#scenes-and-movies,
docs/research/level-loading.md#the-level-record.
"""

from __future__ import annotations

import collections
import re
import struct
from collections.abc import Iterable
from typing import TYPE_CHECKING, Any

from coney_tools import lua4, wad
from coney_tools.chunks import parse_container

if TYPE_CHECKING:
    from pathlib import Path

    from coney_tools.refs_extract import DiscFacts

#: What `AddVolumeBox`'s kind makes: the pool each kind is taken from (docs/research/tasks.md#classes).
BOX_CLASSES = {0: "VolumeBox", 2: "PlayerBox", 3: "TurfBox"}
#: The scene list's chunk type, and its records: u32 id, u32 size, char name[16] (docs/research/scripting.md).
CHUNK_SCENE_LIST = 0x43
SCENE_RECORD = 24
#: The movies the boot sequence plays (`0x0042a938` called from the boot code; docs/research/boot.md#main).
BOOT_MOVIES = ("LOGO", "PLOGO", "L1_IN")
#: The front end's movie table (`Menu.movies` in `level100.lua`; docs/research/frontend.md).
MENU_SCRIPT = "level100.lua"

_LEVEL = re.compile(r"^level(\d+)")
_ZONE_NAME = re.compile(r"^Zone\d+$|_ZONE$")


def _level(script: str) -> str | None:
    """The level a script belongs to (`level11_chapter3.lua` -> `level11`), or None for a shared script."""
    match = _LEVEL.match(script)
    return f"level{match.group(1)}" if match else None


def _level_order(level: str | None) -> int:
    """Sort key of a level name: its number; shared scripts last."""
    return int(level[5:]) if level else 1 << 30


def _whole(value: Any) -> int | None:
    """A whole number argument as an int, else None."""
    return int(value) if isinstance(value, float) and value.is_integer() else None


def _vector(value: Any) -> list[int | float] | None:
    """A literal `{x, y, z}` table as a list rounded to 4 decimals, else None."""
    if not isinstance(value, lua4.Table):
        return None
    items = value.as_list()
    if len(items) != 3 or not all(isinstance(v, float) for v in items):
        return None
    return [int(v) if float(v).is_integer() else round(float(v), 4) for v in items]  # type: ignore[arg-type]


def _number(value: Any) -> int | float | None:
    """A numeric argument, whole numbers as ints, else None."""
    if not isinstance(value, float):
        return None
    return int(value) if value.is_integer() else round(value, 4)


def _by_level(scripts: dict[str, lua4.ChunkFacts]) -> dict[str | None, list[str]]:
    """Script names grouped by level, each group sorted."""
    groups: dict[str | None, list[str]] = collections.defaultdict(list)
    for script in sorted(scripts):
        groups[_level(script)].append(script)
    return dict(sorted(groups.items(), key=lambda item: _level_order(item[0])))


# --- world flags -------------------------------------------------------------------------------------------------


def world_flags(scripts: dict[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Every `AddFlag` call with a literal name: per level, per script, in call order."""
    entries = []
    for level, names in _by_level(scripts).items():
        for script in names:
            stem = script.removesuffix(".lua")
            seen: collections.Counter[str] = collections.Counter()
            for call in scripts[script].calls:
                if call.callee != "AddFlag" or not call.args or not isinstance(call.args[0], str):
                    continue
                args = [*call.args, None, None, None, None]
                name = call.args[0]
                seen[name] += 1
                activity = _whole(args[3]) or None
                entries.append(
                    {
                        # A name added twice by one script keeps its first id; the next ones get #2, #3 ...
                        "id": f"{stem}:{name}" + (f"#{seen[name]}" if seen[name] > 1 else ""),
                        "level": level,
                        "name": name,
                        "pos": _vector(args[1]),
                        "heading": _number(args[2]),
                        "activity": activity,
                        "group": _whole(args[4]) or None,
                        "script": stem,
                    }
                )
    return entries


def topic_flags(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """World flags from every script."""
    return world_flags(facts.scripts)


# --- object zones ------------------------------------------------------------------------------------------------


def _zone_names(facts: Iterable[lua4.ChunkFacts]) -> dict[str, int]:
    """The names a level's scripts give zone numbers: `Zone<n>` globals and `<TABLE>.<NAME>_ZONE` fields."""
    names: dict[str, int] = {}
    for chunk in facts:
        for assignment in chunk.assignments:
            number = _whole(assignment.value)
            if number is not None and _ZONE_NAME.search(assignment.name):
                names[assignment.name] = number
        for table_name, table in chunk.tables.items():
            for key, value in table.fields.items():
                number = _whole(value)
                if number is not None and isinstance(key, str) and key.endswith("_ZONE"):
                    names[f"{table_name}.{key}"] = number
    return names


def _zone(value: Any, names: dict[str, int]) -> int | None:
    """A zone argument: a number, or a name the level gives one."""
    if isinstance(value, lua4.Global):
        return names.get(value.name)
    return _whole(value)


def _switch(value: Any) -> bool:
    """`ObjEnableZone`'s second argument as the binding reads it: nil, false and 0 switch off."""
    if isinstance(value, lua4.Global):
        return value.name != "false"
    return value is not None and value != 0.0


def object_zones(scripts: dict[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Per level, every zone number its scripts name, switch (`ObjEnableZone`) or spawn objects into (`ObjSpawn`)."""
    entries = []
    for level, group in _by_level(scripts).items():
        if level is None:
            continue  # a shared script's zone globals are the calling level's, not known here
        names = _zone_names(scripts[script] for script in group)
        enabled: dict[int, set[str]] = collections.defaultdict(set)
        disabled: dict[int, set[str]] = collections.defaultdict(set)
        spawned: collections.Counter[int] = collections.Counter()
        for script in group:
            for call in scripts[script].calls:
                if call.callee == "ObjEnableZone" and call.args:
                    zone = _zone(call.args[0], names)
                    if zone is not None:
                        on = _switch(call.args[1] if len(call.args) > 1 else None)
                        (enabled if on else disabled)[zone].add(script.removesuffix(".lua"))
                elif call.callee == "ObjSpawn" and len(call.args) > 4:
                    zone = _zone(call.args[4], names)
                    if zone:  # zone 0 is the default every object without one is in
                        spawned[zone] += 1
        named: dict[int, list[str]] = collections.defaultdict(list)
        for name, number in sorted(names.items()):
            named[number].append(name)
        for zone in sorted(set(named) | set(enabled) | set(disabled) | set(spawned)):
            entries.append(
                {
                    "id": f"{level}:{zone}",
                    "level": level,
                    "zone": zone,
                    "names": named.get(zone) or None,
                    "objects": spawned.get(zone) or None,
                    "enabled_by": sorted(enabled.get(zone, ())) or None,
                    "disabled_by": sorted(disabled.get(zone, ())) or None,
                }
            )
    return entries


def topic_zones(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Object zones from every level's scripts."""
    return object_zones(facts.scripts)


# --- volume boxes ------------------------------------------------------------------------------------------------


def _box_holders(chunk: lua4.ChunkFacts) -> dict[str, str]:
    """Where a script keeps box handles: global -> the box's name."""
    holders = {}
    for assignment in chunk.assignments:
        value = assignment.value
        if isinstance(value, lua4.CallResult) and value.callee == "AddVolumeBox" and value.args[:1]:
            name = value.args[0]
            if isinstance(name, str):
                holders[assignment.name] = name
    return holders


def volume_boxes(scripts: dict[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Every `AddVolumeBox` call with a literal name, with the global holding it and the gangs given it as turf."""
    entries = []
    for level, group in _by_level(scripts).items():
        holders: dict[str, str] = {}
        for script in group:
            holders.update(_box_holders(scripts[script]))
        turf: dict[str, set[str]] = collections.defaultdict(set)
        for script in group:
            for call in scripts[script].calls:
                if call.callee != "GangAddTurfBox" or len(call.args) < 2:
                    continue
                gang, box = call.args[0], call.args[1]
                if isinstance(gang, lua4.Global) and isinstance(box, lua4.Global) and box.name in holders:
                    turf[holders[box.name]].add(gang.name)
        held = {name: holder for holder, name in sorted(holders.items(), reverse=True)}
        for script in group:
            stem = script.removesuffix(".lua")
            seen: collections.Counter[str] = collections.Counter()
            for call in scripts[script].calls:
                if call.callee != "AddVolumeBox" or not call.args or not isinstance(call.args[0], str):
                    continue
                args = [*call.args, None, None, None]
                name = call.args[0]
                seen[name] += 1
                kind = _whole(args[1])
                entries.append(
                    {
                        "id": f"{stem}:{name}" + (f"#{seen[name]}" if seen[name] > 1 else ""),
                        "level": level,
                        "name": name,
                        "kind": kind,
                        "class": BOX_CLASSES.get(kind) if kind is not None else None,
                        "corner": _vector(args[2]),
                        "size": _vector(args[3]),
                        "held_in": held.get(name),
                        "turf_of": sorted(turf.get(name, ())) or None,
                        "script": stem,
                    }
                )
    return entries


def topic_boxes(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Volume, player and turf boxes from every script."""
    return volume_boxes(facts.scripts)


# --- scenes and movies -------------------------------------------------------------------------------------------


def scene_records(chunk: bytes) -> list[tuple[int, int, str]]:
    """The scene list chunk's records: (scene id, size in bytes, name)."""
    count = struct.unpack_from("<I", chunk, 0)[0]
    records = []
    for index in range(count):
        scene, size = struct.unpack_from("<II", chunk, 4 + index * SCENE_RECORD)
        raw = chunk[12 + index * SCENE_RECORD : 4 + (index + 1) * SCENE_RECORD]
        records.append((scene, size, raw.split(b"\0")[0].decode("latin-1")))
    return records


def preload_id(records: list[tuple[int, int, str]], name: str) -> int:
    """The id `ScenePreload(name)` gets: the first record whose name contains `name` (a case-sensitive substring
    test), else 0."""
    return next((scene for scene, _, record in records if name in record), 0)


def scenes(
    records: list[tuple[int, int, str]], segments: set[str], scripts: dict[str, lua4.ChunkFacts]
) -> list[dict[str, Any]]:
    """Every scene of the scene list that is not a later segment of another, with the scripts that preload it."""
    loaders: dict[int, set[str]] = collections.defaultdict(set)
    for script, chunk in scripts.items():
        for call in chunk.calls:
            if call.callee == "ScenePreload" and call.args and isinstance(call.args[0], str):
                loaders[preload_id(records, call.args[0])].add(script)
    entries = []
    # Names cut to 15 characters can repeat; every record of such a name is keyed by name and id.
    repeated = {name for name, count in collections.Counter(r[2] for r in records).items() if count > 1}
    for scene, size, name in records:
        if name in segments:
            continue
        users = sorted(loaders.get(scene, ()))
        levels = sorted({level for level in map(_level, users) if level}, key=_level_order)
        entries.append(
            {
                "id": f"{name}-{scene}" if name in repeated else name,
                "kind": "scene",
                "name": name,
                "scene": scene,
                "size": size,
                "levels": levels or None,
                "scripts": [s.removesuffix(".lua") for s in users[:8]]
                + ([f"... {len(users) - 8} more"] if len(users) > 8 else [])
                or None,
            }
        )
    return entries


def movies(level_rows: list[lua4.Value], menu: lua4.Value, on_disc: set[str] | None) -> list[dict[str, Any]]:
    """The full-motion movies: who plays each (the boot, a level's intro or outro, the front end) and whether the
    disc's `PSS` folder holds it (None when the disc's folders cannot be listed)."""
    played: dict[str, list[str]] = collections.defaultdict(list)
    for name in BOOT_MOVIES:
        played[name].append("boot")
    for row in level_rows:
        values = row.as_list() if isinstance(row, lua4.Table) else []
        number = _whole(values[2]) if len(values) > 7 else None
        if number is None or not isinstance(values[0], str):
            continue
        for value, suffix, what in ((values[6], "IN", "intro"), (values[7], "OUT", "outro")):
            if isinstance(value, lua4.Global) and value.name == "LT_PLAY":
                played[f"L{number}_{suffix}"].append(f"{values[0]} {what}")
    if isinstance(menu, lua4.Table):
        for key, value in sorted(menu.items.items()):
            if isinstance(value, str):
                played[value].append(f"front end Menu.movies[{key}]")
    names = set(played) | (on_disc or set())
    return [
        {
            "id": f"movie:{name}",
            "kind": "movie",
            "name": name,
            "played_by": played.get(name) or None,
            "on_disc": (name in on_disc) if on_disc is not None else None,
        }
        for name in sorted(names)
    ]


def _segments(facts: DiscFacts, records: list[tuple[int, int, str]]) -> set[str]:
    """The scene names whose `.scn` record is a segment (its second word is not 0); names with no entry are kept."""
    by_hash = {entry.hash: entry for entry in facts.entries}
    found = set()
    for _, _, name in records:
        entry = by_hash.get(wad.name_hash(wad.NAME_PREFIX + name + ".scn"))
        if entry is None:
            continue
        facts._handle.seek(entry.offset)
        if struct.unpack("<II", facts._handle.read(8))[1] != 0:
            found.add(name)
    return found


def _movie_files(facts: DiscFacts) -> set[str] | None:
    """The movie names in the disc's `PSS` folder, or None when the disc is an image (only its root is read)."""
    folder = facts.disc.path / "PSS"
    if not folder.is_dir():
        return None
    return {path.stem.upper() for path in folder.iterdir() if path.suffix.upper() == ".BIK"}


def topic_scenes(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The movies, then every scene of `scene_list.cnk` in id order."""
    entry = facts.by_name("scene_list.cnk")
    if entry is None:
        raise ValueError("scene_list.cnk is not on the disc")
    data = facts.read(entry)
    container = parse_container(data)
    chunk = next(
        (c for r in (container.resources if container else []) for c in r.chunks if c.type == CHUNK_SCENE_LIST),
        None,
    )
    if chunk is None:
        raise ValueError("scene_list.cnk has no Scene List chunk")
    records = scene_records(data[chunk.offset : chunk.offset + chunk.size])
    level_table = facts.table("config_preload3.lua", "levelNames")
    menu_table = facts.table(MENU_SCRIPT, "Menu")
    menu = menu_table.fields.get("movies") if menu_table else None
    return movies(level_table.as_list() if level_table else [], menu, _movie_files(facts)) + scenes(
        records, _segments(facts, records), facts.scripts
    )
