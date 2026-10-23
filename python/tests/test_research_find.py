# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the generated parts of the find pages (coney_tools.research_find), on made-up files and addresses."""

from __future__ import annotations

import pytest
import research_support

from coney_tools import research_find
from coney_tools.docs_page import heading_ids, read_page, split_cells
from coney_tools.progress_research import ListedFunction
from coney_tools.research_glossary import Glossary, Term
from coney_tools.research_owners import Owners
from coney_tools.research_pages import Corpus
from coney_tools.source_files import Anchor, SourceFile, SourceFiles, SourceFilesError

FILES = SourceFiles(
    "",
    (
        SourceFile(
            "Alpha",
            "Alpha/One.cpp",
            0x100200,
            0x100400,
            3,
            0x100500,
            (Anchor(0x100200, ("One", "Pool<One>")), Anchor(0x100300, ())),
            2,
        ),
        SourceFile("Alpha", "Alpha/Two.cpp", path_string=0x540000),
        SourceFile("Device/ps2", "Device/ps2/fileio/Three.cpp", 0x100400, 0x100600, 1, None, (Anchor(0x100400),)),
        SourceFile(
            "World", "World/(no path string)", 0x200000, 0x200100, 5, note="the unit before `Four.cpp` (inferred)"
        ),
    ),
)


def rows(page: str) -> list[list[str]]:
    """The cells of every table row of the page, the header and rule rows left out."""
    lines = page.splitlines()
    out = []
    for index, line in enumerate(lines):
        header = index + 1 < len(lines) and lines[index + 1].startswith("| ---")
        if line.startswith("|") and not line.startswith("| ---") and not header:
            out.append(split_cells(line))
    return out


def test_the_tables_are_a_heading_per_directory_in_the_order_of_the_data() -> None:
    """One heading per directory, with the directory's name as the heading text."""
    parsed = read_page(research_find.render_file_tables(FILES))
    assert [(h.level, h.text) for h in parsed.headings] == [(4, "Alpha"), (4, "Device/ps2"), (4, "World")]


def test_a_directorys_heading_id_is_the_one_the_source_map_headings_had() -> None:
    """Links such as `source-map.md#gui` and `#deviceps2` keep their target when the tables become generated."""
    parsed = read_page(research_find.render_file_tables(FILES))
    ids = dict(zip([h.text for h in parsed.headings], heading_ids(parsed), strict=True))
    assert ids["Device/ps2"] == "deviceps2"
    assert ids["Alpha"] == "alpha"


def test_a_row_is_the_file_its_range_its_count_its_stub_and_its_anchors() -> None:
    """The cells as the source map's tables wrote them."""
    one, _two, three, _four = rows(research_find.render_file_tables(FILES))
    assert one == [
        "`One.cpp`",
        "`0x00100200`-`0x00100400`",
        "3",
        "`0x00100500`",
        "`0x00100200` (`One`, `Pool<One>`); `0x00100300`, +2 more",
    ]
    assert three[0] == "`fileio/Three.cpp`"
    assert three[3:] == ["", "`0x00100400`"]


def test_a_file_with_no_range_says_where_its_path_string_is_and_a_note_replaces_the_anchors() -> None:
    """A file no code loads has only the address of its path string; the note takes the anchors' cell."""
    _one, two, _three, four = rows(research_find.render_file_tables(FILES))
    assert two == ["`Two.cpp`", "none (path string at `0x00540000` has no code reference)", "0", "", ""]
    assert four[0] == "(no path string)"
    assert four[4] == "the unit before `Four.cpp` (inferred)"


def test_a_pipe_in_a_tag_cannot_end_the_cell() -> None:
    """Tags are strings the allocator took; one with a pipe is escaped."""
    odd = SourceFiles("", (SourceFile("A", "A/x.cpp", 0x100200, 0x100300, 1, None, (Anchor(0x100200, ("a|b",)),)),))
    (cells,) = rows(research_find.render_file_tables(odd))
    assert cells[4] == "`0x00100200` (`a\\|b`)"
    assert len(cells) == 5


def test_every_file_of_the_data_has_exactly_one_row() -> None:
    """Nothing is lost between the data and the page."""
    assert len(rows(research_find.render_file_tables(FILES))) == len(FILES.files)


PAGE = f"# Source map\n\n## Files\n\nWhat the columns mean.\n\n{research_find.FILES_MARKER}\n\n## After\n"


def test_the_marker_is_replaced_by_a_note_and_the_tables() -> None:
    """The page keeps its own text around the generated tables, and says they are generated."""
    page = research_find.fill_source_map(PAGE, FILES)
    assert research_find.FILES_MARKER not in page
    assert page.startswith("# Source map\n\n## Files\n\nWhat the columns mean.\n\n<!-- Generated when the docs")
    assert "#### Alpha" in page
    assert "\n\n## After" in page
    assert page.count("| `One.cpp` |") == 1


@pytest.mark.parametrize("text", ["# Source map\n", PAGE + research_find.FILES_MARKER + "\n"])
def test_a_page_without_exactly_one_marker_is_refused(text: str) -> None:
    """Without the marker the tables would silently not appear; twice would repeat them."""
    with pytest.raises(SourceFilesError, match="exactly one"):
        research_find.fill_source_map(text, FILES)


# --- the Page column -------------------------------------------------------------------------------------------------


def owner_of(file: SourceFile) -> str:
    """Made-up owners: `One.cpp` is described by a page, the rest by none."""
    return "docs/research/combat/grabs.md" if file.name == "One.cpp" else ""


def test_a_page_column_appears_when_some_file_has_an_owning_page() -> None:
    """The column links the page that owns the file, relative to the source map; the rest of the rows stay blank."""
    page = research_find.render_file_tables(FILES, owner_of)
    assert (
        "| File | Attributed range | Functions | Static-init stub | Anchors (allocation tags they pass) | Page |"
        in page
    )
    one, two, *_ = rows(page)
    assert one[5] == "[combat/grabs.md](combat/grabs.md)"
    assert two[5] == ""


def test_without_an_owning_page_there_is_no_column() -> None:
    """A column nobody uses would be a column of nothing."""
    assert "| Page |" not in research_find.render_file_tables(FILES, lambda file: "")
    assert "| Page |" not in research_find.render_file_tables(FILES)


# --- the address map and the words ------------------------------------------------------------------------------------

FUNCTIONS = (
    ListedFunction(0x100200, 0x100, "Grab_Start", True),
    ListedFunction(0x100300, 0x100, "Grab_End", True),
    ListedFunction(0x100400, 0x80, "", False),
    ListedFunction(0x200000, 0x40, "Other_Function", True),
)


def owners_of(**owns: str) -> Owners:
    """Owners from made-up pages (`name=` the YAML of its `owns`), over the files of this module."""
    texts = {
        f"docs/research/{name}.md": f"---\nkind: survey\nowns: {text}\n---\n# {name}\n" for name, text in owns.items()
    }
    ranges = tuple(r for f in FILES.files if (r := f.range()) is not None)
    return Owners(Corpus.from_texts(texts, research_support.research()), ranges)


def test_the_address_map_joins_neighbours_with_the_same_owner() -> None:
    """Consecutive functions of one page are one row, and so are consecutive functions nobody owns."""
    owners = owners_of(grabs="{functions: ['0x00100200', '0x00100300']}")
    page = research_find.render_addresses(FUNCTIONS, owners)
    assert "4 functions: 2 owned by a page, 2 with no page." in page
    runs = [cells for cells in rows(page) if cells[0].startswith("`0x")]
    assert runs == [
        ["`0x00100200`-`0x00100400`", "2", "[grabs.md](../grabs.md)"],
        ["`0x00100400`-`0x00200040`", "2", "(no page)"],
    ]


def test_the_pages_that_own_something_are_listed_with_what_they_own() -> None:
    """The first table: each page, what its front matter lists, and how many listed functions it holds."""
    owners = owners_of(
        grabs="{functions: ['0x00100200']}", combat="{files: ['Combat/Hold.cpp'], ranges: ['0x00300000-0x00300100']}"
    )
    page = research_find.render_addresses(FUNCTIONS, owners)
    owning = [cells for cells in rows(page) if cells[0].startswith("[")]
    assert owning == [
        ["[combat.md](../combat.md)", "1 file, 1 range", "0"],
        ["[grabs.md](../grabs.md)", "1 function", "1"],
    ]


def test_with_no_owner_at_all_the_map_is_one_row() -> None:
    """Today every page is without `owns`: one row says so."""
    page = research_find.render_addresses(FUNCTIONS, owners_of())
    assert "4 functions: 0 owned by a page, 4 with no page." in page
    assert [cells for cells in rows(page) if cells[0].startswith("`0x")] == [
        ["`0x00100200`-`0x00200040`", "4", "(no page)"]
    ]


def test_the_address_map_without_a_listing_says_what_is_missing() -> None:
    """A checkout without the Ghidra listing has no functions to place."""
    assert "listing" in research_find.render_addresses((), owners_of())


def test_the_words_page_lists_each_term_with_its_page_and_functions() -> None:
    """One row per term, in alphabetical order, with the other names and the functions' Ghidra names."""
    glossary = Glossary(
        (
            Term("sprint", "movement.md", (0x100200,)),
            Term("mount", "combat/grabs.md#mount", (0x100200, 0x100300), ("pin", "ground and pound")),
        )
    )
    page = research_find.render_words(glossary, {0x100200: "Grab_Start", 0x100300: ""})
    table = [cells for cells in rows(page)]
    assert table[0] == [
        "mount",
        "[combat/grabs.md#mount](../combat/grabs.md#mount)",
        "`0x00100200` `Grab_Start`, `0x00100300`",
        "pin, ground and pound",
    ]
    assert table[1][0] == "sprint"


def test_an_empty_glossary_says_how_to_add_a_word() -> None:
    """Before anyone lists a word the page is not an empty table."""
    page = research_find.render_words(Glossary(), {})
    assert "research/glossary.yaml" in page
    assert rows(page) == []
