# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for importing a research page's code-index tables into the function database (coney_tools.research_import)."""

from __future__ import annotations

from pathlib import Path

import pytest

from coney_tools import research_functions as rf
from coney_tools import research_import

PAGE = """\
# Tasks

Intro with a table that is not a code index:

| Register | Value |
| --- | --- |
| `0x00100000` | 3 |

## Code index {#code-index}

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x003a2728` | `TaskManager_Construct` | builds the singleton `0x00512c7c`: the two wheels | confirmed (code) |
| `0x003a2a30` | `TaskManager_Destruct` | | confirmed (code) |
| `0x003a3148` | `TaskManager_Tick(mgr, phase)` | see [physics](physics.md#step), [b](#cars-parts) | Confirmed (code) |
| `0x003a3200` | `PhysicsBody_Sweep` (tagged) | sweeps | inferred |

### Cars: Parts {#cars-parts}

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x00389918` | `CarTask_PreRender` | helper | Prepares the car \\| and its parts. | confirmed (code) |
| `0x0038b1f8` | `Car_StartHitRock` | pusher | Starts a rock. | confirmed (code); the rock inferred |
| `0x0038dfa0` | `Car_Explode` | stub | Explodes a car. | not needed: debug only |

## Sizes

| Address | Name | Size |
| --- | --- | --- |
| `0x00347170` | `CollisionMesh_BeginShapeQuery` | 396 |
"""


def run(markdown: str = PAGE, page: str = "tasks.md", section: str = "") -> research_import.ImportResult:
    """Import `markdown` as the page `page`."""
    return research_import.import_tables(markdown, page, source=f"docs/research/{page}", section=section)


def by_name(result: research_import.ImportResult) -> dict[str, rf.Entry]:
    """The imported entries by name."""
    return {e.name: e for e in result.entries}


def test_rows_become_entries_with_the_section_above_them() -> None:
    """Address, name, role and level are read; the page is the page plus the nearest heading's anchor."""
    entry = by_name(run())["TaskManager_Construct"]
    assert entry.address == 0x3A2728
    assert entry.page == "tasks.md#code-index"
    assert entry.role == "builds the singleton `0x00512c7c`: the two wheels"
    assert entry.level == "confirmed-code"
    assert (entry.kind, entry.note) == ("", "")


def test_an_empty_role_is_kept_empty() -> None:
    """A table row with no role imports as an entry with no role."""
    assert by_name(run())["TaskManager_Destruct"].role == ""


def test_a_signature_and_a_tag_are_dropped_and_reported() -> None:
    """The name is Ghidra's name; what the page wrote around it is reported, not kept."""
    result = run()
    entries = by_name(result)
    assert "TaskManager_Tick" in entries
    assert "PhysicsBody_Sweep" in entries
    dropped = [n for n in result.notes if "dropped" in n]
    assert any("(mgr, phase)" in n for n in dropped)
    assert any("(tagged)" in n for n in dropped)


def test_level_spelling_is_forgiven_and_a_mixed_cell_keeps_its_text() -> None:
    """`Confirmed (code)` is confirmed-code; a cell with more than the level keeps the whole cell as the note."""
    entries = by_name(run())
    assert entries["TaskManager_Tick"].level == "confirmed-code"
    assert entries["TaskManager_Tick"].note == ""
    mixed = entries["Car_StartHitRock"]
    assert (mixed.level, mixed.note) == ("confirmed-code", "confirmed (code); the rock inferred")
    assert any("mixed" in n and "Car_StartHitRock" in n for n in run().notes)


def test_a_role_column_beside_a_what_it_does_column_is_the_kind() -> None:
    """When a table has both, the short Role is the kind and What it does is the role; a pipe is unescaped."""
    entry = by_name(run())["CarTask_PreRender"]
    assert entry.kind == "helper"
    assert entry.role == "Prepares the car | and its parts."


def test_not_needed_takes_its_reason_as_the_role_when_there_is_no_other() -> None:
    """`not needed: <reason>` is a level whose reason is the role; with a role already, the reason is the note."""
    entry = by_name(run())["Car_Explode"]
    assert entry.level == "not-needed"
    assert entry.role == "Explodes a car."
    assert entry.note == "not needed: debug only"
    bare = "| Address | Name | Evidence |\n| --- | --- | --- |\n| `0x00100200` | `A` | not needed: a stub |\n"
    assert run(bare).entries[0].role == "a stub"


def test_links_are_rebased_to_the_research_root() -> None:
    """A role link keeps pointing at the same place once the text lives in the database, below docs/research."""
    sub = research_import.import_tables(PAGE, "combat/grabs.md", source="s")
    role = {e.name: e.role for e in sub.entries}["TaskManager_Tick"]
    assert role == "see [physics](combat/physics.md#step), [b](combat/grabs.md#cars-parts)"
    assert {e.name: e.page for e in sub.entries}["TaskManager_Construct"] == "combat/grabs.md#code-index"


def test_links_in_an_evidence_note_are_rebased_too() -> None:
    """The note of a mixed evidence cell is rebased like the role, so the rendered list reads it from its own folder."""
    page = (
        "| Address | Name | Evidence |\n| --- | --- | --- |\n"
        "| `0x00100200` | `A` | confirmed (code); the id [inferred](grabs.md#x) |\n"
    )
    [entry] = research_import.import_tables(page, "combat/other.md", source="s").entries
    assert entry.note == "confirmed (code); the id [inferred](combat/grabs.md#x)"


def test_a_table_that_is_not_a_function_list_is_skipped() -> None:
    """A table without an address and name, or without an evidence column, gives no entries and is reported."""
    result = run()
    assert 0x00347170 not in {e.address for e in result.entries}
    assert 0x00100000 not in {e.address for e in result.entries}
    assert any("no Evidence column" in s for s in result.skipped)


def test_a_bad_row_is_skipped_with_its_line_and_reason() -> None:
    """Each row that cannot be imported is named with the page line, so a person can fix it by hand."""
    page = (
        "| Address | Name | Role | Evidence |\n"
        "| --- | --- | --- | --- |\n"
        "| `0x00100200` | `Good` | ok | inferred |\n"
        "| `0x00100300` or `0x00100304` | `TwoAddresses` | r | inferred |\n"
        "| `0x00100400` | `has space` | r | inferred |\n"
        "| `0x00100500` | `NoLevel` | r | |\n"
        "| `0x00100600` | `Unknown` | r | probably |\n"
        "| not an address | `X` | r | inferred |\n"
        "| `0x00100700` | `NotNeeded` | | not needed |\n"
    )
    result = run(page)
    assert [e.name for e in result.entries] == ["Good"]
    messages = {int(s.split(":")[1]): s for s in result.skipped}
    assert sorted(messages) == [4, 5, 6, 7, 8, 9]
    assert "docs/research/tasks.md:4: " in messages[4]
    for line, word in ((4, "addresses only"), (5, "not a Ghidra name"), (6, "evidence level"), (7, "evidence level")):
        assert word in messages[line]
    assert "address" in messages[8]
    assert "reason" in messages[9]


def test_a_short_address_is_padded_and_reported() -> None:
    """`0x149160` is the address `0x00149160`; the form is fixed in the data and the page is told."""
    page = "| Address | Name | Evidence |\n| --- | --- | --- |\n| `0x149160` | `A` | inferred |\n"
    result = run(page)
    assert result.entries[0].address == 0x149160
    assert any("padded" in n for n in result.notes)


def test_a_row_shared_by_several_functions_is_split() -> None:
    """`0x1 / 0x2` with two code spans in the name cell is two entries with the row's role, reported as split."""
    head = "| Address | Name | Role | Evidence |" + chr(10) + "| --- | --- | --- | --- |" + chr(10)
    rows = [
        "| `0x00100200` / `0x00100210` | `Gang_Set` / `Gang_Get` | byte `+0x6c` | confirmed (code) |",
        "| `0x00100300`, `0x00100310` | `Mem_Alloc`, `the free` | calls the heap | confirmed (code) |",
        "| `0x00100400` / `0x00100410` | `One` | r | inferred |",
    ]
    page = head + chr(10).join(rows) + chr(10)
    result = run(page)
    assert [(e.address, e.name, e.role) for e in result.entries] == [
        (0x100200, "Gang_Set", "byte `+0x6c`"),
        (0x100210, "Gang_Get", "byte `+0x6c`"),
    ]
    assert any("split" in n for n in result.notes)
    assert any("'the free' is not a Ghidra name" in s for s in result.skipped)
    assert any("2 addresses but 1 names" in s for s in result.skipped)


def test_an_address_listed_twice_keeps_the_first_row() -> None:
    """The second row of the same address is reported; if it differs, the page needs a person's decision."""
    page = (
        "| Address | Name | Role | Evidence |\n| --- | --- | --- | --- |\n"
        "| `0x00100200` | `A` | one | inferred |\n"
        "| `0x00100200` | `A` | two | confirmed (code) |\n"
        "| `0x00100200` | `A` | one | inferred |\n"
    )
    result = run(page)
    assert [e.role for e in result.entries] == ["one"]
    assert len([s for s in result.skipped if "again" in s]) == 1
    assert any("differs" in s for s in result.skipped)


def test_a_section_limits_the_import_to_its_tables() -> None:
    """With a section, only tables under that heading (and its subheadings) are imported."""
    names = {e.name for e in run(section="cars-parts").entries}
    assert names == {"CarTask_PreRender", "Car_StartHitRock", "Car_Explode"}
    assert "TaskManager_Construct" in {e.name for e in run(section="code-index").entries}


def test_an_unknown_section_is_an_error() -> None:
    """A typo in --section must not import nothing quietly."""
    with pytest.raises(research_import.ResearchImportError, match="no-such"):
        run(section="no-such")


def test_a_table_inside_a_code_fence_is_ignored() -> None:
    """Fenced examples are not tables of the page."""
    page = "```\n| Address | Name | Evidence |\n| --- | --- | --- |\n| `0x00100200` | `A` | inferred |\n```\n"
    assert run(page).entries == []


def system_with(*entries: rf.Entry) -> rf.SystemFunctions:
    """A `combat` system file with `entries`."""
    return rf.SystemFunctions("combat", Path("research/functions/combat.toml"), "", entries)


def test_merge_adds_new_entries_in_address_order() -> None:
    """New rows join the file; the ones already there are untouched."""
    old = rf.Entry(0x100300, "B", "a.md", "inferred", role="mine")
    new = [rf.Entry(0x100200, "A", "a.md", "inferred"), rf.Entry(0x100300, "B", "a.md", "inferred", role="mine")]
    merged = research_import.merge(system_with(old), new, {}, replace_existing=False)
    assert [e.address for e in merged.system.entries] == [0x100200, 0x100300]
    assert (merged.added, merged.unchanged, merged.replaced) == (1, 1, 0)


def test_merge_keeps_an_existing_entry_that_differs_unless_asked() -> None:
    """A hand-edited entry survives a re-import; `replace` takes the page's row but keeps Coney's fields."""
    old = rf.Entry(
        0x100200,
        "A",
        "a.md",
        "confirmed-code",
        role="better",
        coney="src/a.cpp:A",
        status="compared",
        gaps=(rf.Gap("g", "stand-in", 1),),
    )
    row = rf.Entry(0x100200, "A", "a.md", "inferred", role="worse")
    kept = research_import.merge(system_with(old), [row], {}, replace_existing=False)
    assert kept.system.entries == (old,)
    assert len(kept.kept) == 1
    assert "0x00100200" in kept.kept[0]
    replaced = research_import.merge(system_with(old), [row], {}, replace_existing=True)
    [entry] = replaced.system.entries
    assert (entry.role, entry.level) == ("worse", "inferred")
    assert (entry.coney, entry.status, entry.gaps) == (old.coney, old.status, old.gaps)
    assert replaced.replaced == 1


def test_a_ported_entry_matching_its_row_is_unchanged() -> None:
    """A page row has no `coney`, `status` or `gaps`: an entry that differs only in those is not a hand edit."""
    old = rf.Entry(0x100200, "A", "a.md", "inferred", role="r", coney="src/a.cpp", status="compared")
    row = rf.Entry(0x100200, "A", "a.md", "inferred", role="r")
    merged = research_import.merge(system_with(old), [row], {}, replace_existing=False)
    assert (merged.unchanged, merged.kept) == (1, [])
    assert merged.system.entries == (old,)


def test_merge_leaves_out_a_function_another_system_owns() -> None:
    """One owner per address: a row whose address is in another system file is reported and not added."""
    row = rf.Entry(0x100200, "A", "a.md", "inferred")
    merged = research_import.merge(
        system_with(), [row], {0x100200: "research/functions/ai.toml"}, replace_existing=False
    )
    assert merged.system.entries == ()
    assert "research/functions/ai.toml" in merged.foreign[0]


def test_what_is_written_reads_back() -> None:
    """The imported entries are valid under the schema, so a written file always loads."""
    entries = run().entries
    text = rf.dumps(rf.SystemFunctions("tasks", Path("tasks.toml"), "", tuple(entries)))
    assert rf.parse_system("tasks", text, Path("tasks.toml")).entries == tuple(sorted(entries, key=lambda e: e.address))
