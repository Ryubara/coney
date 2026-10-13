# SPDX-License-Identifier: GPL-3.0-or-later
"""The characters page's stats columns: each character type joined with the power and Warrior classes it gets.

A character's stats come from four configuration tables ([Characters: stats](../research/characters.md#stats)):
its own `CfgChar` record, the power class that record names (`v11d`), and for a player the Warrior class and power
class his type picks. The characters list holds only the first; this module joins in the others from their lists,
on normal difficulty (the one `config_preload2.lua` sets at boot), so the page shows a type's numbers in one row:

- **Stats (normal)**: the power class an AI human of the type fights with and its power and stamina maxima; for a
  player whose class differs (a Warriors type, category 14), his own; and for the nine Warriors, his Warrior class's
  Strength (damage %), Health % (with the health it gives) and rage maximum.
- **Combat**: the damage table times the damage scale, the attack table and the range table.
- **Drop**: the object or object group the human may carry and the `+0xb4` value used with it.

Research: docs/research/characters.md#power-classes, docs/references/warrior-classes.md
"""

from __future__ import annotations

import math
from typing import Any

from coney_tools.refs import RefList
from coney_tools.refs_render import ExtraColumn

#: The difficulty whose classes the page shows: normal's, which `config_preload2.lua` also sets at boot.
DIFFICULTY = "normal"
#: The category of the Warriors (`CfgChar` byte `+0x11b`), whose players take their Warrior's power class.
WARRIORS_CATEGORY = 14
#: The power class a player of a Warriors type with no Warrior class plays with (`0x00222ba8`).
OTHER_WARRIORS_POWER_CLASS = 7

STATS_HEADING = "Stats (normal)"
COMBAT_HEADING = "Combat"
DROP_HEADING = "Drop"


def _by_class(reflist: RefList) -> dict[int, dict[str, Any]]:
    """The DIFFICULTY entries of a power or Warrior class list, by class number."""
    return {entry["class"]: entry for entry in reflist.resolved() if entry.get("difficulty") == DIFFICULTY}


def _power_text(power_class: int, classes: dict[int, dict[str, Any]]) -> str:
    """`class [n](...)` with its power and stamina maxima, or the bare class when the list lacks it."""
    text = f"class [{power_class}](power-classes.md#power-{DIFFICULTY}-{power_class})"
    found = classes.get(power_class)
    if found is None:
        return text
    return f"{text}, power {found.get('power_max')}, stamina {found.get('stamina_max')}"


def _scaled_health(health: Any, percent: Any) -> str:
    """` (n)`: the health a player of the type has, the type's times the Warrior class's percentage, rounded."""
    if not isinstance(health, int) or not isinstance(percent, int):
        return ""
    return f" ({math.floor(health * percent / 100 + 0.5)})"


def _stats_cell(
    entry: dict[str, Any],
    powers: dict[int, dict[str, Any]],
    warriors: dict[int, dict[str, Any]],
    warrior_of_type: dict[int, int],
) -> str:
    """The Stats cell of one type: its AI class, its player class when that differs, and its Warrior class."""
    own = entry.get("v11d")
    if not isinstance(own, int):
        return ""
    lines = [f"AI: {_power_text(own, powers)}"]
    warrior_class = warrior_of_type.get(entry["id"])
    # A Warriors type's player takes his Warrior's power class, 7 when he has none; any other keeps the type's own.
    player = own
    if entry.get("category") == WARRIORS_CATEGORY:
        player = OTHER_WARRIORS_POWER_CLASS
        if warrior_class is not None:
            player = warriors[warrior_class].get("power_class", OTHER_WARRIORS_POWER_CLASS)
    if player != own:
        lines.append(f"Player: {_power_text(player, powers)}")
    if warrior_class is not None:
        record = warriors[warrior_class]
        lines.append(
            f"Warrior class [{warrior_class}](warrior-classes.md#warrior-{DIFFICULTY}-{warrior_class}): "
            f"Strength {record.get('damage_scale')} %, "
            f"Health {record.get('health_scale')} %{_scaled_health(entry.get('health'), record.get('health_scale'))}, "
            f"Rage max {record.get('rage_max')}"
        )
    return "<br>".join(lines)


def _number(value: Any) -> str:
    """A scale as written in the scripts: 1, not 1.0."""
    return str(int(value)) if isinstance(value, float) and value.is_integer() else str(value)


def _combat_cell(entry: dict[str, Any]) -> str:
    """The damage table times its scale, the attack table and the range table; empty when the type names none."""
    parts = []
    if entry.get("damage_table"):
        scale = entry.get("damage_scale")
        parts.append(f"`{entry['damage_table']}`" + (f" x {_number(scale)}" if scale is not None else ""))
    parts += [f"`{entry[name]}`" for name in ("attack_table", "range_table") if entry.get(name)]
    return ", ".join(parts)


def _drop_cell(entry: dict[str, Any]) -> str:
    """The carried object or group and the `+0xb4` value: -1 means the class money range (crimes.md)."""
    group = entry.get("drop_group")
    chance = entry.get("drop_chance")
    if chance == -1:
        value = "-1 (the class money range)"
    elif isinstance(chance, int) and (chance != 0 or group):
        value = f"chance {chance}"
    else:
        value = ""
    if group and group != "none":
        return f"`{group}`, {value}" if value else f"`{group}`"
    return value


def character_columns(characters: RefList, power_classes: RefList, warrior_classes: RefList) -> list[ExtraColumn]:
    """The characters page's extra columns (Stats, Combat, Drop), their cells by character type."""
    powers = _by_class(power_classes)
    warriors = _by_class(warrior_classes)
    warrior_of_type = {kind: number for number, record in warriors.items() for kind in record.get("types") or []}
    stats: dict[Any, str] = {}
    combat: dict[Any, str] = {}
    drop: dict[Any, str] = {}
    for entry in characters.resolved():
        # Only filled cells are kept: a type with none gets the empty cell the table writes for a missing key.
        for cells, text in (
            (stats, _stats_cell(entry, powers, warriors, warrior_of_type)),
            (combat, _combat_cell(entry)),
            (drop, _drop_cell(entry)),
        ):
            if text:
                cells[entry["id"]] = text
    return [
        ExtraColumn(STATS_HEADING, "health", stats),
        ExtraColumn(COMBAT_HEADING, "health", combat),
        ExtraColumn(DROP_HEADING, "weapon", drop),
    ]


def extra_columns(lists: dict[str, RefList]) -> dict[str, list[ExtraColumn]]:
    """Every page's extra columns by topic key, from all the lists by key; only the characters page has any."""
    needed = ("characters", "power-classes", "warrior-classes")
    if not all(key in lists for key in needed):
        return {}
    return {"characters": character_columns(*(lists[key] for key in needed))}
