# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the move of the retired progress list into the function database (issue #778).

Everything is made up: a small Ghidra listing, pages with tables and prose, binding files and tags. No game data.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from test_progress import ONE_TAG, make_checkout

from coney_tools import progress, progress_import, research_functions
from coney_tools.cli import main
from coney_tools.orig_index import IndexRow
from coney_tools.progress_research import ListedFunction

LISTED = {
    f.address: f
    for f in (
        ListedFunction(0x1000, 256, "Alpha_Init", True),
        ListedFunction(0x1100, 64, "Alpha_Step", False),
        ListedFunction(0x1180, 100, "", False),  # Ghidra's default name
        ListedFunction(0x1200, 128, "Beta_Run", False),
        ListedFunction(0x1210, 16, "Wrapper_Fn", False),
        ListedFunction(0x1220, 16, "Bare_Fn", False),
    )
}
PORTS = {a: f"src/core/x.h:f{a:x}" for a in (0x1000, 0x1100, 0x1180, 0x1200, 0x1210, 0x1220, 0x1300)}
PAGES = {
    "combat.md": "# Combat\n",
    "combat-grabs.md": (
        "# Grabs\n\n## Map {#map}\n\n| Address | Name | Role | Evidence |\n| --- | --- | --- | --- |\n"
        "| `0x00001000` | `Old_Name` | starts it | confirmed (code) |\n"
        "| `0x00001100` | `Alpha_Step` | steps it | inferred |\n"
    ),
    "ai.md": (
        "# AI\n\n## Behaviour {#behaviour}\n\n"
        "The item at `0x00001200` is read from the code: confirmed (code).\n\n"
        "The helper at `0x00001180` is only mentioned here.\n"
    ),
}
BINDINGS = {
    "x.yaml": "- name: Wrap\n  wrapper: 0x00001210\n  evidence: inferred\n\n- name: Bad\n  wrapper: 0x00001220\n"
}
ROWS = [
    progress_import.LegacyRow(a, f"Legacy_{a:x}", "Alpha", 16)
    for a in (0x1000, 0x1100, 0x1180, 0x1200, 0x1210, 0x1220, 0x1300)
]


def plan(
    rows: list[progress_import.LegacyRow] | None = None,
    pages: dict[str, str] | None = None,
    existing: list[research_functions.SystemFunctions] | None = None,
    ports: dict[int, str] | None = None,
) -> progress_import.Plan:
    """The plan for the made-up rows, pages, bindings and listing."""
    return progress_import.plan(
        ROWS if rows is None else rows,
        listing=LISTED,
        ports=PORTS if ports is None else ports,
        pages=PAGES if pages is None else pages,
        bindings=BINDINGS,
        existing=existing or [],
    )


def entries(result: progress_import.Plan) -> dict[int, research_functions.Entry]:
    """The plan's entries by address."""
    return {e.address: e for s in result.systems for e in s.entries}


def test_a_table_row_gives_the_page_section_level_and_role() -> None:
    """The best source is the function table of a page: its section, level and role, with Ghidra's name."""
    entry = entries(plan())[0x1000]
    assert (entry.page, entry.level, entry.role) == ("combat-grabs.md#map", "confirmed-code", "starts it")
    assert (entry.name, entry.coney, entry.status) == ("Alpha_Init", "src/core/x.h:f1000", "legacy")


def test_a_paragraph_that_states_a_level_is_the_next_source() -> None:
    entry = entries(plan())[0x1200]
    assert (entry.page, entry.level, entry.role) == ("ai.md#behaviour", "confirmed-code", "")


def test_a_page_that_only_mentions_the_function_gives_the_weakest_level() -> None:
    """With no level stated anywhere, the entry is `speculative` with a note saying why, and the report lists it."""
    result = plan()
    entry = entries(result)[0x1180]
    assert (entry.page, entry.level, entry.note) == ("ai.md#behaviour", "speculative", progress_import.UNGRADED_NOTE)
    assert len(result.report.ungraded) == 1 and "0x00001180" in result.report.ungraded[0]
    assert entry.name == "Legacy_1180"  # Ghidra's name is still the default, so the row's is kept


def test_a_binding_wrapper_points_at_the_bindings_page_with_the_bindings_evidence() -> None:
    result = plan()
    entry = entries(result)[0x1210]
    assert (entry.page, entry.level) == (progress_import.BINDINGS_PAGE, "inferred")
    assert result.report.sources == {"table": 2, "prose": 1, "cited": 1, "binding": 1}
    assert 0x1220 not in entries(result)  # a binding with no evidence gives no level, and no page cites it


def test_a_row_nothing_cites_or_nothing_tags_gets_no_entry() -> None:
    """Rows are reported with the reason: a function no page cites has no page or level; an untagged one has no port."""
    result = plan()
    skipped = " ".join(result.report.skipped)
    assert "0x00001220 Legacy_1220: no research page cites it" in skipped
    assert "0x00001300 Legacy_1300: no research page cites it" in skipped
    untagged = plan(ports={a: p for a, p in PORTS.items() if a != 0x1000})
    assert any("0x00001000" in s and "no @orig tag" in s for s in untagged.report.skipped)
    assert 0x1000 not in entries(untagged) and untagged.report.untagged and "0x00001000" in untagged.report.untagged[0]


def test_names_and_listing_are_reported() -> None:
    """The entry has Ghidra's name; a row that differs, and a row Ghidra has no function for, are listed."""
    result = plan()
    assert any("0x00001000 Legacy_1000: Ghidra calls it Alpha_Init" in r for r in result.report.renamed)
    assert result.report.unlisted == ["0x00001300 Legacy_1300"]


def test_the_page_with_most_functions_wins_and_names_the_system() -> None:
    """Among equal sources the home page (most functions on it) wins; a topic page joins the system of its prefix."""
    pages = {
        **PAGES,
        "combat-moves.md": "# Moves\n\n## Map {#map}\n\n| Address | Name | Evidence |\n| --- | --- | --- |\n"
        "| `0x00001000` | `Alpha_Init` | confirmed (code) |\n",
    }
    result = plan(pages=pages)
    assert entries(result)[0x1000].page == "combat-grabs.md#map"  # grabs holds two of the functions, moves one
    assert {s.system for s in result.systems} == {"ai", "combat", "scripting"}
    assert result.report.systems["combat"] == 2


def test_an_entry_the_database_has_gains_coney_and_keeps_the_rest() -> None:
    """An existing entry is not rewritten from a page: it gets `coney` and `status`; a second run changes nothing."""
    mine = research_functions.Entry(0x1000, "Alpha_Init", "ai.md#mine", "inferred", role="my own words", kind="init")
    existing = [research_functions.SystemFunctions("hand", Path("research/functions/hand.toml"), "", (mine,))]
    result = plan(existing=existing)
    kept = entries(result)[0x1000]
    assert (kept.page, kept.level, kept.role, kept.kind) == ("ai.md#mine", "inferred", "my own words", "init")
    assert (kept.coney, kept.status) == ("src/core/x.h:f1000", "legacy")
    assert result.report.updated == 1 and "hand" in result.changed
    again = plan(existing=result.systems)
    assert again.report.updated == 0 and again.report.added == 0 and again.changed == set()
    assert again.report.unchanged == len(ROWS) - len(again.report.skipped)


def test_a_candidate_the_schema_refuses_gives_way_to_the_next() -> None:
    """A table cell that carries decompiler output cannot be an entry's note; the next source is used."""
    pages = {
        **PAGES,
        "combat-grabs.md": PAGES["combat-grabs.md"].replace("confirmed (code) |", "confirmed (code) FUN_00123456 |"),
        "ai.md": PAGES["ai.md"] + "\nThe start at `0x00001000` is inferred.\n",
    }
    entry = entries(plan(pages=pages))[0x1000]
    assert (entry.page, entry.level) == ("ai.md#behaviour", "inferred")


def test_systems_follow_the_page_names() -> None:
    pages = {"combat.md", "combat-grabs.md", "hud.md", "file-io.md", "formats/animation.md", "animation.md"}
    assert progress_import.system_of("combat-grabs.md#grab", pages) == "combat"
    assert progress_import.system_of("combat.md#x", pages) == "combat"
    assert progress_import.system_of("hud-functions-lua.md", pages) == "hud"
    assert progress_import.system_of("file-io.md", pages) == "file-io"  # `file` is no page
    assert progress_import.system_of("formats/animation.md#x", pages) == "animation"


def test_the_retired_list_is_read_strictly(tmp_path: Path) -> None:
    path = tmp_path / "functions.toml"
    path.write_text('[[function]]\naddress = 0x10\nname = "A"\nsubsystem = "Alpha"\nsize = 8\n', encoding="utf-8")
    assert progress_import.read_legacy(path) == [progress_import.LegacyRow(0x10, "A", "Alpha", 8)]
    for text, message in (
        ('[[function]]\nname = "A"\n', "address must be"),
        ("[[function]]\naddress = 1\n", "name must be"),
        ('[[function]]\naddress = 1\nname = "A"\nsize = -4\n', "size must be"),
        ("[[function", "functions.toml"),
    ):
        path.write_text(text, encoding="utf-8")
        with pytest.raises(progress.ProgressError, match=message):
            progress_import.read_legacy(path)
    with pytest.raises(progress.ProgressError, match="missing"):
        progress_import.read_legacy(tmp_path / "nothing.toml")


def test_a_day_before_the_move_is_measured_with_the_listings_sizes(tmp_path: Path) -> None:
    """The history reads the old list for such a day, with the listing's size where it has one and the row's else."""
    root = make_checkout(tmp_path, tags=ONE_TAG)
    (root / progress.LEGACY_FUNCTIONS_FILE).write_text(
        '[[function]]\naddress = 0x1010\nname = "A"\nsubsystem = "Alpha"\nsize = 100\n'
        '[[function]]\naddress = 0x1020\nname = "B"\nsubsystem = "Alpha"\nsize = 40\n',
        encoding="utf-8",
    )
    listing = [ListedFunction(0x1010, 128, "A", False)]
    functions = progress_import.legacy_functions(root, listing)
    assert functions is not None and [(f.name, f.size) for f in functions] == [("A", 128), ("B", 40)]
    state = progress.load(root, functions)
    assert state.problems == [] and state.reimplemented_bytes == 168
    (root / progress.LEGACY_FUNCTIONS_FILE).unlink()
    assert progress_import.legacy_functions(root, listing) is None


def test_tags_choose_the_port_by_placement_then_path() -> None:
    """A function tagged in several places names the tag above a function first; a constant leaves just the file."""

    def row(address: int, path: str, coney: str, placement: str) -> IndexRow:
        return IndexRow(address, "N", "", f"{path}:1", coney, placement, ())

    rows = [
        row(0x10, "src/a/z.h", "Z::go", "comment"),
        row(0x10, "src/a/a.cpp", "A::go", "body"),
        row(0x10, "src/a/b.h", "B::go", "comment"),
        row(0x20, "src/a/c.h", "inline constexpr int kC", "other"),
    ]
    assert progress_import.ports_of(rows) == {0x10: "src/a/b.h:B::go", 0x20: "src/a/c.h"}


# --- the command ------------------------------------------------------------------------------------------------------

LISTING_TEXT = "0x00001010\t128\tThing::Do\t0\n"
PAGE_TEXT = (
    "# Thing\n\n## Map {#map}\n\n| Address | Name | Role | Evidence |\n| --- | --- | --- | --- |\n"
    "| `0x00001010` | `Thing::Do` | does the thing | confirmed (code) |\n"
)
OLD_LIST = '[[function]]\naddress = 0x00001010\nname = "Thing::Do"\nsubsystem = "Alpha"\nsize = 0x80\n'


def old_checkout(tmp_path: Path) -> Path:
    """A checkout from before the move: the old list, a tagged function, a page that cites it, and no database."""
    root = make_checkout(tmp_path, tags=ONE_TAG, listing=LISTING_TEXT)
    (root / "docs" / "research").mkdir(parents=True)
    (root / "docs" / "research" / "thing.md").write_text(PAGE_TEXT, encoding="utf-8")
    (root / progress.LEGACY_FUNCTIONS_FILE).write_text(OLD_LIST, encoding="utf-8")
    return root


def test_the_command_moves_the_list_and_the_tracker_counts_the_same_function(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A dry run writes nothing; the real run writes the system file, after which `progress check` passes."""
    root = old_checkout(tmp_path)
    monkeypatch.chdir(root)
    assert main(["progress", "import-functions", "--dry-run"]) == 0
    assert "dry run, nothing written" in capsys.readouterr().out
    assert not (root / "research" / "functions").exists()
    assert main(["progress", "check"]) == 1  # the tag has no entry yet
    capsys.readouterr()
    assert main(["progress", "import-functions"]) == 0
    assert "1 new entries" in capsys.readouterr().out
    written = (root / "research" / "functions" / "thing.toml").read_text(encoding="utf-8")
    assert 'coney = "src/core/thing.h:doThing"' in written and 'status = "legacy"' in written
    assert 'page = "thing.md#map"' in written and 'role = "does the thing"' in written
    state = progress.load(root)
    assert state.problems == [] and state.reimplemented_bytes == 0x80 and len(state.functions) == 1
    assert main(["progress", "import-functions"]) == 0  # again: nothing to add
    assert "1 already recorded" in capsys.readouterr().out


def test_the_command_exits_1_for_a_row_it_could_not_place(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = old_checkout(tmp_path)
    (root / "docs" / "research" / "thing.md").write_text("# Nothing\n", encoding="utf-8")
    monkeypatch.chdir(root)
    assert main(["progress", "import-functions"]) == 1
    assert "no research page cites it" in capsys.readouterr().out


def test_the_command_needs_the_listing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = old_checkout(tmp_path)
    (root / "docs" / "progress" / "ghidra-functions.tsv").unlink()
    monkeypatch.chdir(root)
    assert main(["progress", "import-functions"]) == 2
    assert "ghidra-functions.tsv" in capsys.readouterr().err
