# SPDX-License-Identifier: GPL-3.0-or-later
"""The environment reference lists: glass types, doors, object tints, lights, spawner states and crime types.

Each list is defined here (its topic, its starting prose and its reader) so that `refs_topics`, `refs_cli` and
`refs_extract` only register it. Glass types, doors, tints and lights come from what the scripts pass to bindings
(`CfgSetGlassProperties`, `SpawnBreakableGlass`, `SpawnDoor`, `ObjSpawn`, `ObjColor`, `SetLight`); the spawner
states and crime types are the code's own numbers, with the scripts' uses counted and the crime names read from the
executable. Only names, ids and numbers are kept, never a script's code or a file's bytes.

The pure functions take the walked scripts (`lua4.ChunkFacts` by script name) so the tests can feed them synthetic
facts; the `topic_*` functions are the disc readers `refs extract` calls.

Research: docs/research/objects.md, docs/research/ai.md#spawners, docs/research/ai.md#crimes,
docs/research/lighting.md#record, docs/guides/research-workflow.md#reference-lists.
"""

from __future__ import annotations

import collections
import re
import struct
from collections.abc import Callable, Iterable, Mapping
from typing import TYPE_CHECKING, Any

from coney_tools import lua4
from coney_tools.refs import Field, Topic

if TYPE_CHECKING:
    from pathlib import Path

    from coney_tools.refs_extract import DiscFacts

F = Field

# --- topics ------------------------------------------------------------------------------------------------------

GLASS_TYPES = Topic(
    "glass-types",
    "id",
    "glass",
    (
        F(
            "id",
            "int",
            "Glass type: `SpawnBreakableGlass`'s first argument and `CfgSetGlassProperties`'.",
            "Type",
            required=True,
        ),
        F(
            "window_link",
            "bool",
            "Second argument (`+0x14`): the pane sits on a navigation jump link, closed while it is whole and opened "
            "when it breaks (inferred).",
            "Window link",
        ),
        F(
            "alarm",
            "bool",
            "Third argument (`+0x18`): breaking the pane reports a [break-in](crime-types.md#crime-1).",
            "Alarm",
        ),
        F(
            "sprite",
            "hex",
            "Fourth argument (`+0x1c`): the whole pane's sprite word; its low 16 bits are the rectangle drawn.",
            "Sprite",
        ),
        F(
            "broken_sprite",
            "hex",
            "Fifth argument (`+0x20`): the sprite word left after the pane breaks (0 draws nothing).",
            "Broken sprite",
        ),
        F("behaviour", "str", "What the code does differently for this type.", "Behaviour", curated=True),
        F("panes", "int", "`SpawnBreakableGlass` calls that pass the type.", "Panes"),
        F("scripts", "list", "The scripts that place panes of it (at most six).", "Scripts"),
    ),
    nav="Glass types",
)

DOORS = Topic(
    "doors",
    "id",
    "door",
    (
        F("id", "str", "`<level>:<number>`.", required=True),
        F("level", "str", "The level whose script spawns the door.", "Level"),
        F(
            "number",
            "int",
            "`SpawnDoor`'s fifth argument (door `+0xe0`): the door's number in the level, unique per level; the "
            "level's navigation links carrying it belong to the door.",
            "Number",
        ),
        F("type", "str", "The door's object type (`SpawnDoor`'s first argument).", "Type", link="objects.md#obj"),
        F("pos", "list", "Position `{x, y, z}` in metres, game axes (z up).", "Position"),
        F(
            "triangles",
            "list",
            "`SpawnDoor`'s fourth argument (door `+0xd8`, `+0xdc`): the two collision triangles the door owns.",
            "Triangles",
        ),
        F(
            "marks_links",
            "bool",
            "Whether the door's kind marks the navigation links of its number as kind `0x40` (the breakable doors).",
            "Marks links",
        ),
        F("script", "str", "The script that spawns it (without `.lua`).", "Script"),
    ),
    group_by="level",
    compact=True,
    nav="Doors",
)

TINTS = Topic(
    "tints",
    "id",
    "tint",
    (
        F("id", "str", "`spawn:<RRGGBBAA>`, `color:<RRGGBBAA>` or `icon:default`.", required=True),
        F("kind", "str", "`ObjSpawn` tint, `ObjColor` tint or spinning icon colour.", "Kind"),
        F("rgba", "str", "The colour word as the object keeps it (`+0xc8`), `RRGGBBAA` hex.", "RGBA"),
        F("swatch", "str", "A swatch of the colour (rendered).", "Swatch"),
        F("calls", "int", "Script calls that pass it.", "Calls"),
        F(
            "objects",
            "list",
            "The object types given it, most often first (at most six).",
            "Objects",
            link="objects.md#obj",
        ),
        F("scripts", "list", "The scripts that pass it (at most six).", "Scripts"),
    ),
    group_by="kind",
    nav="Object tints",
)

LIGHTS = Topic(
    "lights",
    "id",
    "light",
    (
        F("id", "str", "`<script>:<n>`: the script and the call's place among its `SetLight` calls.", required=True),
        F("level", "str", "The level whose scripts make the light; none for `global.lua`.", "Level"),
        F("name", "str", "The key the script keeps the light under in its `Lights` table, when it does.", "Name"),
        F("type", "str", "Second argument: point, spot, directional or ambient.", "Type"),
        F("pos", "list", "Third argument: position `{x, y, z}` in metres (point and spot lights).", "Position"),
        F("dir", "list", "Fourth argument: direction (spot and directional lights).", "Direction"),
        F("rgba", "list", "Fifth argument: colour `{r, g, b, a}`, components 0-1.", "Colour"),
        F("swatch", "str", "A swatch of the colour (rendered).", "Swatch"),
        F("radius", "float", "Sixth argument: radius in metres.", "Radius"),
        F("cone", "float", "Seventh argument: a spot light's cone.", "Cone"),
        F(
            "flicker",
            "int",
            "Thirteenth argument: the corona, 0 none, 1-6 a rectangle of the `lighting` sheet.",
            "Corona",
        ),
        F(
            "flicker_params",
            "list",
            "Eighth to tenth arguments: the corona's height, pull towards the camera and size, in metres.",
        ),
        F(
            "group",
            "int",
            "Eleventh argument, 0-3: what the light lights (bit 0 objects, bit 1 the world).",
            "Lights what",
        ),
        F("priority", "int", "Twelfth argument, the effects word: bit 0 light bugs, bits 1-4 a flicker mode."),
        F("state", "int", "Fourteenth argument: 1 on, 0 off, 2 removes the light.", "State"),
        F("script", "str", "The script that makes it (without `.lua`).", "Script"),
    ),
    group_by="level",
    compact=True,
    nav="Lights",
    split=True,
)

SPAWNER_STATES = Topic(
    "spawner-states",
    "id",
    "spawner",
    (
        F(
            "id",
            "int",
            "The spawner state (spawner `+0x52`): `GangAddSpawner`'s kind, `GangStartSpawner`'s mode.",
            "State",
            required=True,
        ),
        F("name", "str", "Our short name for it.", "Name", curated=True),
        F("behaviour", "str", "When the spawner makes a human and what it does then.", "Behaviour", curated=True),
        F(
            "settable",
            "bool",
            "Whether `GangStartSpawner` can set it (the code's switch takes 0-5, 7, 8, 9 and 11).",
            "Settable",
        ),
        F("added", "int", "`GangAddSpawner` calls that start a spawner in it.", "Added"),
        F("started", "int", "`GangStartSpawner` calls that switch a spawner to it.", "Switched"),
        F("scripts", "list", "The scripts that make those calls (at most six).", "Scripts"),
    ),
    nav="Spawner states",
)

CRIME_TYPES = Topic(
    "crime-types",
    "id",
    "crime",
    (
        F(
            "id",
            "int",
            "The crime type: `CrimeIsHappening`'s kind, `CfgCrimeResponders`' and `CfgEnableCrimeType`'s "
            "first argument.",
            "Type",
            required=True,
        ),
        F("name", "str", "The name the executable's crime-name table gives it.", "Name"),
        F("response", "str", "What the game does when it is reported.", "Response", curated=True),
        F("responders", "list", "`CfgCrimeResponders` calls: `script: count`.", "Responders set by"),
        F("enabled", "list", "`CfgEnableCrimeType` calls: `script: on/off`.", "Switched by"),
        F("reported", "int", "Script calls that report it (`CrimeIsHappening`, `SpawnCustomCrime`).", "Script reports"),
        F("scripts", "list", "The scripts that report it (at most six).", "Reported in"),
    ),
    nav="Crime types",
)

#: The lists of this module, in index order.
TOPICS: tuple[Topic, ...] = (GLASS_TYPES, DOORS, TINTS, LIGHTS, SPAWNER_STATES, CRIME_TYPES)

#: The prose and defaults each list starts with; after the first extract they live in the YAML.
STARTERS: dict[str, dict[str, Any]] = {
    "glass-types": {
        "title": "Glass types",
        "source": "config_preload2.lua, CfgSetGlassProperties; SpawnBreakableGlass in the scripts",
        "evidence": "confirmed-code",
        "about": "The breakable-glass types: `CfgSetGlassProperties(type, windowLink, alarm, sprite, brokenSprite)`\n"
        "configures each in `config_preload2.lua`, and a level script places panes with\n"
        "`SpawnBreakableGlass(type, corner, cornerU, cornerV, uv0, uv1, 0, triangle1, triangle2)`\n"
        "([World objects: glass](../research/objects.md#glass)).",
        "complete": "All 19 types are listed with every argument. The sprite words' low halves are rectangles\n"
        "of `part_page1`, the sheet of sprite batch 0 that every pane but type 14 draws from.",
    },
    "doors": {
        "title": "Doors",
        "source": "the level scripts, SpawnDoor",
        "evidence": "confirmed-code",
        "about": "Every door level scripts spawn with `SpawnDoor(type, pos, rot, {triangle1, triangle2}, number)`\n"
        "([World objects: doors](../research/objects.md#doors)). The number was thought to be a lock kind; it is\n"
        "the door's number in its level, which ties the level's navigation links to the door.",
        "complete": "Every `SpawnDoor` call (484 doors in 45 levels). What the navigation links' kind `0x40` does,\n"
        "and who else reads the number, are not traced.",
    },
    "tints": {
        "title": "Object tints",
        "source": "the scripts, ObjSpawn and ObjColor",
        "evidence": "confirmed-code",
        "about": "The colours scripts tint world objects with: `ObjSpawn`'s seventh argument, a colour word\n"
        "`0xRRGGBBAA` (default `0xFFFFFFFF`, no tint), and `ObjColor(object, {r, g, b, a})`. Both end in the\n"
        "object's tint word ([World objects: tint](../research/objects.md#tint)). The colours scripts give cars\n"
        "are in [Cars](cars.md#colour) ([Cars: colour](../research/cars.md#colour)).",
        "complete": "Every literal tint of `ObjSpawn` and `ObjColor` is listed. No script calls\n"
        "`HuSetSpinningIconColor`; its default is listed.",
    },
    "lights": {
        "title": "Lights",
        "source": "the level scripts and global.lua, SetLight",
        "evidence": "confirmed-code",
        "about": "Every light the scripts make with the long form of `SetLight(0, type, pos, dir, colour, radius,\n"
        "cone, coronaHeight, coronaPull, coronaSize, lights, effects, corona, state)`\n"
        "([Lighting](../research/lighting.md#record)).\n"
        "`global.lua` builds each level's moonlight, reflected and ambient light from the level's `LightData`;\n"
        "level scripts add their lamps, most kept in a `Lights` table.",
        "complete": "Every 14-argument `SetLight` call; arguments the script computes show none. The corona\n"
        "parameters and the effects word are not listed.",
    },
    "spawner-states": {
        "title": "Spawner states",
        "source": "SLUS_212.15, spawner update 0x001681a0",
        "evidence": "confirmed-code",
        "about": "A gang's spawners (four per gang, `GangAddSpawner`) make humans according to their state:\n"
        "`GangAddSpawner`'s kind starts one in a state and `GangStartSpawner(gang, name, state, value)` switches it\n"
        "([AI: spawners](../research/ai.md#spawners)).",
        "complete": "All twelve states are listed; 6 and 10 only the game sets.",
    },
    "crime-types": {
        "title": "Crime types",
        "source": "SLUS_212.15, crime report 0x0041b8b0 and crime-name table 0x0041d2c0",
        "evidence": "confirmed-code",
        "about": "The crimes the police respond to. Scripts report one with `CrimeIsHappening` or\n"
        "`SpawnCustomCrime`, set how many responders each type draws with `CfgCrimeResponders` and switch one with\n"
        "`CfgEnableCrimeType`; the game reports others itself ([AI: crimes](../research/ai.md#crimes)).",
        "complete": "All 15 types are listed. The names come from a table no code calls, so which name goes with\n"
        "which number is inferred from the types the code raises.",
    },
}

# --- shared helpers ----------------------------------------------------------------------------------------------

_LEVEL = re.compile(r"^level(\d+)")
#: How many scripts or objects a list cell shows.
SHOWN = 6


def _level(script: str) -> str | None:
    """The level a script belongs to (`level11_chapter3.lua` -> `level11`), or None for a shared script."""
    match = _LEVEL.match(script)
    return f"level{match.group(1)}" if match else None


def _level_order(script: str) -> tuple[int, str]:
    """Sort key of a script: its level's number (shared scripts first), then its name."""
    level = _level(script)
    return (int(level[5:]) if level else -1, script)


def _whole(value: Any) -> int | None:
    """A whole number argument as an int, else None."""
    return int(value) if isinstance(value, float) and value.is_integer() else None


def _number(value: Any) -> int | float | None:
    """A numeric argument rounded to 4 decimals, whole numbers as ints, else None."""
    if not isinstance(value, float):
        return None
    return int(value) if value.is_integer() else round(value, 4)


def _numbers(value: Any, count: int) -> list[int | float] | None:
    """A literal table of `count` numbers as a list, else None."""
    if not isinstance(value, lua4.Table):
        return None
    items = [_number(v) for v in value.as_list()]
    if len(items) != count or any(v is None for v in items):
        return None
    return items  # type: ignore[return-value]


def _truth(value: Any) -> bool | None:
    """A boolean argument: the globals `TRUE` / `true` and `FALSE` / `false`, or a number (non-zero is true)."""
    if isinstance(value, lua4.Global):
        return {"true": True, "false": False}.get(value.name.lower())
    if isinstance(value, float):
        return value != 0.0
    return None


def _shown(items: Iterable[str]) -> list[str] | None:
    """At most SHOWN distinct items in name order, None when empty."""
    return sorted(set(items), key=_level_order)[:SHOWN] or None


def _stem(script: str) -> str:
    """A script's name without `.lua`."""
    return script.removesuffix(".lua")


def _calls(scripts: Mapping[str, lua4.ChunkFacts], callee: str) -> Iterable[tuple[str, lua4.Call]]:
    """Every call of `callee`, scripts in level order, calls in code order."""
    for script in sorted(scripts, key=_level_order):
        for call in scripts[script].calls:
            if call.callee == callee:
                yield script, call


# --- glass types -------------------------------------------------------------------------------------------------


def glass_types(scripts: Mapping[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Every type `CfgSetGlassProperties` configures, with the panes `SpawnBreakableGlass` places of it."""
    panes: dict[int, list[str]] = collections.defaultdict(list)
    for script, call in _calls(scripts, "SpawnBreakableGlass"):
        number = _whole(call.args[0]) if call.args else None
        if number is not None:
            panes[number].append(script)
    entries: dict[int, dict[str, Any]] = {}
    for _, call in _calls(scripts, "CfgSetGlassProperties"):
        args = [*call.args, None, None, None, None, None]
        number = _whole(args[0])
        if number is None:
            continue
        entries[number] = {
            "id": number,
            "window_link": _truth(args[1]),
            "alarm": _truth(args[2]),
            "sprite": _whole(args[3]),
            "broken_sprite": _whole(args[4]),
        }
    for number in set(panes) - set(entries):  # a type placed but never configured keeps zeros
        entries[number] = {"id": number}
    for number, entry in entries.items():
        entry["panes"] = len(panes.get(number, ())) or None
        entry["scripts"] = _shown(_stem(s) for s in panes.get(number, ()))
    return [entries[number] for number in sorted(entries)]


def topic_glass_types(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Glass types from `config_preload2.lua` and every level's panes."""
    return glass_types(facts.scripts)


# --- doors -------------------------------------------------------------------------------------------------------

#: The door classes whose initialisers mark their number's navigation links as kind 0x40 (0x003b2f40, 0x003b3250,
#: 0x003b4128, 0x003b57d0, 0x003b6220), and the swinging door types that do so (0x003f80f0 tests these names).
LINK_MARKING_CLASSES = frozenset(
    ("dyn_door_fence", "dyn_door_bar_bani", "dyn_door_bnstr", "dyn_door_fence_o", "dyn_door_parapet")
)
LINK_MARKING_TYPES = frozenset(("dyn_door_dclub", "dyn_door_liz"))


def object_classes(scripts: Mapping[str, lua4.ChunkFacts]) -> dict[str, str]:
    """Object type name -> its `CfgObj` class, from the first `CfgObj` of each type."""
    classes: dict[str, str] = {}
    for _, call in _calls(scripts, "CfgObj"):
        if len(call.args) > 1 and isinstance(call.args[0], str):
            given = call.args[1]
            name = given if isinstance(given, str) else given.name if isinstance(given, lua4.Global) else None
            if name:
                classes.setdefault(call.args[0], name)
    return classes


def doors(scripts: Mapping[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Every `SpawnDoor` call, by level and door number."""
    classes = object_classes(scripts)
    entries = []
    for script, call in _calls(scripts, "SpawnDoor"):
        args = [*call.args, None, None, None, None, None]
        number = _whole(args[4])
        level = _level(script)
        door_type = args[0] if isinstance(args[0], str) else None
        if number is None or level is None:
            continue
        marks = door_type in LINK_MARKING_TYPES or classes.get(door_type or "") in LINK_MARKING_CLASSES
        entries.append(
            {
                "id": f"{level}:{number}",
                "level": level,
                "number": number,
                "type": door_type,
                "pos": _numbers(args[1], 3),
                "triangles": _numbers(args[3], 2),
                "marks_links": marks or None,
                "script": _stem(script),
            }
        )
    return sorted(entries, key=lambda e: (int(e["level"][5:]), e["number"]))


def topic_doors(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Doors from every level script."""
    return doors(facts.scripts)


# --- tints -------------------------------------------------------------------------------------------------------

#: `ObjSpawn`'s default tint, and `HuSetSpinningIconColor`'s default second colour (`0x0035af80`).
NO_TINT = 0xFFFFFFFF
ICON_DEFAULT = 0xFFFFFF00


def colour_byte(component: float) -> int:
    """The byte `ObjColor` stores for one component: the binding and the packer (`0x00396bd0`, `0x0017aca8`) each
    multiply by 255 and the soft-float conversion keeps the low 8 bits, so a whole 0-255 component comes out as
    itself (65,025 is 1 modulo 256) and a 0-1 one does not."""
    return int(component * 255.0 * 255.0) & 0xFF if component > 0 else 0


def tints(scripts: Mapping[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """The literal tints of `ObjSpawn` and `ObjColor`, and the spinning icon's default."""
    spawned: dict[int, list[str]] = collections.defaultdict(list)
    objects: dict[int, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    for script, call in _calls(scripts, "ObjSpawn"):
        word = _whole(call.args[6]) if len(call.args) > 6 else None
        if word is None or word == NO_TINT:
            continue
        spawned[word].append(script)
        objects[word][call.args[0] if isinstance(call.args[0], str) else "?"] += 1
    coloured: dict[int, list[str]] = collections.defaultdict(list)
    for script, call in _calls(scripts, "ObjColor"):
        values = _numbers(call.args[1], 4) if len(call.args) > 1 else None
        if values is not None:
            r, g, b, a = (colour_byte(float(v)) for v in values)
            coloured[r << 24 | g << 16 | b << 8 | a].append(script)
    entries: list[dict[str, Any]] = []
    for kind, prefix, found in (("ObjSpawn tint", "spawn", spawned), ("ObjColor tint", "color", coloured)):
        for word in sorted(found, key=lambda w: (-len(found[w]), w)):
            hex_word = f"{word:08X}"
            names = [name for name, _ in objects[word].most_common(SHOWN) if name != "?"] if found is spawned else []
            entries.append(
                {
                    "id": f"{prefix}:{hex_word}",
                    "kind": kind,
                    "rgba": hex_word,
                    "swatch": hex_word,
                    "calls": len(found[word]),
                    "objects": names or None,
                    "scripts": _shown(_stem(s) for s in found[word]),
                }
            )
    icon = f"{ICON_DEFAULT:08X}"
    entries.append({"id": "icon:default", "kind": "spinning icon colour", "rgba": icon, "swatch": icon})
    return entries


def topic_tints(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Object tints from every script."""
    return tints(facts.scripts)


# --- lights ------------------------------------------------------------------------------------------------------

#: `SetLight`'s light types (`0x0017ef20`: 0 → RenderWare point, 1 spot, 2 directional, 3 ambient).
LIGHT_TYPES = ("point", "spot", "directional", "ambient")


def _light_names(facts: lua4.ChunkFacts) -> list[tuple[str, tuple[lua4.Value, ...]]]:
    """The `Lights.<name> = SetLight(...)` of a script: each key with the call's arguments."""
    named = []
    for assignment in facts.assignments:
        if assignment.name == "Lights" and isinstance(assignment.value, lua4.Table):
            for key, value in assignment.value.fields.items():
                if isinstance(value, lua4.CallResult) and value.callee == "SetLight" and isinstance(key, str):
                    named.append((key, value.args))
    return named


def _hex_colour(rgba: list[int | float] | None) -> str | None:
    """A 0-1 colour as `RRGGBBAA`, components clamped."""
    if rgba is None:
        return None
    return "".join(f"{round(min(max(float(v), 0.0), 1.0) * 255):02X}" for v in rgba)


def lights(scripts: Mapping[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Every long-form `SetLight` call (14 arguments), per script in call order, named where a `Lights` key holds it."""
    entries = []
    for script in sorted(scripts, key=_level_order):
        names = _light_names(scripts[script])
        count = 0
        for call in scripts[script].calls:
            if call.callee != "SetLight" or len(call.args) != 14:
                continue
            count += 1
            args = call.args
            name = next((key for key, given in names if list(given) == list(args)), None)
            if name is not None:
                names.remove(next(item for item in names if item[0] == name))
            kind = _whole(args[1])
            rgba = _numbers(args[4], 4)
            entries.append(
                {
                    "id": f"{_stem(script)}:{count}",
                    "level": _level(script),
                    "name": name,
                    "type": LIGHT_TYPES[kind] if kind is not None and 0 <= kind < len(LIGHT_TYPES) else None,
                    "pos": _numbers(args[2], 3),
                    "dir": _numbers(args[3], 3),
                    "rgba": rgba,
                    "swatch": _hex_colour(rgba),
                    "radius": _number(args[5]),
                    "cone": _number(args[6]),
                    "flicker": _whole(args[12]),
                    "flicker_params": [_number(v) for v in args[7:10]] if any(_number(v) for v in args[7:10]) else None,
                    "group": _whole(args[10]),
                    "priority": _whole(args[11]) or None,
                    "state": _whole(args[13]),
                    "script": _stem(script),
                }
            )
    return entries


def topic_lights(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Lights from every script."""
    return lights(facts.scripts)


# --- spawner states ----------------------------------------------------------------------------------------------

#: The states `GangStartSpawner` can set (the switch of `0x00168cd0`); 6 and 10 only the spawner update sets.
SETTABLE_STATES = frozenset((0, 1, 2, 3, 4, 5, 7, 8, 9, 11))
SPAWNER_STATE_COUNT = 12
#: Where the state is among each binding's arguments (docs/references/bindings/gang.md).
ADD_KIND, START_MODE = 10, 2


def spawner_states(scripts: Mapping[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """The twelve spawner states, with the scripts' `GangAddSpawner` kinds and `GangStartSpawner` modes."""
    uses: dict[str, dict[int, list[str]]] = {}
    for callee, position in (("GangAddSpawner", ADD_KIND), ("GangStartSpawner", START_MODE)):
        found: dict[int, list[str]] = collections.defaultdict(list)
        for script, call in _calls(scripts, callee):
            state = _whole(call.args[position]) if len(call.args) > position else None
            if state is not None:
                found[state].append(script)
        uses[callee] = found
    added, started = uses["GangAddSpawner"], uses["GangStartSpawner"]
    return [
        {
            "id": state,
            "settable": state in SETTABLE_STATES,
            "added": len(added.get(state, ())) or None,
            "started": len(started.get(state, ())) or None,
            "scripts": _shown(_stem(s) for s in [*added.get(state, ()), *started.get(state, ())]),
        }
        for state in range(SPAWNER_STATE_COUNT)
    ]


def topic_spawner_states(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Spawner states, with the scripts' uses."""
    return spawner_states(facts.scripts)


# --- crime types -------------------------------------------------------------------------------------------------

#: The crime-name function's jump table (`0x0041d2c0`): 16 cases, each `lui v0, hi; jr ra; addiu v0, v0, lo`.
CRIME_NAME_JUMPS = 0x0058B0E0
CRIME_TYPES_COUNT = 15  # 0-14; case 15 and above give "unknown"


def crime_names(read: Callable[[int, int], bytes]) -> list[str]:
    """The 15 crime names, decoding each case of the name function's switch to the string it returns.

    `read(address, size)` gives the executable's bytes at a virtual address."""
    names = []
    targets = struct.unpack(f"<{CRIME_TYPES_COUNT}I", read(CRIME_NAME_JUMPS, 4 * CRIME_TYPES_COUNT))
    for target in targets:
        lui, _, addiu = struct.unpack("<3I", read(target, 12))
        address = ((lui & 0xFFFF) << 16) + struct.unpack("<h", struct.pack("<H", addiu & 0xFFFF))[0]
        names.append(read(address, 32).split(b"\0")[0].decode("latin-1"))
    return names


def crime_types(scripts: Mapping[str, lua4.ChunkFacts], names: list[str]) -> list[dict[str, Any]]:
    """The crime types, with the scripts' responder counts, switches and reports."""
    responders: dict[int, list[str]] = collections.defaultdict(list)
    enabled: dict[int, list[str]] = collections.defaultdict(list)
    reported: dict[int, list[str]] = collections.defaultdict(list)
    for script, call in _calls(scripts, "CfgCrimeResponders"):
        crime = _whole(call.args[0]) if call.args else None
        count = _whole(call.args[1]) if len(call.args) > 1 else None
        if crime is not None and count is not None:
            responders[crime].append(f"{_stem(script)}: {count}")
    for script, call in _calls(scripts, "CfgEnableCrimeType"):
        crime = _whole(call.args[0]) if call.args else None
        on = _truth(call.args[1]) if len(call.args) > 1 else None
        if crime is not None and on is not None:
            enabled[crime].append(f"{_stem(script)}: {'on' if on else 'off'}")
    for script, call in _calls(scripts, "CrimeIsHappening"):
        crime = _whole(call.args[1]) if len(call.args) > 1 else None
        if crime is not None:
            reported[crime].append(script)
    reported[4] += [script for script, _ in _calls(scripts, "SpawnCustomCrime")]  # always kind 4 (0x0041b7a0)
    return [
        {
            "id": crime,
            "name": names[crime] if crime < len(names) else None,
            "responders": responders.get(crime),
            "enabled": enabled.get(crime),
            "reported": len(reported.get(crime, ())) or None,
            "scripts": _shown(_stem(s) for s in reported.get(crime, ())),
        }
        for crime in range(CRIME_TYPES_COUNT)
    ]


def topic_crime_types(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Crime types: their names from the executable and the scripts' uses."""
    return crime_types(facts.scripts, crime_names(facts.elf_bytes))


#: The reader of each list, by file stem.
EXTRACTORS: dict[str, Callable[[DiscFacts, Path | None], list[dict[str, Any]]]] = {
    "glass-types": topic_glass_types,
    "doors": topic_doors,
    "tints": topic_tints,
    "lights": topic_lights,
    "spawner-states": topic_spawner_states,
    "crime-types": topic_crime_types,
}
