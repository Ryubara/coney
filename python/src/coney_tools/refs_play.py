# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the gameplay reference lists from the player's own disc: inventory items, unlockables, power and Warrior
classes, attack kinds and tables, hat fittings, the Rumble roster and the statistics (`coney-tools refs extract`).

All of them come from what the configuration scripts pass to the `Cfg*` bindings and from the tables `global.lua`
builds (`Unlockables`, `PointsMatrix`); what the numbers mean comes from the research pages, kept here as small
tables where the game's code fixes it. Only names, ids and numbers are kept, never a script's code or its text (the
Rumble names and descriptions are never read).

Each `topic_*` function returns the entries of one list, in page order; the pure functions under them take the walked
scripts so the tests can feed them synthetic chunks. The topics themselves, and their starting prose, are defined here
too and registered in `refs_topics` and `refs_cli`.

Research: docs/research/characters.md#power-classes, docs/research/combat.md, docs/research/ai.md#attack-kinds,
docs/research/frontend.md#rumble, docs/research/gui.md#statistics, docs/guides/research-workflow.md#reference-lists.
"""

from __future__ import annotations

import collections
import re
from collections.abc import Callable, Iterable, Iterator, Mapping
from typing import TYPE_CHECKING, Any

from coney_tools import lua4
from coney_tools.refs import Field, Topic

if TYPE_CHECKING:
    from pathlib import Path

    from coney_tools.refs_extract import DiscFacts

F = Field
Scripts = Mapping[str, lua4.ChunkFacts]

CONFIG = "config_preload2.lua"
GLOBAL = "global.lua"
#: The difficulty scripts, each defining `CfgDifficulty`; `config_preload2.lua`'s own copy (run at boot) is
#: normal's, so its sets are listed only where no difficulty script has them.
DIFFICULTIES = ("easy", "normal", "hard", "fury", "rumble")
BOOT = "boot"
#: How many shown before "... n more" in a list cell.
SHOWN = 6

#: `0x00222c20` and `0x00222ba8` (both confirmed (code)): the Warrior class and the player's power class a human
#: of a Warriors type (category 14) gets as a player, by character type; every other type gets (9, 7).
WARRIOR_TYPES: dict[int, tuple[int, int, str]] = {}
for _types, _warrior, _power, _who in (
    ((1, 2, 3, 4, 189), 1, 59, "Cleon"),
    ((5, 6, 7, 8, 9, 10), 8, 66, "Swan"),
    ((11, 12, 13, 14), 0, 58, "Ajax"),
    ((15, 16, 17), 2, 60, "Cochise"),
    ((18, 19, 20, 188), 3, 61, "Cowboy"),
    ((21, 22, 23, 24, 25), 4, 62, "Fox"),
    ((26, 27, 28, 29, 191), 5, 63, "Vermin"),
    ((30, 31, 32, 38, 39, 40), 6, 64, "Rembrandt, Ash"),
    ((33, 34, 35, 36, 37, 190), 7, 65, "Snow"),
):
    for _type in _types:
        WARRIOR_TYPES[_type] = (_warrior, _power, _who)
OTHER_WARRIOR_CLASS, OTHER_POWER_CLASS = 9, 7

#: `AttackKind_ToCommand` (`0x00231090`): the command id an AI attack of each kind writes (docs/research/ai.md).
ATTACK_COMMANDS: dict[int, int | str] = {}
for _kinds, _command in (
    ((0, 32, 43), 0x10),
    ((1, 10, 31, 42), 0x0F),
    ((2, 3, 6), 0x12),
    ((4, 5, 7, 27, 28, 38, 39), 0x11),
    ((8,), 0x14),
    ((9,), 0x13),
    ((11,), 0x36),
    ((12,), 0x37),
    ((13,), 0x38),
    ((14, 21), 0x0E),
    ((15,), 0x24),
    ((16, 26, 37), 0x22),
    ((17,), 0x23),
    ((18,), 0),
    ((19,), 0x20),
    ((20,), 0x21),
    ((22, 25, 29, 30, 33, 40), 0x0D),
    ((23,), 0x39),
    ((24, 35), "0x0f or 0x10"),
    ((34, 44), 0x19),
    ((36,), 5),
    ((41,), 0x31),
):
    for _kind in _kinds:
        ATTACK_COMMANDS[_kind] = _command
#: `0x002548f0`'s jump table (`0x0055d6f0`): the anim ids a damage table's index writes (docs/research/combat.md).
ATTACK_ANIMS: dict[int, list[int]] = {
    0: [11], 1: [12], 2: [13], 3: [15], 4: [14], 5: [16], 6: [17], 7: [19], 8: [18], 9: [20],
    10: [25, 26, 27, 28, 29, 30], 11: [21], 12: [193], 13: [194], 16: [653, 655], 17: [657, 659], 19: [0], 20: [1],
    24: [51, 53, 55], 25: [147, 151, 149, 153], 26: [57], 27: [59], 28: [61], 29: [155, 159, 157, 161],
    31: [96, 98, 108, 110], 35: [219, 221, 223], 37: [225], 38: [227], 39: [229], 42: [250], 43: [246],
}  # fmt: skip
ATTACK_KINDS = 45
_TABLE_KIND = (("Att_", "attack weights"), ("Damage", "damage"), ("Range", "far range"))

#: The record types of the unlockables table, by what reads them (docs/research/frontend.md#unlockables).
UNLOCK_TYPES = {
    0: "story level",
    1: "Rumble mode",
    2: "Rumble arena",
    3: "Rumble gang",
    4: "Rumble character",
    5: "clubhouse display",
    6: "upgrade or feature",
    7: "flashback mission",
    8: "Coney Island shop",
    9: "Armies of the Night level",
    10: "workout move",
    11: "hint",
}

#: The six statistic groups (`StatAdd`'s group, `CfgSetStatValue`'s table, `CfgSetStatTypeMax`'s slots), which are
#: the end-of-mission scoring categories (`0x004211d8`, `0x00422a90`; docs/research/gui.md#statistics).
STAT_GROUPS = ("mission", "bonus", "style", "combat", "crime", "harmony")
#: `CfgSetStatTypeMax`'s argument order, as category names.
STAT_MAX_ARGS = ("combat", "crime", "harmony", "style", "mission", "bonus")
#: Entries per group (`0x00422da0` walks them all).
STAT_SIZES = (3, 4, 13, 8, 12, 5)

#: The inventory has 23 item slots, ids 0-22 (`0x0041e420`); the bindings that name an item.
INVENTORY_SLOTS = 23
INVENTORY_BINDINGS = {
    1: ["InvGiveRevive", "InvNumberRevives"],
    2: ["InvGetMoney", "InvSetMoney", "GiveMoney", "TakeMoney"],
    3: ["InvGetSpraycanCharges", "InvSetSpraycanCharges"],
    6: ["InvGiveSkeletonKey", "InvNumberSkeletonKeys"],
}

_DIFFICULTY_SCRIPT = re.compile(r"^config_([a-z]+)\.lua$")


# --- helpers -----------------------------------------------------------------------------------------------------


def _whole(value: Any) -> int | None:
    """A whole number argument as an int, else None."""
    return int(value) if isinstance(value, float) and value.is_integer() else None


def _num(value: Any) -> int | float | None:
    """A numeric argument (whole numbers as ints, others rounded to 6 decimals), else None."""
    if isinstance(value, float):
        return int(value) if value.is_integer() else round(value, 6)
    return None


def _nums(value: Any) -> list[Any] | None:
    """A table of numbers as a list, or None when it is not one."""
    if not isinstance(value, lua4.Table):
        return None
    items = [_num(v) for v in value.as_list()]
    return items if items and all(v is not None for v in items) else None


def _name(value: Any) -> str | None:
    """A global's name or a literal string (not "none"), else None."""
    if isinstance(value, lua4.Global):
        return value.name
    return value if isinstance(value, str) and value != "none" else None


def _shown(items: Iterable[str]) -> list[str] | None:
    """Sorted, at most SHOWN, then "... n more"; None when empty."""
    ordered = sorted(set(items))
    if len(ordered) > SHOWN:
        return [*ordered[:SHOWN], f"... {len(ordered) - SHOWN} more"]
    return ordered or None


def _calls(scripts: Scripts, callee: str, script: str | None = None) -> Iterator[tuple[str, lua4.Call]]:
    """Every call of `callee`, in one script or all, with the script's name."""
    for name, facts in scripts.items():
        if script is None or name == script:
            yield from ((name, call) for call in facts.calls if call.callee == callee)


def _difficulty(script: str) -> str | None:
    """The difficulty a configuration script sets (`config_hard.lua` -> `hard`; the boot script -> `boot`)."""
    if script == CONFIG:
        return BOOT
    match = _DIFFICULTY_SCRIPT.match(script)
    return match.group(1) if match and match.group(1) in DIFFICULTIES else None


def constants(scripts: Scripts, script: str = GLOBAL) -> dict[str, int | float]:
    """The numeric globals a script's main function sets (`RM_Brawl = 1`), last value kept."""
    facts = scripts.get(script)
    found: dict[str, int | float] = {}
    for assignment in facts.assignments if facts else []:
        if assignment.path == "main" and isinstance(assignment.value, float):
            found[assignment.name] = _num(assignment.value)  # type: ignore[assignment]
    return found


def _resolve(value: Any, names: Mapping[str, int | float]) -> int | float | None:
    """A number, or the value of the constant a global names."""
    if isinstance(value, lua4.Global):
        return names.get(value.name)
    return _num(value)


def char_types(scripts: Scripts) -> dict[int, list[Any]]:
    """`CfgChar`'s arguments by character type, from `config_preload2.lua`."""
    return {
        kind: call.args
        for _, call in _calls(scripts, "CfgChar", CONFIG)
        if call.args and (kind := _whole(call.args[0])) is not None
    }


def _models(scripts: Scripts) -> dict[int, str]:
    """Character type -> model name (`CfgChar`'s tenth argument)."""
    return {kind: args[9] for kind, args in char_types(scripts).items() if len(args) > 9 and isinstance(args[9], str)}


def _set_order(script: str) -> tuple[int, str]:
    """Sort key of a difficulty: the game's order, then boot."""
    difficulty = _difficulty(script) or script
    return (DIFFICULTIES.index(difficulty) if difficulty in DIFFICULTIES else len(DIFFICULTIES), difficulty)


# --- inventory items ---------------------------------------------------------------------------------------------


def inventory_items(scripts: Scripts, image: Callable[[str], str | None] | None = None) -> list[dict[str, Any]]:
    """The 23 inventory slots: what `CfgInventoryItem` puts in each, and how the scripts give, test and pocket them.

    `image(name)` returns a thumbnail path for an object name, or None.
    """
    configured: dict[int, list[Any]] = {}
    overrides: dict[int, list[str]] = collections.defaultdict(list)
    for script, call in _calls(scripts, "CfgInventoryItem"):
        slot = _whole(call.args[1]) if len(call.args) > 1 else None
        if slot is None:
            continue
        if script == CONFIG:
            configured[slot] = call.args
        else:
            overrides[slot].append(f"{script.removesuffix('.lua')}: {_name(call.args[0]) or 'none'}")
    counts = {key: collections.Counter[int]() for key in ("given", "tested", "pocketed")}
    users: dict[int, set[str]] = collections.defaultdict(set)
    for key, callee, position in (
        ("given", "InvGiveItem", 0),
        ("tested", "InvNumberOf", 0),
        ("tested", "InvPlayerHasItem", 0),
        ("pocketed", "HuPutItemInPocket", 1),
    ):
        for script, call in _calls(scripts, callee):
            item = _whole(call.args[position]) if len(call.args) > position else None
            if item is not None:
                counts[key][item] += 1
                users[item].add(script)
    entries = []
    for slot in range(INVENTORY_SLOTS):
        args = configured.get(slot)
        obj = _name(args[0]) if args else None
        entries.append(
            {
                "id": slot,
                "object": obj,
                "count": _whole(args[2]) if args else None,
                "sound": _name(args[3]) if args and len(args) > 3 else None,
                "ms": (_whole(args[4]) if len(args) > 4 else 10000) if args else None,
                "bindings": INVENTORY_BINDINGS.get(slot),
                "overrides": overrides.get(slot),
                "given": counts["given"][slot] or None,
                "tested": counts["tested"][slot] or None,
                "pocketed": counts["pocketed"][slot] or None,
                "scripts": _shown(users[slot]),
                "image": image(obj) if image and obj else None,
            }
        )
    return entries


def topic_inventory(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The inventory items, with the object thumbnails that exist."""
    return inventory_items(facts.scripts, _image_finder(images))


def _image_finder(images: Path | None) -> Callable[[str], str | None]:
    """A function from an object name to `objects/<name>.png` when that thumbnail exists."""

    def find(name: str) -> str | None:
        return f"objects/{name}.png" if images is not None and (images / "objects" / f"{name}.png").is_file() else None

    return find


# --- unlockables -------------------------------------------------------------------------------------------------


def unlockables(scripts: Scripts) -> list[dict[str, Any]]:
    """The records of `global.lua`'s `Unlockables` table (which `UM_SetUnlockable` loads by position), with the
    scripts that unlock each by its level, group and item and those that test it by type and data."""
    facts = scripts.get(GLOBAL)
    table = facts.tables.get("Unlockables") if facts else None
    names = constants(scripts)
    models = _models(scripts)
    unlocks: dict[tuple[int, int, int], set[str]] = collections.defaultdict(set)
    for script, call in _calls(scripts, "UM_Unlock"):
        key = tuple(_whole(arg) for arg in call.args[:3])
        if len(key) == 3 and None not in key:
            unlocks[key].add(script)  # type: ignore[index]
    tests: dict[tuple[int, int], set[str]] = collections.defaultdict(set)
    for callee in ("UM_IsDataUnlocked", "UM_IsDataDirty"):
        for script, call in _calls(scripts, callee):
            key = tuple(_whole(arg) for arg in call.args[:2])
            if len(key) == 2 and None not in key:
                tests[key].add(script)  # type: ignore[index]
    entries = []
    for index, row in enumerate(table.as_list() if isinstance(table, lua4.Table) else []):
        if not isinstance(row, lua4.Table):
            continue
        values = row.as_list()
        level, group, item, kind, extra = (_whole(v) for v in [*values, None, None, None, None, None][:5])
        data_value = values[5] if len(values) > 5 else None
        data = _resolve(data_value, names)
        entry: dict[str, Any] = {
            "index": index,
            "kind": UNLOCK_TYPES.get(kind, f"type {kind}") if kind is not None else None,
            "type": kind,
            "level": level,
            "group": group,
            "item": item,
            "extra": extra,
            "data": data,
            "mode": data_value.name if isinstance(data_value, lua4.Global) else None,
            "character": data if kind == 4 else None,
            "model": models.get(data) if kind == 4 and isinstance(data, int) else None,
            "unlocked_by": _shown(unlocks.get((level, group, item), ())),  # type: ignore[arg-type]
            "tested_by": _shown(tests.get((kind, data), ())),  # type: ignore[arg-type]
        }
        entries.append(entry)
    # The page groups by type: list them in type order, each type's records in table order.
    return sorted(entries, key=lambda e: (e["type"] if e["type"] is not None else 99, e["index"]))


def topic_unlockables(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The unlockable records."""
    return unlockables(facts.scripts)


# --- power and Warrior classes -----------------------------------------------------------------------------------

#: `CfgPowerClass`'s arguments 2-31, as the list's field names (the binding's argument order).
POWER_FIELDS = (
    "power_max",
    "power_refill",
    "stamina_max",
    "stamina_refill",
    "down_ms",
    "stun_ms",
    "v32",
    "weapon_stun",
    "hurt",
    "block",
    "block_hurt",
    "stun_hurt",
    "down_hurt",
    "power_hurt",
    "delay",
    "delay_down",
    "grab_divisor",
    "pattern",
    "counter",
    "v38",
    "v39",
    "v3a",
    "v3b",
    "v3c",
    "v3d",
    "v3f",
    "v3e",
    "v40",
    "v41",
    "v42",
)
#: `CfgWarriorClass`'s arguments 2-14, as field names (record offsets in the binding's order).
WARRIOR_FIELDS = (
    "v05",
    "damage_scale",
    "rage_max",
    "rage_gain",
    "rage_decay",
    "rage_drain",
    "v07",
    "mash",
    "v09",
    "v0a",
    "stereo_turns",
    "mugging",
    "v0d",
)


def _class_sets(scripts: Scripts, callee: str) -> dict[tuple[str, int], list[Any]]:
    """`(difficulty, class) -> arguments` for every set of a class binding; the boot copy only where no
    difficulty script sets the class (it is normal's)."""
    found: dict[tuple[str, int], list[Any]] = {}
    for script in sorted(scripts, key=_set_order):
        difficulty = _difficulty(script)
        if difficulty is None:
            continue
        for _, call in _calls(scripts, callee, script):
            number = _whole(call.args[0]) if call.args else None
            if number is not None:
                found[(difficulty, number)] = call.args
    covered = {number for difficulty, number in found if difficulty != BOOT}
    return {key: args for key, args in found.items() if key[0] != BOOT or key[1] not in covered}


def power_classes(scripts: Scripts) -> list[dict[str, Any]]:
    """Every power class each difficulty sets, with the character types that use it there: `config_preload2.lua`'s
    `CfgChar` power class (`v11d`), as the difficulty script's own `CfgChar` calls change it."""
    players: dict[int, set[str]] = collections.defaultdict(set)
    for _, power, name in WARRIOR_TYPES.values():
        players[power].add(name)
    players[OTHER_POWER_CLASS].add("any other Warriors type")
    users = {difficulty: _power_users(scripts, difficulty) for difficulty in (*DIFFICULTIES, BOOT)}
    entries = []
    for (difficulty, number), args in sorted(_class_sets(scripts, "CfgPowerClass").items(), key=_class_order):
        entry: dict[str, Any] = {"id": f"{difficulty}:{number}", "difficulty": difficulty, "class": number}
        entry.update({name: _num(value) for name, value in zip(POWER_FIELDS, args[1:], strict=False)})
        entry["players"] = ", ".join(sorted(players[number])) or None
        entry["types"] = users[difficulty].get(number)
        entries.append(entry)
    return entries


def _power_users(scripts: Scripts, difficulty: str) -> dict[int, list[int]]:
    """Power class -> the character types an AI human of the difficulty gets it with."""
    classes = {kind: args[4] for kind, args in char_types(scripts).items() if len(args) > 4}
    for _, call in _calls(scripts, "CfgChar", f"config_{difficulty}.lua"):
        if len(call.args) > 4 and (kind := _whole(call.args[0])) is not None:
            classes[kind] = call.args[4]
    users: dict[int, list[int]] = collections.defaultdict(list)
    for kind, value in sorted(classes.items()):
        if (number := _whole(value)) is not None:
            users[number].append(kind)
    return users


def _class_order(item: tuple[tuple[str, int], Any]) -> tuple[int, int]:
    """Sort key of a class set: the difficulty's order, then the class number."""
    (difficulty, number), _ = item
    order = (*DIFFICULTIES, BOOT)
    return (order.index(difficulty), number)


def warrior_classes(scripts: Scripts) -> list[dict[str, Any]]:
    """Every Warrior class each difficulty sets, with the character types that get it as a player."""
    types: dict[int, list[int]] = collections.defaultdict(list)
    names: dict[int, str] = {OTHER_WARRIOR_CLASS: "everyone else"}
    for kind, (warrior, _power, name) in sorted(WARRIOR_TYPES.items()):
        types[warrior].append(kind)
        names[warrior] = name
    power_of = {warrior: power for warrior, power, _ in WARRIOR_TYPES.values()}
    entries = []
    for (difficulty, number), args in sorted(_class_sets(scripts, "CfgWarriorClass").items(), key=_class_order):
        entry: dict[str, Any] = {
            "id": f"{difficulty}:{number}",
            "difficulty": difficulty,
            "class": number,
            "warrior": names.get(number),
        }
        entry.update({name: _num(value) for name, value in zip(WARRIOR_FIELDS, args[1:], strict=False)})
        entry["power_class"] = power_of.get(number, OTHER_POWER_CLASS)
        entry["types"] = types.get(number)
        entries.append(entry)
    return entries


def topic_power_classes(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The power classes per difficulty."""
    return power_classes(facts.scripts)


def topic_warrior_classes(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The Warrior classes per difficulty."""
    return warrior_classes(facts.scripts)


# --- attack kinds and tables -------------------------------------------------------------------------------------


def attacks(scripts: Scripts) -> list[dict[str, Any]]:
    """The 45 attack kinds (with their AI delay, command, anim ids and Rumble weights), then every 45-entry attack,
    damage and range table each configuration script defines, with the types `CfgChar` gives it."""
    delays = {
        kind: _whole(call.args[1])
        for _, call in _calls(scripts, "CfgAttackDelay", CONFIG)
        if len(call.args) > 1 and (kind := _whole(call.args[0])) is not None
    }
    weights: dict[int, list[str]] = collections.defaultdict(list)
    for script, call in _calls(scripts, "BrSetAttackWeight"):
        kind = _whole(call.args[1]) if len(call.args) > 2 else None
        if kind is not None:
            weights[kind].append(f"{script.removesuffix('.lua')} {_whole(call.args[2])}")
    entries: list[dict[str, Any]] = [
        {
            "id": f"kind-{kind}",
            "kind": "attack kind",
            "number": kind,
            "delay": delays.get(kind),
            "command": _hex(ATTACK_COMMANDS.get(kind)),
            "anim_ids": ATTACK_ANIMS.get(kind),
            "rumble_weights": sorted(set(weights[kind])) or None,
        }
        for kind in range(ATTACK_KINDS)
    ]
    used: dict[str, set[int]] = collections.defaultdict(set)
    for kind, args in char_types(scripts).items():
        for position in (6, 7, 13):
            if len(args) > position and (name := _name(args[position])):
                used[name].add(kind)
    for script in sorted(scripts, key=_set_order):
        difficulty = _difficulty(script)
        if difficulty is None:
            continue
        for name, table in sorted(scripts[script].tables.items()):
            kind_name = next((label for prefix, label in _TABLE_KIND if name.startswith(prefix)), None)
            values = _nums(table)
            if kind_name is None or values is None or len(values) != ATTACK_KINDS:
                continue
            entries.append(
                {
                    "id": f"{difficulty}:{name}",
                    "kind": f"{kind_name} table",
                    "table": name,
                    "difficulty": difficulty,
                    "values": values,
                    "types": _count(used.get(name)) if difficulty == BOOT else None,
                }
            )
    return entries


def _hex(value: int | str | None) -> str | None:
    """A command id as `0x..`, or the text the table gives."""
    return f"0x{value:02x}" if isinstance(value, int) else value


def _count(kinds: set[int] | None) -> str | None:
    """`n types` for a table's users, or None."""
    return f"{len(kinds)} types" if kinds else None


def topic_attacks(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The attack kinds and the attack, damage and range tables."""
    return attacks(facts.scripts)


# --- hat fittings ------------------------------------------------------------------------------------------------


def hat_fittings(scripts: Scripts, image: Callable[[str], str | None] | None = None) -> list[dict[str, Any]]:
    """Every `CfgHat` fitting: the hat-fit set, the character type it is looked up by, the hat and its transform."""
    models = _models(scripts)
    seen: collections.Counter[str] = collections.Counter()
    entries = []
    for _, call in _calls(scripts, "CfgHat", CONFIG):
        if len(call.args) < 5:
            continue
        number, owner, hat = _whole(call.args[0]), _whole(call.args[1]), _name(call.args[2])
        key = f"{owner}:{hat}"
        seen[key] += 1
        entries.append(
            {
                "id": key if seen[key] == 1 else f"{key}#{seen[key]}",
                "wearer": f"{owner} {models.get(owner, '')}".strip() if owner is not None else None,
                "set": number,
                "type": owner,
                "hat": hat,
                "pos": _nums(call.args[3]),
                "rot": _nums(call.args[4]),
                "image": image(hat) if image and hat else None,
            }
        )
    return sorted(entries, key=lambda e: (e["type"] or 0, e["hat"] or ""))


def topic_hats(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The hat fittings, with the hat thumbnails that exist."""
    return hat_fittings(facts.scripts, _image_finder(images))


# --- the Rumble roster -------------------------------------------------------------------------------------------


def rumble(scripts: Scripts) -> list[dict[str, Any]]:
    """Rumble's modes, arenas, gangs and characters, each with the unlockable record that gates it.

    Names, titles and descriptions are the game's text and are never read; a character is named by its type.
    """
    names = constants(scripts)
    unlock_index: dict[tuple[int, Any], int] = {}
    for record in unlockables(scripts):
        unlock_index.setdefault((record["type"], record["data"]), record["index"])
    models = _models(scripts)
    entries: list[dict[str, Any]] = []
    for _, call in _calls(scripts, "CfgRumbleGame"):
        args = call.args
        mode_name = args[1].name if len(args) > 1 and isinstance(args[1], lua4.Global) else None
        number = _resolve(args[1], names) if len(args) > 1 else None
        entries.append(
            {
                "id": f"mode-{mode_name or number}",
                "kind": "mode",
                "number": number,
                "name": mode_name,
                "one_player": _flag(args, 2),
                "coop": _flag(args, 3),
                "versus": _flag(args, 4),
                "fighters": _whole(args[6]) if len(args) > 6 else None,
                "presets": [v for v in (_whole(a) for a in args[7:9]) if v] or None,
                "unlock": unlock_index.get((1, number)),
            }
        )
    for _, call in _calls(scripts, "CfgRumbleArena"):
        level = _whole(call.args[0]) if call.args else None
        modes = call.args[2] if len(call.args) > 2 else None
        allowed = (
            [m.name for m in modes.as_list() if isinstance(m, lua4.Global)] if isinstance(modes, lua4.Table) else []
        )
        entries.append(
            {
                "id": f"arena-{level}",
                "kind": "arena",
                "number": level,
                "value": _whole(call.args[1]) if len(call.args) > 1 else None,
                "modes": allowed or ["all"],
                "unlock": unlock_index.get((2, level)),
            }
        )
    for _, call in _calls(scripts, "CfgRumbleGang"):
        gang = _whole(call.args[0]) if call.args else None
        entries.append(
            {
                "id": f"gang-{gang}",
                "kind": "gang",
                "number": gang,
                "members": _nums(call.args[2]) if len(call.args) > 2 else None,
                "unlock": unlock_index.get((3, gang)) if gang is not None and gang >= 0 else None,
            }
        )
    characters: dict[int, dict[str, Any]] = {}
    for _, call in _calls(scripts, "CfgRumbleChar"):
        args = call.args
        kind = _whole(args[0]) if args else None
        if kind is None or len(args) < 7:
            continue
        characters[kind] = {
            "id": f"char-{kind}",
            "kind": "character",
            "number": kind,
            "character": kind,
            "model": models.get(kind),
            "gang": _whole(args[3]),
            "rank": args[4].name if isinstance(args[4], lua4.Global) else None,
            "v0e": _whole(args[5]),
            "strength": _whole(args[6]),
            "unlock": unlock_index.get((4, kind)),
        }
    entries += [characters[kind] for kind in sorted(characters)]
    order = ("mode", "arena", "gang", "character")
    return sorted(entries, key=lambda e: (order.index(e["kind"]), e["number"] if e["number"] is not None else -1))


def _flag(args: list[Any], position: int) -> bool | None:
    """A boolean-ish argument (a number or a `true`/`false` global) as a bool."""
    if len(args) <= position:
        return None
    value = args[position]
    if isinstance(value, lua4.Global) and value.name in ("true", "false"):
        return value.name == "true"
    number = _whole(value)
    return bool(number) if number is not None else None


def topic_rumble(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The Rumble roster."""
    return rumble(facts.scripts)


# --- statistics --------------------------------------------------------------------------------------------------


def statistics(scripts: Scripts) -> list[dict[str, Any]]:
    """The six scoring categories (with `config_preload2.lua`'s maxima), each category's points per counted event
    (`CfgSetStatValue`), and the per-level maxima of `global.lua`'s `PointsMatrix`."""
    values: dict[tuple[int, int], int] = {}
    setters: dict[tuple[int, int], set[str]] = collections.defaultdict(set)
    for script, call in _calls(scripts, "CfgSetStatValue"):
        group, index, value = (_whole(arg) for arg in [*call.args, None, None, None][:3])
        if group is None or index is None:
            continue
        setters[(group, index)].add(script)
        if script == CONFIG or (group, index) not in values:
            values[(group, index)] = value  # type: ignore[assignment]
    maxima: dict[str, int | None] = {}
    for _, call in _calls(scripts, "CfgSetStatTypeMax", CONFIG):
        maxima = {name: _whole(arg) for name, arg in zip(STAT_MAX_ARGS, call.args, strict=False)}
    adds: dict[tuple[int, int], set[str]] = collections.defaultdict(set)
    for script, call in _calls(scripts, "StatAdd"):
        group, index = (_whole(arg) for arg in [*call.args, None, None, None][1:3])
        if group is not None and index is not None:
            adds[(group, index)].add(script)
    entries: list[dict[str, Any]] = []
    for group, name in enumerate(STAT_GROUPS):
        entries.append({"id": name, "kind": "category", "group": group, "category": name, "max": maxima.get(name)})
    for group, size in enumerate(STAT_SIZES):
        for index in range(size):
            entries.append(
                {
                    "id": f"{group}-{index}",
                    "kind": "event",
                    "group": group,
                    "category": STAT_GROUPS[group],
                    "index": index,
                    "points": values.get((group, index)),
                    "set_by": _shown(s for s in setters[(group, index)] if s != CONFIG),
                    "added_by": _shown(adds[(group, index)]),
                }
            )
    facts = scripts.get(GLOBAL)
    matrix = facts.tables.get("PointsMatrix") if facts else None
    points = facts.tables.get("Pts") if facts else None
    named = {str(k): _num(v) for k, v in points.fields.items()} if isinstance(points, lua4.Table) else {}
    for level, row in sorted(matrix.items.items()) if isinstance(matrix, lua4.Table) else []:
        if not isinstance(row, lua4.Table):
            continue
        entry: dict[str, Any] = {"id": f"level-{level}", "kind": "level", "level": level}
        for name in STAT_GROUPS:
            given = row.fields.get(name)
            if isinstance(given, lua4.Global):
                entry[name] = named.get(given.name.removeprefix("Pts."))
            else:
                entry[name] = _num(given)
        entries.append(entry)
    return entries


def topic_statistics(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The statistics."""
    return statistics(facts.scripts)


# --- the topics --------------------------------------------------------------------------------------------------

INVENTORY = Topic(
    "inventory",
    "id",
    "item",
    (
        F(
            "id",
            "int",
            "Item id: what `InvGiveItem`, `InvNumberOf` and `HuPutItemInPocket` take (0-22).",
            "Item",
            required=True,
        ),
        F("object", "str", "The [object](objects.md) `CfgInventoryItem` makes the item (`none` for money).", "Object"),
        F("meaning", "str", "What the item is, in our words.", "What", curated=True),
        F("count", "int", "`CfgInventoryItem`'s third argument, stored with the item (`+0x20`)."),
        F("sound", "str", "The pickup sound (stored as its hash).", "Pickup sound"),
        F("ms", "int", "Duration in ms (`+0x28`; default 10,000).", "Duration (ms)"),
        F("bindings", "list", "Bindings made for this item alone.", "Own bindings"),
        F(
            "overrides",
            "list",
            "Level scripts that configure the slot again, with the object they give it.",
            "Reconfigured by",
        ),
        F("given", "int", "`InvGiveItem` calls with this item.", "Given"),
        F("tested", "int", "`InvNumberOf` and `InvPlayerHasItem` calls with it.", "Tested"),
        F("pocketed", "int", "`HuPutItemInPocket` calls that put it in a human's pocket.", "Pocketed"),
        F("scripts", "list", "Scripts that give, test or pocket it.", "Scripts"),
    ),
    nav="Inventory items",
    images=True,
)

UNLOCKABLES = Topic(
    "unlockables",
    "index",
    "unlock",
    (
        F(
            "index",
            "int",
            "Record index: the position in `Unlockables` minus one (`UM_IsUnlocked`).",
            "Record",
            required=True,
        ),
        F("kind", "str", "What the type unlocks, in our words.", prose=True),
        F("type", "int", "Byte 3: the type `UM_IsDataUnlocked` and the code test.", "Type"),
        F("level", "int", "Byte 0: the level `UM_Unlock` matches (0 the game's start).", "Level"),
        F("group", "int", "Byte 1: a group within the level (1 the hub's clubhouse unlocks).", "Group"),
        F("item", "int", "Byte 2: the item within the group (0 the level-complete record).", "Item"),
        F("extra", "int", "16-bit `+4` (`UM_GetRecordData` field 4).", "Extra"),
        F(
            "data",
            "int",
            "32-bit `+8`: the data id tested with the type (a level, mode, gang, type, hint ...).",
            "Data",
        ),
        F("mode", "str", "For a Rumble mode, the `RM_*` constant `global.lua` names.", "Mode"),
        F("character", "int", "For a Rumble character, the character type.", "Character", link="characters.md#char"),
        F("model", "str", "That type's model.", "Model"),
        F("meaning", "str", "What the record unlocks, in our words.", "What", curated=True),
        F("unlocked_by", "list", "Scripts that unlock it by its level, group and item with constants.", "Unlocked by"),
        F("tested_by", "list", "Scripts that test its type and data with constants.", "Tested by"),
    ),
    group_by="kind",
    compact=True,
    nav="Unlockables",
)

_POWER_DOCS = (
    ("power_max", "`+0x28`: the power meter's maximum.", "Power"),
    ("power_refill", "`+0x2a`: power refill per second.", "Power refill/s"),
    ("stamina_max", "`+0x2c`: stamina maximum.", "Stamina"),
    ("stamina_refill", "`+0x2e`: stamina refill per second.", "Stamina refill/s"),
    ("down_ms", "`+0x34`: ms a knocked-down human stays down.", "Down ms"),
    ("stun_ms", "`+0x30`: ms a stun lasts.", "Stun ms"),
    ("v32", "`+0x32` (16-bit); not traced.", "+0x32"),
    (
        "weapon_stun",
        "`+0x00`: as the attacker, scales the victim's stun for weapon hits of 20-49 damage.",
        "Weapon stun",
    ),
    ("hurt", "`+0x04`: the hurt threshold, a fraction of maximum health.", "Hurt"),
    ("block", "`+0x08`: the AI's block chance.", "Block"),
    ("block_hurt", "`+0x0c`: the AI's block chance while hurt.", "Block hurt"),
    ("stun_hurt", "`+0x10`: the stun time's factor while hurt.", "Stun x"),
    ("down_hurt", "`+0x14`: the down time's factor while hurt.", "Down x"),
    ("power_hurt", "`+0x18`: the power maximum's factor while hurt.", "Power x"),
    ("delay", "`+0x1c`: AI only, the factor on the [attack delay](attacks.md#attack).", "Delay x"),
    ("delay_down", "`+0x20`: the same when the target is down.", "Delay down x"),
    (
        "grab_divisor",
        "`+0x36`, a byte (3.5 is stored as 3): a grabbed human's strike costs the grabber 1 / this of its power.",
        "Grab divisor",
    ),
    ("pattern", "`+0x37` (0-15): the AI's pattern-reading threshold.", "Pattern"),
    ("counter", "`+0x24` (0-1): the AI's counter chance.", "Counter"),
    ("v38", "`+0x38` (0-127); not traced.", "+0x38"),
    ("v39", "`+0x39` (0 or 1); not traced.", "+0x39"),
    ("v3a", "`+0x3a`; not traced.", "+0x3a"),
    ("v3b", "`+0x3b`; not traced.", "+0x3b"),
    ("v3c", "`+0x3c`; not traced.", "+0x3c"),
    ("v3d", "`+0x3d`; not traced.", "+0x3d"),
    ("v3f", "`+0x3f` (-1, 0 or 1); not traced.", "+0x3f"),
    ("v3e", "`+0x3e` (-1, 0 or 1); not traced.", "+0x3e"),
    ("v40", "`+0x40` (0-100, a percentage); not traced.", "+0x40"),
    ("v41", "`+0x41` (0 or 1); not traced.", "+0x41"),
    ("v42", "`+0x42` (0 or 1); not traced.", "+0x42"),
)

POWER_CLASSES = Topic(
    "power-classes",
    "id",
    "power",
    (
        F("id", "str", "`<difficulty>:<class>`.", "Id", required=True),
        F("difficulty", "str", "The difficulty script that sets it (`boot`: `config_preload2.lua` only)."),
        F(
            "class",
            "int",
            "Power class (`CfgPowerClass`'s first argument; record `0x006619a0 + class x 0x44`).",
            "Class",
        ),
        *(F(name, "float", doc, column) for name, doc, column in _POWER_DOCS),
        F("players", "str", "The Warriors who get the class as a player (by character type).", "Players", prose=True),
        F(
            "types",
            "list",
            "Character types an AI human gets it with on this difficulty (`CfgChar`'s `v11d`).",
            "AI types",
            link="characters.md#char",
        ),
    ),
    group_by="difficulty",
    compact=True,
    nav="Power classes",
)

WARRIOR_CLASSES = Topic(
    "warrior-classes",
    "id",
    "warrior",
    (
        F("id", "str", "`<difficulty>:<class>`.", "Id", required=True),
        F("difficulty", "str", "The difficulty script that sets it (`boot`: `config_preload2.lua` only)."),
        F(
            "class",
            "int",
            "Warrior class (`CfgWarriorClass`'s first argument; record `0x006b65c0 + class x 14`).",
            "Class",
        ),
        F("warrior", "str", "Who gets it as a player.", "Warrior", prose=True),
        F("v05", "int", "Byte `+0x05`; not traced.", "+0x05"),
        F("damage_scale", "int", "Byte `+0x06`: the player's damage scale in percent.", "Damage %"),
        F("rage_max", "int", "16-bit `+0x00`: the rage maximum.", "Rage max"),
        F("rage_gain", "int", "Byte `+0x02`: the rage gain percentage.", "Rage gain %"),
        F("rage_decay", "int", "Byte `+0x03`: idle rage decay (percent of the maximum per 20 s).", "Rage decay"),
        F(
            "rage_drain",
            "int",
            "Byte `+0x04`: rage drain while raging (percent of the maximum per 20 s).",
            "Rage drain",
        ),
        F("v07", "int", "Byte `+0x07` (1-3); not traced.", "+0x07"),
        F("mash", "int", "Byte `+0x08`: picks the button-mash gain factor.", "Mash"),
        F("v09", "int", "Byte `+0x09` (1-3); not traced.", "+0x09"),
        F("v0a", "int", "Byte `+0x0a`; not traced.", "+0x0a"),
        F("stereo_turns", "int", "Byte `+0x0b`: the car stereo theft's turns.", "Stereo"),
        F("mugging", "int", "Byte `+0x0c`: picks the mugging parameters.", "Mugging"),
        F("v0d", "int", "Byte `+0x0d`; not traced.", "+0x0d"),
        F(
            "power_class",
            "int",
            "The [power class](power-classes.md) the same Warriors get as a player.",
            "Power class",
        ),
        F("types", "list", "Character types that get this class as a player.", "Types", link="characters.md#char"),
    ),
    group_by="difficulty",
    compact=True,
    nav="Warrior classes",
)

ATTACKS = Topic(
    "attacks",
    "id",
    "attack",
    (
        F("id", "str", "`kind-<n>` for an attack kind; `<difficulty>:<table>` for a table.", "Id", required=True),
        F("kind", "str", "`attack kind`, or the table's kind.", prose=True),
        F("number", "int", "The attack kind (0-44): the index of every 45-entry table.", "Kind"),
        F("meaning", "str", "What the attack is, in our words.", "What", curated=True),
        F(
            "delay",
            "int",
            "`CfgAttackDelay`: ms an AI human waits before this attack, times its power class's factor.",
            "Delay (ms)",
        ),
        F("command", "str", "The command id the AI writes for it (`AttackKind_ToCommand`).", "Command"),
        F(
            "anim_ids",
            "list",
            "The anim ids a damage table's value at this index is written to.",
            "Anim ids",
            link="anim-ids.md#anim",
        ),
        F(
            "rumble_weights",
            "list",
            "`BrSetAttackWeight` calls of the Rumble scripts (script, weight).",
            "Rumble weights",
        ),
        F("table", "str", "The table's global (the name `CfgChar` passes).", "Table"),
        F(
            "difficulty",
            "str",
            "Where it is defined: `boot` (`config_preload2.lua`) or a difficulty script that redefines it.",
            "Defined in",
        ),
        F("values", "list", "The 45 values by attack kind: a weight, a damage or a far range.", "Values"),
        F("types", "str", "How many character types `CfgChar` gives the table.", "Used by"),
    ),
    group_by="kind",
    compact=True,
    nav="Attack kinds and tables",
)

HATS = Topic(
    "hats",
    "id",
    "hat",
    (
        F("id", "str", "`<type>:<hat>`.", "Id", required=True),
        F("wearer", "str", "The character type and model the set is looked up by."),
        F("set", "int", "Hat-fit set (`CfgHat`'s first argument; record `0x00662b70 + set x 0xa90`).", "Set"),
        F("type", "int", "Character type the set belongs to (record `+0xa80`).", "Type", link="characters.md#char"),
        F("hat", "str", "The hat [object](objects.md).", "Hat"),
        F("pos", "list", "Offset `{x, y, z}` in metres from the head.", "Offset"),
        F("rot", "list", "Rotation quaternion `{i, j, k, r}`.", "Rotation"),
    ),
    group_by="wearer",
    compact=True,
    nav="Hat fittings",
    images=True,
)

RUMBLE = Topic(
    "rumble",
    "id",
    "rumble",
    (
        F("id", "str", "`mode-<RM name>`, `arena-<level>`, `gang-<id>` or `char-<type>`.", "Id", required=True),
        F("kind", "str", "`mode`, `arena`, `gang` or `character`.", prose=True),
        F("number", "int", "The mode's value, the arena's level number, the gang id or the character type.", "Number"),
        F("name", "str", "A mode's `RM_*` constant.", "Mode"),
        F("one_player", "bool", "A mode offers one player against the computer.", "1P"),
        F("coop", "bool", "A mode offers two players together.", "Co-op"),
        F("versus", "bool", "A mode offers two players against each other.", "Versus"),
        F("fighters", "int", "Fighters per side.", "Fighters"),
        F(
            "presets",
            "list",
            "The character types a mode fills both sides with.",
            "Preset types",
            link="characters.md#char",
        ),
        F("value", "int", "An arena's second argument (9 in every call).", "Value"),
        F("modes", "list", "The modes an arena allows (`all` when its table starts with 0).", "Modes"),
        F("members", "list", "A gang's nine character types.", "Members", link="characters.md#char"),
        F("character", "int", "A character's type.", "Character", link="characters.md#char"),
        F("model", "str", "Its model.", "Model"),
        F("gang", "int", "The gang id a character belongs to.", "Gang"),
        F("rank", "str", "Its rank (`RM_WARCHIEF`, `RM_LT`, `RM_SOLDIER`, `RM_BOSS` ...).", "Rank"),
        F("v0e", "int", "`CfgRumbleChar`'s sixth argument (16-bit `+0x0e`, 0 in every call).", "+0x0e"),
        F("strength", "int", "Seventh argument (byte `+0x10`, 20-80): a strength rating (inferred).", "Strength"),
        F(
            "unlock",
            "int",
            "The unlockable record that gates it, when there is one.",
            "Unlock",
            link="unlockables.md#unlock",
        ),
    ),
    group_by="kind",
    compact=True,
    nav="Rumble roster",
)

STATISTICS = Topic(
    "statistics",
    "id",
    "stat",
    (
        F(
            "id",
            "str",
            "A category's name, `<group>-<index>` for an event, `level-<n>` for a level.",
            "Id",
            required=True,
        ),
        F("kind", "str", "`category`, `event` or `level`.", prose=True),
        F("group", "int", "The category's number: `StatAdd`'s group and `CfgSetStatValue`'s table.", "Group"),
        F("category", "str", "Mission, bonus, style, combat, crime or harmony.", "Category"),
        F("index", "int", "The event's index in its category.", "Index"),
        F("meaning", "str", "What the game counts there, in our words.", "What", curated=True),
        F("points", "int", "Points per event (`CfgSetStatValue` in `config_preload2.lua`).", "Points"),
        F("max", "int", "The category's default maximum (`CfgSetStatTypeMax` in `config_preload2.lua`).", "Max"),
        F("set_by", "list", "Other scripts that set the event's points.", "Also set by"),
        F("added_by", "list", "Scripts that count the event with `StatAdd`.", "Counted by scripts"),
        F("level", "int", "A level's number in `PointsMatrix`.", "Level"),
        F("mission", "float", "The level's mission value (`PointsMatrix`).", "Mission"),
        F("bonus", "float", "Its bonus value.", "Bonus"),
        F("style", "int", "Its style maximum (`Pts.STYLE_*`).", "Style"),
        F("combat", "int", "Its combat maximum (`Pts.COMBAT_*`).", "Combat"),
        F("crime", "int", "Its crime maximum (`Pts.CRIME_*`).", "Crime"),
        F("harmony", "int", "Its harmony maximum (`Pts.HARM_*`).", "Harmony"),
    ),
    group_by="kind",
    compact=True,
    nav="Statistics",
)

#: The topics of this module, in index order.
TOPICS = (INVENTORY, UNLOCKABLES, POWER_CLASSES, WARRIOR_CLASSES, ATTACKS, HATS, RUMBLE, STATISTICS)

#: `coney-tools refs extract`'s readers for them.
EXTRACTORS = {
    "inventory": topic_inventory,
    "unlockables": topic_unlockables,
    "power-classes": topic_power_classes,
    "warrior-classes": topic_warrior_classes,
    "attacks": topic_attacks,
    "hats": topic_hats,
    "rumble": topic_rumble,
    "statistics": topic_statistics,
}

#: The starting prose and defaults of each list (`refs_cli.new_list`); the YAML keeps them once written.
STARTERS: dict[str, dict[str, Any]] = {
    "inventory": {
        "title": "Inventory items",
        "source": "config_preload2.lua, CfgInventoryItem; the Inv bindings",
        "evidence": "confirmed-code",
        "about": "The 23 item slots of each player's inventory, ids 0-22 (`0x0041e420`). `CfgInventoryItem(object,\n"
        "id, count, sound, ms)` gives a slot its object, pickup sound and duration in both players'\n"
        "inventories; `InvGiveItem(id, n)` adds to a player's count, `InvNumberOf(id)` reads it and\n"
        "`HuPutItemInPocket(human, id, n)` puts an item in a human's pocket (human `+0x250`, the count at\n"
        "`+0x254`), to be taken when the human is beaten or mugged.",
        "complete": "",
    },
    "unlockables": {
        "title": "Unlockables",
        "source": "global.lua, Unlockables",
        "about": "",
        "complete": "",
    },
    "power-classes": {
        "title": "Power classes",
        "source": "config_<difficulty>.lua, CfgPowerClass",
        "evidence": "confirmed-code",
        "about": "",
        "complete": "",
    },
    "warrior-classes": {
        "title": "Warrior classes",
        "source": "config_<difficulty>.lua, CfgWarriorClass",
        "evidence": "confirmed-code",
        "about": "",
        "complete": "",
    },
    "attacks": {
        "title": "Attack kinds and tables",
        "source": "config_preload2.lua and the difficulty scripts",
        "about": "",
        "complete": "",
    },
    "hats": {
        "title": "Hat fittings",
        "source": "config_preload2.lua, CfgHat",
        "evidence": "confirmed-code",
        "about": "",
        "complete": "",
    },
    "rumble": {
        "title": "Rumble roster",
        "source": "rumble_data.lua, rumble_arena.lua, rumble_gang.lua and the Rumble character scripts",
        "evidence": "confirmed-code",
        "about": "",
        "complete": "",
    },
    "statistics": {
        "title": "Statistics",
        "source": "config_preload2.lua, CfgSetStatValue and CfgSetStatTypeMax; global.lua, PointsMatrix",
        "evidence": "confirmed-code",
        "about": "",
        "complete": "",
    },
}
