# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the move of the retired progress list into the function database (issue #778).

Everything is made up: a small Ghidra listing, pages with tables and prose, binding files and tags. No game data.
"""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest
import research_support
from test_progress import ONE_TAG, make_checkout

from coney_tools import progress, progress_import, research_functions
from coney_tools.cli import main
from coney_tools.conventions import CONVENTIONS_FILE
from coney_tools.orig_index import IndexRow
from coney_tools.progress_research import ListedFunction

REPO = Path(__file__).resolve().parents[2]
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
    listing: dict[int, ListedFunction] | None = None,
) -> progress_import.Plan:
    """The plan for the made-up rows, pages, bindings and listing."""
    return progress_import.plan(
        ROWS if rows is None else rows,
        listing=LISTED if listing is None else listing,
        ports=PORTS if ports is None else ports,
        pages=PAGES if pages is None else pages,
        bindings=BINDINGS,
        existing=existing or [],
        research=research_support.research(),
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


def test_a_page_that_only_mentions_the_function_gives_no_level() -> None:
    """With no level stated for the function, the entry has none (not a guess) and a note; the report lists it."""
    result = plan()
    entry = entries(result)[0x1180]
    assert (entry.page, entry.level, entry.note) == ("ai.md#behaviour", "", progress_import.UNGRADED_NOTE)
    assert len(result.report.ungraded) == 1 and "0x00001180" in result.report.ungraded[0]
    assert "page states" not in entry.note  # the page may state a level the import could not tie
    assert entry.name == "Legacy_1180"  # Ghidra's name is still the default, so the row's is kept


def test_a_binding_wrapper_points_at_its_reference_page_with_the_bindings_evidence() -> None:
    """The generated page of the binding names the wrapper; the entry belongs to the scripting system."""
    result = plan()
    entry = entries(result)[0x1210]
    assert (entry.page, entry.level) == ("../references/bindings/x.md#wrap", "inferred")
    assert entry.is_binding
    assert [s.system for s in result.systems if entry in s.entries] == ["scripting"]
    assert result.report.sources == {"table": 2, "claim": 1, "cited": 1, "binding": 1}
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


# --- what a paragraph may say about one function ----------------------------------------------------------------------

A, B = 0x00123400, 0x00123500
HOME = {
    "sys.md": "# Sys\n\n## Behaviour {#behaviour}\n\n",
}


def paragraph_plan(page_body: str, addresses: tuple[int, ...] = (A,)) -> progress_import.Plan:
    """The plan for functions at `addresses` described only by the Behaviour section `page_body` of one page."""
    rows = [progress_import.LegacyRow(a, f"F{a:x}", "Alpha", 8) for a in addresses]
    listing = {a: ListedFunction(a, 8, f"F{a:x}", False) for a in addresses}
    ports = {a: f"src/core/x.h:f{a:x}" for a in addresses}
    pages = {"sys.md": HOME["sys.md"] + page_body}
    return plan(rows, pages=pages, ports=ports, listing=listing)


def test_sentences_ignore_a_full_stop_in_code_and_in_numbers() -> None:
    parts = progress_import.sentences('It loads `name .. ".lua"` in 0.3 s. Then it runs. (Confirmed (code).)')
    assert parts == ['It loads `name .. ".lua"` in 0.3 s.', "Then it runs.", "(Confirmed (code).)"]


def test_the_level_must_be_in_the_sentence_that_cites_the_address() -> None:
    """A level at the end of a paragraph that is about a table below it ties nothing to a function named before it."""
    text = "A call can rescale it later (`0x00123400`, not traced). Every value below was seen (confirmed (runtime))."
    assert progress_import.tied_level(text, A) is None
    own = "It rescales the set (`0x00123400`, confirmed (code)). Every value below was seen (confirmed (runtime))."
    assert progress_import.tied_level(own, A) == ("confirmed-code", "claim")


def test_a_level_only_sentence_right_after_belongs_to_the_sentence_before() -> None:
    text = "It offers the event to the handlers (`0x00123400`). Confirmed (code). The rest is a long story."
    assert progress_import.tied_level(text, A) == ("confirmed-code", "claim")


NAMES = {
    A: frozenset({"Start_It"}),
    B: frozenset({"End_It"}),
    0x00123600: frozenset({"Mid_It"}),
    0x00123999: frozenset({"Caller_It"}),
}


def test_a_sentence_must_be_about_the_function_not_about_its_neighbours() -> None:
    """Another address in the sentence that the sentence does not name makes the level about something wider."""
    event = "Confirmed (code): the victim's clip, started by `Start_It` (`0x00123400`, caller `0x00123999`), ran."
    assert progress_import.tied_level(event, A, NAMES) is None
    alone = "`Start_It` starts it from `0x00123400` and runs on the pad (confirmed (code))."
    assert progress_import.tied_level(alone, A, NAMES) == ("confirmed-code", "claim")
    # Without names the only address in the sentence is the function's own, which is enough.
    assert progress_import.tied_level("It starts at `0x00123400` (confirmed (code)).", A) == ("confirmed-code", "claim")
    # Two functions, each named beside its address: the level is about both.
    both = "`Start_It` (`0x00123400`) calls `End_It` (`0x00123500`) (confirmed (code))."
    assert progress_import.tied_level(both, A, NAMES) == ("confirmed-code", "claim")
    assert progress_import.tied_level(both, B, NAMES) == ("confirmed-code", "claim")
    # A name that only contains the function's name does not count.
    assert progress_import.tied_level(both.replace("`End_It`", "`Pre_End_It`"), A, NAMES) is None


def test_an_aside_level_in_a_sentence_ties_nothing_but_a_code_reading_does() -> None:
    """`(the player; inferred)` and a runtime setup qualify a detail; only a code reading is the function's claim."""
    aside = "`Start_It` (`0x00123400`) skips the search when the brain is 0 (the player; inferred)."
    assert progress_import.tied_level(aside, A, NAMES) is None
    setup = "At runtime (confirmed (runtime)), `Start_It` (`0x00123400`) was called from the test."
    assert progress_import.tied_level(setup, A, NAMES) is None
    code = "`Start_It` (`0x00123400`) skips the search when the brain is 0 (confirmed (code))."
    assert progress_import.tied_level(code, A, NAMES) == ("confirmed-code", "claim")
    # A block that opens with its level still carries any of them.
    block = "Inferred: `Start_It` (`0x00123400`) starts it. `End_It` (`0x00123500`) ends it."
    assert progress_import.tied_level(block, B, NAMES) == ("inferred", "block")


def test_a_level_between_two_addresses_belongs_to_the_one_before_it() -> None:
    """`Wide_It (0x1, confirmed (code)): ... (End_It, 0x2)` grades the first function, not the second."""
    text = "The test is `Start_It` (`0x00123400`, confirmed (code)): it squares the range (`End_It`, `0x00123500`)."
    assert progress_import.tied_level(text, A, NAMES) == ("confirmed-code", "claim")
    assert progress_import.tied_level(text, B, NAMES) is None
    # A level that leads or trails every address is about all of them.
    leading = "Confirmed (code) at `Start_It` (`0x00123400`) and `End_It` (`0x00123500`)."
    assert progress_import.tied_level(leading, B, NAMES) == ("confirmed-code", "claim")


def test_a_level_only_sentence_after_is_one_without_code() -> None:
    """`Confirmed (code): `Other` is called first.` states a level about another function, not about this one."""
    text = "It offers the event to the handlers (`0x00123400`). Confirmed (code): `Other` is called first."
    assert progress_import.tied_level(text, A) is None


def test_two_levels_in_one_sentence_tie_nothing() -> None:
    """`confirmed (code); the rock inferred` grades two claims; a page-less default would be a guess."""
    assert progress_import.tied_level("It hits (`0x00123400`, confirmed (code)) and the rock inferred.", A) is None


def test_a_block_that_opens_with_its_level_grades_every_function_it_lists() -> None:
    text = "Confirmed (code) at the addresses given: `0x00123400` starts it. `0x00123500` ends it."
    assert progress_import.tied_level(text, A) == ("confirmed-code", "claim")
    assert progress_import.tied_level(text, B) == ("confirmed-code", "block")
    # A level further on, about something else, is not the block's.
    assert progress_import.tied_level("`0x00123400` starts it. The ids it writes are confirmed (code).", A) is None


def test_a_paragraph_grades_only_the_functions_whose_claim_states_the_level() -> None:
    result = paragraph_plan(
        "`F123400` (`0x00123400`) starts it (confirmed (code)). `F123500` (`0x00123500`) ends it, not traced.\n",
        (A, B),
    )
    found = entries(result)
    assert (found[A].level, found[A].note) == ("confirmed-code", "")
    assert found[B].level == "" and found[B].note == progress_import.UNGRADED_NOTE
    assert result.report.sources["claim"] == 1 and result.report.sources["cited"] == 1


def test_a_lead_line_grades_the_blocks_under_its_heading() -> None:
    body = "### Group {#group}\n\nConfirmed (code) for each function below.\n\n- `F123400` (`0x00123400`) starts it.\n"
    entry = entries(paragraph_plan(body))[A]
    assert (entry.page, entry.level, entry.note) == ("sys.md#group", "confirmed-code", "")
    # A block that states its own level is not graded by the lead line, even when it ties nothing.
    own = body + "- `F123500` (`0x00123500`) and `F123600` (`0x00123600`) end it (inferred), the id confirmed (code).\n"
    assert entries(paragraph_plan(own, (B,)))[B].level == ""


def test_a_table_row_without_an_evidence_column_grades_nothing() -> None:
    """A field table's cell may say `(confirmed (runtime))` about one field, and a lead line is not the row's claim."""
    fields = "| Offset | Meaning |\n| --- | --- |\n| `+0x1e` | skipped via `0x00123400` (confirmed (runtime)) |\n"
    assert entries(paragraph_plan(fields))[A].level == ""
    led = "### Group {#group}\n\nConfirmed (code) for each row below.\n\n" + fields
    assert entries(paragraph_plan(led))[A].level == ""
    graded = "| Offset | Meaning | Evidence |\n| --- | --- | --- |\n| `+0x1e` | skipped via `0x00123400` | inferred |\n"
    assert entries(paragraph_plan(graded))[A].level == "inferred"  # the row's own cell is its claim


def test_a_first_paragraph_that_states_a_level_about_its_own_subject_is_no_lead_line() -> None:
    """A lead line says how far its level reaches; a behaviour paragraph with a runtime remark in it does not."""
    behaviour = (
        "R1 held blocks. Confirmed (runtime). No dodge exists (confirmed (runtime)); the shuffle is the only one."
    )
    body = f"### Block {{#block}}\n\n{behaviour}\n\n- `F123400` (`0x00123400`) reads the reach.\n"
    assert entries(paragraph_plan(body))[A].level == ""
    scoped = body.replace(behaviour, "R1 held blocks. Confirmed (runtime) at the addresses cited.")
    assert entries(paragraph_plan(scoped))[A].level == "confirmed-runtime"


def test_an_open_questions_section_grades_the_claims_it_states_but_gives_no_lead_line_level() -> None:
    """Weak sections (Coney's, open questions, summaries) say what they say; they are a home only as a last resort."""
    page = "# Sys\n\n## Open questions\n\n`F123400` (`0x00123400`) is answered: confirmed (code).\n"
    result = plan(
        [progress_import.LegacyRow(A, "F", "Alpha", 8)],
        pages={"sys.md": page},
        ports={A: "src/core/x.h:f"},
        listing={A: ListedFunction(A, 8, "F", False)},
    )
    assert entries(result)[A].level == "confirmed-code"
    led = (
        "# Sys\n\n## Open questions\n\n### Group {#group}\n\nConfirmed (code) for each function below.\n\n"
        "- `F` (`0x00123400`) starts it.\n"
    )
    result = plan(
        [progress_import.LegacyRow(A, "F", "Alpha", 8)],
        pages={"sys.md": led},
        ports={A: "src/core/x.h:f"},
        listing={A: ListedFunction(A, 8, "F", False)},
    )
    assert entries(result)[A].level == ""


def test_the_section_that_names_the_function_wins_over_one_that_only_cites_it() -> None:
    body = (
        "### Block {#block}\n\nThe walk passes `0x00123400` on its way (confirmed (code)).\n\n"
        "### Sight {#sight}\n\n`F123400` (`0x00123400`) tests the line of sight (confirmed (code)).\n"
    )
    assert entries(paragraph_plan(body))[A].page == "sys.md#sight"


def test_a_candidate_the_schema_refuses_gives_way_to_the_next() -> None:
    """A table cell that carries decompiler output cannot be an entry's role: the row still names the function's
    section and level, without the role."""
    pages = {
        **PAGES,
        "combat-grabs.md": PAGES["combat-grabs.md"].replace("| starts it |", "| starts FUN_00123456 |"),
    }
    entry = entries(plan(pages=pages))[0x1000]
    assert (entry.page, entry.level, entry.role) == ("combat-grabs.md#map", "confirmed-code", "")


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
    assert state.problems == [] and state.reimplemented_functions == 2
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
    (root / CONVENTIONS_FILE).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(REPO / CONVENTIONS_FILE, root / CONVENTIONS_FILE)
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
    assert state.problems == [] and state.reimplemented_functions == 1 and len(state.functions) == 1
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


# --- levels the pages give in other forms ----------------------------------------------------------------------


def test_a_level_before_an_at_list_grades_every_address_in_it() -> None:
    """`Confirmed (code) at A, B and C` is a statement about each address, whatever the sentences before it say."""
    text = (
        "`Start_It` (`0x00123400`) walks the slots and `End_It` (`0x00123500`) stops. "
        "Confirmed (code) at `0x00123400`, `0x00123500` and `0x00123700`."
    )
    for address in (A, B, 0x00123700):
        assert progress_import.tied_level(text, address, NAMES) == ("confirmed-code", "claim")
    assert progress_import.tied_level(text, 0x00123600, NAMES) is None
    inline = "The door side, confirmed (code) at `0x00123400`, `0x00123500`:"
    assert progress_import.tied_level(inline, B) == ("confirmed-code", "claim")
    other = "It stops (`0x00123400`), confirmed (runtime) at `0x00123500`."
    assert progress_import.tied_level(other, B) is None  # only a code reading is a claim of prose, as always
    two = "Confirmed (code) at `0x00123400`. Inferred at `0x00123400`."
    assert progress_import.tied_level(two, A) is None  # two levels for one address tie nothing


def test_a_level_right_after_the_address_is_its_claim_whatever_it_is() -> None:
    """`(F, 0x1, confirmed (code))` and `(0x1); inferred from both readers` belong to the function just named."""
    adjacent = (
        "Each is copied (`Start_It`, `0x00123400`; inferred from both readers), then `End_It` (`0x00123500`) runs."
    )
    assert progress_import.tied_level(adjacent, A, NAMES) == ("inferred", "claim")
    assert progress_import.tied_level(adjacent, B, NAMES) is None
    paren = "A step follows (`0x00123400`); inferred from both readers."
    assert progress_import.tied_level(paren, A) == ("inferred", "claim")


def test_a_clause_and_a_call_chain_keep_the_level_to_what_it_follows() -> None:
    """A semicolon outside parentheses ends a clause; a level after a chain `A -> B -> C` is about the whole chain."""
    clauses = "It is set by `Start_It` (`0x00123400`; confirmed (code)); that it holds the same ids is inferred."
    assert progress_import.tied_level(clauses, A, NAMES) == ("confirmed-code", "claim")
    own = "Confirmed (code) at `0x00123400` and `0x00123500`; confirmed (runtime) for the pad."
    assert progress_import.tied_level(own, B, NAMES) == ("confirmed-code", "claim")
    chain = "`Go(id)` (`0x00123300` → `0x00123400`; none above 32) → `0x00123500` → `0x00123700`, confirmed (code):"
    names = {**NAMES, 0x00123300: frozenset({"Go_It"}), 0x00123700: frozenset({"Third_It"})}
    for address in (A, B, 0x00123700):
        assert progress_import.tied_level(chain, address, names) == ("confirmed-code", "claim")


def test_a_level_after_the_subject_is_its_claim_when_a_code_reading() -> None:
    """The first function of a sentence is its subject: the level that follows it, before another function, is its."""
    subject = (
        "`Start_It` (`0x00123400`) stores the number and copies the rest, confirmed (code): the stats (`0x00123700`)."
    )
    assert progress_import.tied_level(subject, A, NAMES) == ("confirmed-code", "claim")
    assert progress_import.tied_level(subject, 0x00123700, NAMES) is None
    aside = subject.replace("confirmed (code)", "the player, inferred")
    assert progress_import.tied_level(aside, A, NAMES) is None  # a word-separated inferred is an aside


def test_a_wrapper_arrow_and_a_data_address_do_not_make_the_level_about_something_wider() -> None:
    """`(W -> F)` names a binding's wrapper beside the function it calls; `0x110c0880` is a flag, not a function."""
    names = {**NAMES, 0x00123300: frozenset({"Wrap_It"})}
    wrapper = "`SetIt(x)` (`0x00123300` → `Start_It`, `0x00123400`): floats in 0 to 1 (confirmed (code))."
    assert progress_import.tied_level(wrapper, A, names) == ("confirmed-code", "claim")
    data = "`Start_It` (`0x00123400`) tests the flags `0x110c0880` and `0x00120000` (confirmed (code))."
    assert progress_import.tied_level(data, A, NAMES) == ("confirmed-code", "claim")
    function = data.replace("0x00120000", "0x00123500")
    assert progress_import.tied_level(function, A, NAMES) is None  # `End_It` is a function this sentence does not name


def test_a_level_only_sentence_grades_the_subject_of_the_sentence_before_it() -> None:
    """The first function of a sentence is its subject, whatever other addresses the sentence cites."""
    text = "**Pause** (`Start_It`, `0x00123400` / `0x00123900`): sets it and calls (`0x00123700`). Confirmed (code)."
    names = {**NAMES, 0x00123900: frozenset({"Alt_It"}), 0x00123700: frozenset({"Third_It"})}
    assert progress_import.tied_level(text, A, names) == ("confirmed-code", "claim")
    assert progress_import.tied_level(text, 0x00123700, names) is None


def test_a_closing_level_only_sentence_grades_the_functions_of_the_two_sentences_before_it() -> None:
    closing = (
        "`Start_It` (`0x00123400`) sets the transform. `End_It` (`0x00123500`) takes the flag. Confirmed (code). "
        "A human in a scene is lowered."
    )
    assert progress_import.tied_level(closing, A, NAMES) == ("confirmed-code", "claim")
    assert progress_import.tied_level(closing, B, NAMES) == ("confirmed-code", "claim")
    far = "`Start_It` (`0x00123400`) sets it. It runs on. The pad is read. Confirmed (code)."
    assert progress_import.tied_level(far, A, NAMES) is None


# --- where an entry is described -------------------------------------------------------------------------------


def test_a_section_that_describes_the_function_is_its_home_and_a_coney_section_only_the_last() -> None:
    body = (
        "### Spawners {#spawners}\n\n**The type** (`0x00123400`): the spawner keeps an index.\n\n"
        "## Coney's implementation {#coney}\n\n- **The spawners**: the port calls `F123400` (`0x00123400`)"
        " after each step. Confirmed (code).\n"
    )
    entry = entries(paragraph_plan(body))[A]
    # The Coney section states a code reading of its own; it gives the level, not the home.
    assert (entry.page, entry.level) == ("sys.md#spawners", "confirmed-code") and "sys.md#coney" in entry.note
    # With nothing else to cite, a Coney section is still a page to read.
    only = "## Coney's implementation {#coney}\n\n- **The spawners**: the port calls `F123400` (`0x00123400`).\n"
    assert entries(paragraph_plan(only))[A].page == "sys.md#coney"


def test_the_row_of_another_function_neither_describes_nor_grades_the_function() -> None:
    table = (
        "| Address | Name | Evidence |\n| --- | --- | --- |\n"
        "| `0x00123500` | `End_It` | confirmed (code), called from `0x00123400` |\n"
    )
    mention = "### Block {#block}\n\n`F123400` is read by the walk, `0x00123400`, without a claim.\n"
    body = "### Map {#map}\n\n" + table + "\n" + mention
    result = entries(paragraph_plan(body, (A, B)))
    assert (result[B].page, result[B].level) == ("sys.md#map", "confirmed-code")
    assert result[A].level == "" and result[A].page == "sys.md#block"


def test_a_function_the_research_grades_two_ways_keeps_one_row_and_says_where_the_other_is() -> None:
    body = (
        "### Map {#map}\n\n| Address | Name | Evidence |\n| --- | --- | --- |\n"
        "| `0x00123400` | `F123400` | inferred |\n\n"
        "### Index {#index}\n\n| Address | Name | Evidence |\n| --- | --- | --- |\n"
        "| `0x00123400` | `F123400` | confirmed (code) |\n"
    )
    result = paragraph_plan(body)
    entry = entries(result)[A]
    assert (entry.page, entry.level) == ("sys.md#map", "inferred")
    assert "also graded confirmed-code at [sys.md#index](sys.md#index)" in entry.note
    assert len(result.report.disagreeing) == 1


def test_a_row_about_something_else_grades_the_functions_its_evidence_cell_lists() -> None:
    """`confirmed (code) at A, B` in the Evidence cell is the page's grade of A and B, whatever the row is about."""
    table = (
        "| Offset | Meaning | Evidence |\n| --- | --- | --- |\n"
        "| `+0x20` | the level chosen, -1 = none | confirmed (code) at `0x00123400`, `0x00123500` |\n"
    )
    result = entries(paragraph_plan("### Fields {#fields}\n\n" + table, (A, B)))
    assert (result[A].level, result[B].level) == ("confirmed-code", "confirmed-code")
    assert result[A].page == "sys.md#fields"


def test_the_level_of_another_page_is_taken_and_said() -> None:
    """The home is the section that describes the function; a code reading of it on another page is its level."""
    pages = {
        "sys.md": HOME["sys.md"] + "### Sight {#sight}\n\n- **`F123400`** (`0x00123400`): a ray from a to b.\n",
        "other.md": "# Other\n\n## Behaviour {#behaviour}\n\nIt calls `F123400` (`0x00123400`), confirmed (code)\n",
    }
    rows = [progress_import.LegacyRow(A, f"F{A:x}", "Alpha", 8)]
    result = plan(
        rows,
        pages=pages,
        ports={A: "src/core/x.h:f"},
        listing={A: ListedFunction(A, 8, f"F{A:x}", False)},
    )
    entry = entries(result)[A]
    assert (entry.page, entry.level) == ("sys.md#sight", "confirmed-code")
    assert "other.md#behaviour" in entry.note
    assert result.report.sources["claim"] == 0 and len(result.report.borrowed) == 1


# --- the last round: the closing level, the reimpl pages, the home, the notes and the overrides -----------------------


def pages_plan(pages: dict[str, str], addresses: tuple[int, ...] = (A,)) -> progress_import.Plan:
    """The plan for functions at `addresses` described by `pages`."""
    rows = [progress_import.LegacyRow(a, f"F{a:x}", "Alpha", 8) for a in addresses]
    listing = {a: ListedFunction(a, 8, f"F{a:x}", False) for a in addresses}
    ports = {a: f"src/core/x.h:f{a:x}" for a in addresses}
    return plan(rows, pages=pages, ports=ports, listing=listing)


def test_a_block_that_opens_with_the_function_and_closes_confirmed_code_grades_it_whatever_it_holds() -> None:
    """An inferred aside about a detail, a second address and a trailing sentence do not take the closing level away."""
    closings = [
        "A new one starts from 0 (inferred: the record starts zeroed) and `0x00123500` is read. Confirmed (code).",
        "A ray is cast. Confirmed (code) for the rays; the meaning of `hit` inferred.",
        "It clears the flag (`0x00384a50`). confirmed (code).",
        "The slots are taken in turn. Confirmed (code). The 908 fittings: [Hat fittings](hats.md).",
        "It stops at once. Confirmed (code); that the carried item is a boom box is inferred.",
    ]
    for closing in closings:
        body = f"### Spawners {{#spawners}}\n\n**The type** (`0x00123400`): the spawner keeps an index. {closing}\n"
        entry = entries(paragraph_plan(body))[A]
        assert (entry.page, entry.level) == ("sys.md#spawners", "confirmed-code"), closing


def test_the_closing_level_needs_a_block_that_opens_with_the_function_and_one_code_reading() -> None:
    mention = "The walk passes `0x00123400` and `0x00123500` (inferred: to fan out). It goes on. Confirmed (code).\n"
    assert entries(paragraph_plan("### Walk {#walk}\n\n" + mention))[A].level == ""
    two = "**The type** (`0x00123400`): it does x. Inferred: it does y. Confirmed (code).\n"
    assert entries(paragraph_plan("### Walk {#walk}\n\n" + two))[A].level == ""
    inferred = "**The type** (`0x00123400`): it does x (confirmed (runtime)). Inferred.\n"
    assert entries(paragraph_plan("### Walk {#walk}\n\n" + inferred))[A].level == ""


def test_a_reimpl_page_and_a_coney_section_grade_their_claims_but_are_the_last_home() -> None:
    """They may give the level (as another page's does), and are the home only when no other page cites the function."""
    reimpl = (
        "# Reimpl\n\n## Behaviour {#behaviour}\n\n### `F123400` (`0x00123400`), to correct {#fix}\n\n"
        "1. **The pass.** It runs the list. Confirmed (code) at `0x00123400`.\n"
    )
    pages = {
        "sys.md": HOME["sys.md"] + "### Walk {#walk}\n\nThe walk calls `F123400` (`0x00123400`) first.\n",
        "sys-reimpl.md": reimpl,
    }
    entry = entries(pages_plan(pages))[A]
    assert (entry.page, entry.level) == ("sys.md#walk", "confirmed-code")
    assert "sys-reimpl.md#fix" in entry.note
    alone = entries(pages_plan({"sys-reimpl.md": reimpl}))[A]
    assert (alone.page, alone.level, alone.note) == ("sys-reimpl.md#fix", "confirmed-code", "")
    coney = (
        "## Coney's implementation {#coney}\n\n- **The spawners**: it calls `F123400` (`0x00123400`). "
        "Confirmed (code).\n"
    )
    entry = entries(pages_plan({"sys.md": pages["sys.md"] + coney}))[A]
    assert (entry.page, entry.level) == ("sys.md#walk", "confirmed-code") and "sys.md#coney" in entry.note


SIGHT = "### Sight {#sight}\n\n"
WALK = "### Walk {#walk}\n\nThe walk passes `F123400` (`0x00123400`) on its way (confirmed (code)).\n\n"


def test_the_opening_span_may_name_the_function_chain_wrappers_or_be_a_link() -> None:
    """These openings are the section that describes the function, so it is the home over a mention that grades it."""
    openings = [
        "**Turning it** (`F123400`, `0x00123400`, which stops the sound): mode 1 retunes.",
        "**Sector numbering** (`Sector_Of` `0x00123300` → `F123400` `0x00123400`): take the bearing.",
        "[`SetHandler`](../x.md#sethandler) (`0x00123300` → `0x00123400`) gives the object a handler.",
        "`SetupRadio` (`F123400`, `0x00123400`) makes a radio.",
    ]
    for opening in openings:
        entry = entries(paragraph_plan(WALK + SIGHT + opening + "\n"))[A]
        assert entry.page == "sys.md#sight", opening
        assert entry.level == "confirmed-code" and "sys.md#walk" in entry.note, opening


def test_a_row_whose_first_cell_is_a_sentence_is_not_the_functions_row() -> None:
    """`| **Radar map.** A dictionary which F finds by the name | both discs | inferred |` grades the claim, not F."""
    table = (
        "| Finding | Where | Evidence |\n| --- | --- | --- |\n"
        "| **Radar map.** A dictionary named level7 has the shape of a radar sheet, which `F123400` (`0x00123400`) "
        "finds by the world's name. | both discs | inferred |\n"
    )
    pages = {
        "sys.md": HOME["sys.md"] + SIGHT + "- **`F123400`** (`0x00123400`): a ray. Confirmed (code).\n",
        "other.md": "# Other\n\n## Behaviour {#behaviour}\n\n### Cut {#cut}\n\n" + table,
    }
    entry = entries(pages_plan(pages))[A]
    assert (entry.page, entry.level, entry.note) == ("sys.md#sight", "confirmed-code", "")


def test_an_address_that_ends_a_range_is_graded_by_nothing_the_range_says() -> None:
    table = (
        "| Range | Source file | Evidence |\n| --- | --- | --- |\n"
        "| `0x00123400`-`0x00123500` | the unnamed stretch before `X.cpp` | inferred |\n"
    )
    prose = "The player sits just before (`0x00123380`-`0x00123400`, inferred; [Source map](source-map.md)).\n"
    function = (
        "| Address | Name | Role | Evidence |\n| --- | --- | --- | --- |\n"
        "| `0x00123400` | `F123400` | it | confirmed (code) |\n"
    )
    body = "### Map {#map}\n\n" + table + "\n" + prose + "\n" + function
    entry = entries(paragraph_plan(body))[A]
    assert (entry.level, entry.note) == ("confirmed-code", "")
    only = entries(paragraph_plan("### Map {#map}\n\n" + table + "\n" + prose))[A]
    assert only.level == ""


def test_a_row_shared_with_another_function_is_the_home_after_a_row_of_its_own() -> None:
    shared = (
        "| Address | Name | Role | Evidence |\n| --- | --- | --- | --- |\n"
        "| `0x00123400` / `0x00123700` | `F123400` / `F123700` | both | confirmed (code) |\n"
        "| `0x00123700` | `F123700` | a second row | confirmed (code) |\n"
    )
    own = (
        "| Address | Name | Role | Evidence |\n| --- | --- | --- | --- |\n"
        "| `0x00123400` | `F123400` | it | inferred |\n"
    )
    pages = {
        "big.md": "# Big\n\n## Behaviour {#behaviour}\n\n### Two {#two}\n\n" + shared,
        "small.md": "# Small\n\n## Behaviour {#behaviour}\n\n### One {#one}\n\n" + own,
    }
    entry = entries(pages_plan(pages, (A, 0x00123700)))[A]
    assert (entry.page, entry.level) == ("small.md#one", "inferred")
    assert "also graded confirmed-code at [big.md#two](big.md#two)" in entry.note


def test_disagreeing_grades_are_recorded_for_a_borrowed_level_too_and_show_two_and_a_count() -> None:
    """Grades of the function as the subject of a block (and code readings) are recorded; a side remark is not."""

    def reimpl(level: str) -> str:
        return f"# R\n\n## Behaviour {{#behaviour}}\n\n### Fix {{#fix}}\n\n- **`F123400`** (`0x00123400`) ({level}).\n"

    pages = {
        "sys.md": HOME["sys.md"] + SIGHT + "- **`F123400`** (`0x00123400`): a ray.\n",
        "a.md": "# A\n\n## Behaviour {#behaviour}\n\nIt calls `F123400` (`0x00123400`), confirmed (code).\n",
        "b-reimpl.md": reimpl("inferred"),
        "f.md": "# F\n\n## Behaviour {#behaviour}\n\nIt reads it (`0x00123400`; inferred: to fan out).\n",
    }
    result = pages_plan(pages)
    entry = entries(result)[A]
    assert (entry.page, entry.level) == ("sys.md#sight", "confirmed-code")
    assert "the level is from [a.md#behaviour]" in entry.note
    assert entry.note.endswith("also graded inferred at [b-reimpl.md#fix](b-reimpl.md#fix)"), entry.note
    assert any("inferred" in line for line in result.report.disagreeing)
    # Beyond two, the rest is counted.
    more = {"c-reimpl.md": reimpl("speculative"), "d-reimpl.md": reimpl("confirmed (runtime)")}
    note = entries(pages_plan({**pages, **more}))[A].note
    assert note.endswith("; and 1 more") and note.count("also graded") == 2, note


def test_the_level_is_never_said_to_come_from_the_functions_own_home() -> None:
    """Two blocks of one section: the describing one has no level, the mention does; the section's level is its own."""
    body = (
        "### Path {#path}\n\n**The point** (`0x00123400`): casts a ray.\n\n"
        "The planner calls `F123400` (`0x00123400`), confirmed (code).\n"
    )
    result = paragraph_plan(body)
    entry = entries(result)[A]
    assert (entry.page, entry.level, entry.note) == ("sys.md#path", "confirmed-code", "")
    assert result.report.borrowed == []


def test_an_override_replaces_the_home_and_level_the_rules_chose() -> None:
    body = "### Walk {#walk}\n\nThe walk calls `F123400` (`0x00123400`).\n\n### Other {#other}\n\nElsewhere.\n"
    override = progress_import.Override("sys.md#other", "confirmed-code", "sys.md: Elsewhere.", "the page says so")
    rows = [progress_import.LegacyRow(A, "F", "Alpha", 8)]
    result = progress_import.plan(
        rows,
        listing={A: ListedFunction(A, 8, f"F{A:x}", False)},
        ports={A: "src/core/x.h:f"},
        pages={"sys.md": HOME["sys.md"] + body},
        bindings={},
        existing=[],
        research=research_support.research(),
        overrides={A: override},
    )
    entry = entries(result)[A]
    assert (entry.page, entry.level) == ("sys.md#other", "confirmed-code")
    assert result.report.sources["override"] == 1


@pytest.mark.reads("docs_only")
def test_every_override_still_matches_its_page() -> None:
    """An override is a hand reading of a page: the page, the section and the quoted words must still be there, and the
    block that holds them must cite the address and state the level."""
    assert progress_import.OVERRIDES
    pages = progress_import.read_pages(REPO, frozenset(), include_front_matter=True)
    problems = progress_import.override_problems(progress_import.OVERRIDES, pages)
    assert problems == []


def test_an_override_that_no_longer_matches_is_reported() -> None:
    pages = {"sys.md": "# Sys\n\n## Behaviour {#behaviour}\n\nIt calls `0x00123400`, confirmed (code).\n"}
    good = progress_import.Override("sys.md#behaviour", "confirmed-code", "sys.md: It calls", "x")
    assert progress_import.override_problems({A: good}, pages) == []
    moved = progress_import.Override("sys.md#gone", "confirmed-code", "sys.md: It calls", "x")
    assert progress_import.override_problems({A: moved}, pages)
    words = progress_import.Override("sys.md#behaviour", "confirmed-code", "sys.md: Other words", "x")
    assert progress_import.override_problems({A: words}, pages)
    level = progress_import.Override("sys.md#behaviour", "inferred", "sys.md: It calls", "x")
    assert progress_import.override_problems({A: level}, pages)
    address = progress_import.Override("sys.md#behaviour", "confirmed-code", "sys.md: It calls", "x")
    assert progress_import.override_problems({0x00123500: address}, pages)


def test_an_override_may_take_its_level_from_the_lead_line_of_its_list() -> None:
    """`Shared behaviour, confirmed (code):` over a bullet that cites the address states the bullet's level."""
    page = (
        "# Sys\n\n## Behaviour {#behaviour}\n\nShared behaviour, confirmed (code):\n\n"
        "- **Leader**: it holds `0x00123400`.\n"
    )
    override = progress_import.Override("sys.md#behaviour", "confirmed-code", "sys.md: **Leader**: it holds", "x")
    assert progress_import.override_problems({A: override}, {"sys.md": page}) == []
    other = page.replace("Shared behaviour, confirmed (code):", "Shared behaviour:")
    assert progress_import.override_problems({A: override}, {"sys.md": other})


def test_a_section_whose_heading_names_the_function_is_its_home_over_a_block_elsewhere_that_grades_it() -> None:
    """The section of the function itself is the home; the level of a block about it elsewhere is its level."""
    body = (
        "### Unit 2 {#unit-2}\n\n**Who consumes it** (`F123400`, `0x00123400`, confirmed (code)): a contact.\n\n"
        "### `F123400` {#unit-3}\n\nThe resolver (`0x00123400`) walks the list.\n"
    )
    entry = entries(paragraph_plan(body))[A]
    assert (entry.page, entry.level) == ("sys.md#unit-3", "confirmed-code")
    assert "sys.md#unit-2" in entry.note


def test_an_override_whose_block_moved_to_another_section_is_reported() -> None:
    """The quoted block must lie in the override's own section when the source is the entry's page."""
    page = (
        "# Sys\n\n## Behaviour {#behaviour}\n\nNothing here.\n\n## Other {#other}\n\n"
        "It calls `0x00123400`, confirmed (code).\n"
    )
    override = progress_import.Override("sys.md#behaviour", "confirmed-code", "sys.md: It calls", "x")
    problems = progress_import.override_problems({A: override}, {"sys.md": page})
    assert problems and "not in the section 'behaviour'" in problems[0]
    here = progress_import.Override("sys.md#other", "confirmed-code", "sys.md: It calls", "x")
    assert progress_import.override_problems({A: here}, {"sys.md": page}) == []


def test_override_problems_names_a_gone_page_a_bad_level_and_a_gone_source_page() -> None:
    pages = {"sys.md": "# Sys\n\n## Behaviour {#behaviour}\n\nIt calls `0x00123400`, confirmed (code).\n"}
    gone = progress_import.Override("none.md#x", "confirmed-code", "none.md: It calls", "x")
    assert progress_import.override_problems({A: gone}, pages)[0].endswith("the page is gone")
    bad = progress_import.Override("sys.md#behaviour", "sure", "sys.md: It calls", "x")
    assert any("is no level" in p for p in progress_import.override_problems({A: bad}, pages))
    elsewhere = progress_import.Override("sys.md#behaviour", "confirmed-code", "far.md: It calls", "x")
    assert any("source page far.md is gone" in p for p in progress_import.override_problems({A: elsewhere}, pages))


def test_a_closing_statement_that_lists_other_addresses_without_this_one_gives_no_grade() -> None:
    other = "It does x. Confirmed (code) at `0x00123500`, `0x00123600`."
    assert progress_import.closing_key(other, A) == ""
    listed = "It does x. Confirmed (code) at `0x00123400`, `0x00123600`."
    assert progress_import.closing_key(listed, A) == "confirmed-code"
