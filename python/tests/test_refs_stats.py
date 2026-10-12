# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the characters page's stats columns: each type joined with its power and Warrior classes.

Everything is synthetic: made-up types, power classes and Warrior classes with the real lists' shapes.
"""

from pathlib import Path

from coney_tools import refs, refs_cli, refs_render, refs_stats
from coney_tools.refs import RefList
from coney_tools.refs_play import POWER_CLASSES, WARRIOR_CLASSES
from coney_tools.refs_topics import CHARACTERS

REPO = Path(__file__).resolve().parents[2]


def _list(topic: refs.Topic, entries: list[dict[str, object]]) -> RefList:
    """A list of `topic` holding `entries`, every one inferred from a made-up source."""
    return RefList(topic, "T", "", "", {"source": "test", "evidence": "inferred"}, entries)


def _characters() -> RefList:
    """A Warrior with a class (32), a Warriors type with none (41), a civilian (417) and a type with no class."""
    common = {"damage_table": "DamageFox", "attack_table": "Att_Warrior", "range_table": "RangeNormal"}
    return _list(
        CHARACTERS,
        [
            {"id": 32, "category": 14, "v11d": 7, "health": 1800, "damage_scale": 1, "hat": "dyn_eh_ma2", **common},
            {"id": 41, "category": 14, "v11d": 7, "health": 1500, "damage_scale": 1, **common},
            {
                "id": 417,
                "category": 4,
                "v11d": 2,
                "health": 600,
                "damage_scale": 0.75,
                "drop_group": "grp_male_ped",
                "drop_chance": -1,
                "weapon": "dyn_baton",
                **common,
            },
            {"id": 500, "category": 4, "health": 100},
        ],
    )


def _power_classes() -> RefList:
    """Classes 2, 7 and 64 on normal, and class 7 again on easy with other numbers (which must not be used)."""
    return _list(
        POWER_CLASSES,
        [
            {"id": "easy:7", "difficulty": "easy", "class": 7, "power_max": 1, "stamina_max": 1},
            {"id": "normal:2", "difficulty": "normal", "class": 2, "power_max": 200, "stamina_max": 95},
            {"id": "normal:7", "difficulty": "normal", "class": 7, "power_max": 400, "stamina_max": 120},
            {"id": "normal:64", "difficulty": "normal", "class": 64, "power_max": 400, "stamina_max": 135},
        ],
    )


def _warrior_classes() -> RefList:
    """Rembrandt's class 6 on easy and normal, and the boot class 9."""
    return _list(
        WARRIOR_CLASSES,
        [
            {"id": "easy:6", "difficulty": "easy", "class": 6, "health_scale": 70, "damage_scale": 150, "rage_max": 1},
            {
                "id": "normal:6",
                "difficulty": "normal",
                "class": 6,
                "health_scale": 50,
                "damage_scale": 115,
                "rage_max": 78,
                "power_class": 64,
                "types": [30, 31, 32, 38, 39, 40],
            },
            {
                "id": "boot:9",
                "difficulty": "boot",
                "class": 9,
                "health_scale": 85,
                "damage_scale": 135,
                "rage_max": 120,
            },
        ],
    )


def _cells() -> dict[str, dict[object, str]]:
    """The extra columns' cells by heading, for the synthetic lists."""
    columns = refs_stats.character_columns(_characters(), _power_classes(), _warrior_classes())
    return {column.heading: column.cells for column in columns}


def test_a_warrior_shows_his_ai_and_player_classes_and_his_warrior_class() -> None:
    stats = _cells()[refs_stats.STATS_HEADING][32]
    # As an AI human he fights with the type's own class 7; as a player with his Warrior's 64.
    assert "AI: class [7](power-classes.md#power-normal-7), power 400, stamina 120" in stats
    assert "Player: class [64](power-classes.md#power-normal-64), power 400, stamina 135" in stats
    # His Warrior class on normal: Strength is the damage %, the health % gives 900 of the type's 1800.
    assert "Warrior class [6](warrior-classes.md#warrior-normal-6)" in stats
    assert "Strength 115 %" in stats and "Health 50 % (900)" in stats and "Rage max 78" in stats
    assert "easy" not in stats and "150" not in stats


def test_a_warriors_type_with_no_warrior_class_plays_with_class_7() -> None:
    stats = _cells()[refs_stats.STATS_HEADING][41]
    assert "AI: class [7]" in stats
    assert "Player:" not in stats  # the same class as a player, so one line says it
    assert "Warrior class" not in stats


def test_a_civilian_has_one_class_line_and_its_combat_and_drop_cells() -> None:
    cells = _cells()
    assert (
        cells[refs_stats.STATS_HEADING][417] == "AI: class [2](power-classes.md#power-normal-2), power 200, stamina 95"
    )
    assert cells[refs_stats.COMBAT_HEADING][417] == "`DamageFox` x 0.75, `Att_Warrior`, `RangeNormal`"
    assert cells[refs_stats.DROP_HEADING][417] == "`grp_male_ped`, -1 (the class money range)"


def test_a_type_with_no_class_or_tables_has_empty_cells() -> None:
    cells = _cells()
    assert cells[refs_stats.STATS_HEADING].get(500, "") == ""
    assert cells[refs_stats.COMBAT_HEADING].get(500, "") == ""
    assert cells[refs_stats.DROP_HEADING].get(500, "") == ""


def test_a_drop_chance_without_a_group_still_shows() -> None:
    characters = _list(CHARACTERS, [{"id": 3, "drop_chance": 5}, {"id": 4, "drop_chance": 0}])
    columns = refs_stats.character_columns(characters, _power_classes(), _warrior_classes())
    drop = next(column for column in columns if column.heading == refs_stats.DROP_HEADING)
    assert drop.cells[3] == "chance 5"
    assert 4 not in drop.cells  # 0 and no group: nothing to drop


def test_the_page_places_the_extra_columns_after_their_fields() -> None:
    columns = refs_stats.character_columns(_characters(), _power_classes(), _warrior_classes())
    page = refs_render.page(_characters(), columns)
    header = next(line for line in page.splitlines() if line.startswith("| Type |"))
    names = [name.strip() for name in header.strip("|").split("|")]
    assert names.index("Category") < names.index("Health") < names.index(refs_stats.STATS_HEADING)
    assert names.index(refs_stats.STATS_HEADING) < names.index(refs_stats.COMBAT_HEADING) < names.index("Hat")
    assert names.index("Weapon") < names.index(refs_stats.DROP_HEADING)
    row = next(line for line in page.splitlines() if 'id="char-32"' in line)
    assert len(row.split("|")) == len(header.split("|"))  # every row has a cell under every heading


def test_the_real_characters_page_has_the_stats_and_stays_small() -> None:
    lists = {reflist.topic.key: reflist for reflist in refs_cli.load_all(REPO)}
    pages = refs_cli.pages(REPO, list(lists.values()))
    page = pages[REPO / refs_cli.DOCS_DIR / "characters.md"]
    rembrandt = next(line for line in page.splitlines() if 'id="char-32"' in line)
    assert "Warrior class [6](warrior-classes.md#warrior-normal-6)" in rembrandt
    assert "Strength 115 %" in rembrandt and "Health 50 % (900)" in rembrandt
    # The repository refuses files of 512 KB or more (a guard against game data): the page must stay well under.
    assert len(page.encode("utf-8")) < 448 * 1024
