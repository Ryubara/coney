# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the gameplay reference lists: inventory, unlockables, classes, attacks, hats, Rumble and statistics.

Synthetic script facts only; no game data.
"""

from __future__ import annotations

from coney_tools import refs_play
from coney_tools.lua4 import Assignment, Call, ChunkFacts, Global, Table


def _call(callee: str, *args: object) -> Call:
    """A call in a script's main function."""
    return Call("main", 0, callee, list(args))  # type: ignore[arg-type]


def _list(*values: object) -> Table:
    """A Lua list of values."""
    return Table(items={i + 1: v for i, v in enumerate(values)})  # type: ignore[misc]


def _char(kind: int, power: int = 2, model: str = "civ_a", attack: str = "Att_Normal") -> Call:
    """A `CfgChar` call with the arguments the readers look at."""
    args: list[object] = [float(kind), 4.0, 0.0, 1.0, float(power), 600.0, Global("DamageNormal"), Global(attack)]
    args += [1.0, model, "none", 0.0, 0.0, Global("RangeNormal")]
    return _call("CfgChar", *args)


def test_inventory_lists_every_slot_with_its_object_and_uses() -> None:
    """All 23 slots appear; a configured slot has its object, a level's override and the calls are counted."""
    config = ChunkFacts(calls=[_call("CfgInventoryItem", "dyn_key", 6.0, 0.0, "vags/a", 30000.0)])
    level = ChunkFacts(
        calls=[
            _call("CfgInventoryItem", "dyn_other", 6.0, 0.0, "vags/b"),
            _call("InvGiveItem", 6.0, 1.0),
            _call("InvNumberOf", 6.0),
            _call("HuPutItemInPocket", Global("Guy"), 6.0, 1.0),
        ]
    )
    entries = refs_play.inventory_items(
        {"config_preload2.lua": config, "level9.lua": level}, lambda name: f"objects/{name}.png"
    )
    assert [e["id"] for e in entries] == list(range(23))
    key = entries[6]
    assert key["object"] == "dyn_key" and key["ms"] == 30000 and key["overrides"] == ["level9: dyn_other"]
    assert (key["given"], key["tested"], key["pocketed"]) == (1, 1, 1) and key["image"] == "objects/dyn_key.png"
    assert key["bindings"] == ["InvGiveSkeletonKey", "InvNumberSkeletonKeys"]
    assert entries[0]["object"] is None and entries[0]["image"] is None


def test_unlockables_resolve_modes_and_find_unlockers_and_testers() -> None:
    """A record's mode constant is resolved, and scripts are matched by (level, group, item) and (type, data)."""
    table = _list(_list(5.0, 0.0, 0.0, 1.0, 37.0, Global("RM_Brawl")), _list(95.0, 1.0, 3.0, 6.0, 5.0, 3.0))
    shared = ChunkFacts(assignments=[Assignment("main", 0, "RM_Brawl", 12.0)], tables={"Unlockables": table}, calls=[])
    hub = ChunkFacts(calls=[_call("UM_Unlock", 95.0, 1.0, 3.0), _call("UM_IsDataUnlocked", 6.0, 3.0)])
    entries = refs_play.unlockables({"global.lua": shared, "level95.lua": hub})
    mode, upgrade = entries
    assert mode["kind"] == "Rumble mode" and mode["data"] == 12 and mode["mode"] == "RM_Brawl"
    assert upgrade["index"] == 1 and upgrade["unlocked_by"] == ["level95.lua"]
    assert upgrade["tested_by"] == ["level95.lua"]


def test_power_classes_follow_difficulty_overrides_and_skip_the_boot_copy() -> None:
    """A difficulty's `CfgChar` moves a type to another class; the boot set shows only for classes nobody else sets."""
    power = [float(v) for v in range(1, 31)]
    boot = ChunkFacts(
        calls=[_call("CfgPowerClass", 2.0, *power), _call("CfgPowerClass", 9.0, *power), _char(100, power=2)]
    )
    hard = ChunkFacts(calls=[_call("CfgPowerClass", 2.0, *power), _char(100, power=3)])
    normal = ChunkFacts(calls=[_call("CfgPowerClass", 2.0, *power)])
    scripts = {"config_preload2.lua": boot, "config_hard.lua": hard, "config_normal.lua": normal}
    entries = {e["id"]: e for e in refs_play.power_classes(scripts)}
    assert list(entries) == ["normal:2", "hard:2", "boot:9"]
    assert entries["normal:2"]["types"] == [100] and entries["hard:2"]["types"] is None
    assert entries["normal:2"]["power_max"] == 1 and entries["normal:2"]["v42"] == 30


def test_warrior_classes_name_the_warriors_and_their_power_class() -> None:
    """Class 6 is Rembrandt's and Ash's, with power class 64."""
    stats = [float(v) for v in range(1, 14)]
    scripts = {"config_easy.lua": ChunkFacts(calls=[_call("CfgWarriorClass", 6.0, *stats)])}
    (entry,) = refs_play.warrior_classes(scripts)
    assert entry["warrior"] == "Rembrandt, Ash" and entry["power_class"] == 64 and 30 in entry["types"]
    assert entry["v05"] == 1 and entry["rage_max"] == 3 and entry["v0d"] == 13


def test_attacks_list_kinds_then_tables() -> None:
    """Every kind has its delay and command; 45-entry tables are listed per script, other tables are not."""
    values = _list(*[float(v) for v in range(45)])
    boot = ChunkFacts(
        calls=[_call("CfgAttackDelay", 0.0, 200.0), _char(100)],
        tables={"Att_Normal": values, "Att_Short": _list(1.0), "Other": values},
    )
    rumble = ChunkFacts(calls=[_call("BrSetAttackWeight", Global("h"), 41.0, 0.0)])
    entries = refs_play.attacks({"config_preload2.lua": boot, "survival.lua": rumble})
    kinds = [e for e in entries if e["kind"] == "attack kind"]
    assert len(kinds) == 45 and kinds[0]["delay"] == 200 and kinds[0]["command"] == "0x10"
    assert kinds[41]["rumble_weights"] == ["survival 0"] and kinds[24]["command"] == "0x0f or 0x10"
    (table,) = [e for e in entries if e["kind"] != "attack kind"]
    assert table["id"] == "boot:Att_Normal" and table["types"] == "1 types" and table["values"][44] == 44


def test_hat_fittings_key_by_type_and_hat() -> None:
    """A fitting is keyed by the type it is looked up by; a repeat gets `#2`."""
    fit = (Table(items={1: 0.1, 2: 0.0, 3: 0.2}), Table(items={1: 0.5, 2: 0.5, 3: 0.5, 4: 0.5}))
    config = ChunkFacts(calls=[_char(11, model="warr_aj"), *[_call("CfgHat", 10.0, 11.0, "dyn_cap", *fit)] * 2])
    first, second = refs_play.hat_fittings({"config_preload2.lua": config})
    assert first["id"] == "11:dyn_cap" and second["id"] == "11:dyn_cap#2"
    assert first["wearer"] == "11 warr_aj" and first["pos"] == [0.1, 0, 0.2] and first["set"] == 10


def test_rumble_links_unlocks_and_never_reads_text() -> None:
    """Modes, arenas, gangs and characters get the unlock record of their type and data; names stay out."""
    unlocks = _list(_list(0.0, 0.0, 0.0, 1.0, 1.0, Global("RM_Koth")), _list(0.0, 0.0, 0.0, 4.0, 2.0, 7.0))
    shared = ChunkFacts(assignments=[Assignment("main", 0, "RM_Koth", 2.0)], tables={"Unlockables": unlocks})
    data = ChunkFacts(
        calls=[
            _call("CfgRumbleGame", "title", Global("RM_Koth"), 1.0, 1.0, 0.0, 3.0, 3.0, 0.0, 0.0, "text"),
            _call("CfgRumbleArena", 101.0, 9.0, _list(Global("RM_Koth"), -1.0)),
            _call("CfgRumbleGang", 5.0, "name", _list(*[7.0] * 9)),
            _call("CfgRumbleChar", 7.0, "NAME", "GANG", 5.0, Global("RM_LT"), 0.0, 70.0, "BIO"),
        ]
    )
    entries = {e["id"]: e for e in refs_play.rumble({"global.lua": shared, "rumble_data.lua": data})}
    assert list(entries) == ["mode-RM_Koth", "arena-101", "gang-5", "char-7"]
    assert entries["mode-RM_Koth"]["number"] == 2 and entries["mode-RM_Koth"]["unlock"] == 0
    assert entries["mode-RM_Koth"]["versus"] is False and entries["arena-101"]["modes"] == ["RM_Koth"]
    assert entries["char-7"]["unlock"] == 1 and entries["char-7"]["rank"] == "RM_LT"
    assert all("NAME" not in str(e.values()) and "BIO" not in str(e.values()) for e in entries.values())


def test_statistics_list_categories_events_and_levels() -> None:
    """The maxima go to their categories, every event slot is listed, and the points matrix resolves `Pts`."""
    config = ChunkFacts(
        calls=[_call("CfgSetStatTypeMax", 5.0, 3.0, 4.0, 2.0, 10.0), _call("CfgSetStatValue", 3.0, 1.0, 4.0)]
    )
    shared = ChunkFacts(
        tables={
            "Pts": Table(fields={"COMBAT_HI": 15000.0}),
            "PointsMatrix": Table(items={2: Table(fields={"mission": 11.0, "combat": Global("Pts.COMBAT_HI")})}),
        },
        calls=[_call("StatAdd", Global("player"), 5.0, 4.0, 1.0)],
    )
    entries = {e["id"]: e for e in refs_play.statistics({"config_preload2.lua": config, "global.lua": shared})}
    assert entries["combat"]["max"] == 5 and entries["mission"]["max"] == 10 and entries["bonus"]["max"] is None
    assert len([e for e in entries.values() if e["kind"] == "event"]) == 45
    assert entries["3-1"]["points"] == 4 and entries["5-4"]["added_by"] == ["global.lua"]
    assert entries["level-2"]["mission"] == 11 and entries["level-2"]["combat"] == 15000
