# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the generated parts of the find pages (coney_tools.research_find), on made-up files and addresses."""

from __future__ import annotations

import pytest

from coney_tools import research_find
from coney_tools.docs_page import heading_ids, read_page, split_cells
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
    """The cells of every table row of the page, header and rule rows left out."""
    out = []
    for line in page.splitlines():
        if line.startswith("|") and not line.startswith("| ---") and not line.startswith("| File"):
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
