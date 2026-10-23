# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.docs_page: a Markdown page read into headings, blocks and tables, on made-up text."""

from __future__ import annotations

import pytest

from coney_tools.docs_page import kind_of, level_column, read_page, slug, split_cells


@pytest.mark.parametrize(
    ("path", "kind"),
    [
        ("docs/research/camera.md", "research"),
        ("docs/research/combat/grabs.md", "research"),
        ("docs/research/index.md", "page"),
        ("docs/research/find/addresses.md", "generated"),
        ("docs/research/combat/functions.md", "generated"),
        ("docs/guides/functions.md", "guide"),
        ("docs/research/notes.txt", "other"),
    ],
)
def test_the_research_folder_has_pages_a_landing_page_and_generated_lists(path: str, kind: str) -> None:
    assert kind_of(path) == kind


def test_front_matter_is_kept_apart_and_its_lines_stay_numbered() -> None:
    page = read_page("---\nkind: topic\nsystem: a\n---\n# Title\n\nText.\n")
    assert page.front_matter == 4 and page.front_text == "kind: topic\nsystem: a"
    assert page.headings[0].line == 5 and page.line_count == 7 and page.body_lines == 3
    assert all(not line.text for line in page.lines[:4])


def test_a_page_without_front_matter_has_none() -> None:
    page = read_page("# Title\n\n---\n\nrule above\n")
    assert page.front_matter == 0 and page.front_text == "" and page.body_lines == 5


def test_headings_keep_their_anchor_and_leave_it_out_of_the_text() -> None:
    page = read_page("# T\n\n## Data {#data-section}\n\n### Deep {#d .note}\n\n## Plain\n")
    assert [(h.level, h.text, h.anchor) for h in page.headings] == [
        (1, "T", ""),
        (2, "Data", "data-section"),
        (3, "Deep", "d"),
        (2, "Plain", ""),
    ]


def test_anchors_come_from_headings_slugs_and_inline_marks() -> None:
    page = read_page("# T\n\n## Open questions {#oq}\n\n- **Spin** {#q-spin}: unknown.\n\n```\n{#hidden}\n```\n")
    assert {"oq", "q-spin", "open-questions"} <= page.anchors and "hidden" not in page.anchors


def test_a_slug_is_the_id_mkdocs_gives() -> None:
    assert slug("Open questions") == "open-questions"
    assert slug("`Grab_Start` and the (hold)") == "grab_start-and-the-hold"


def test_blocks_are_paragraphs_items_and_rows() -> None:
    text = (
        "# T\n\nOne line\ncontinues.\n\n- item a\n- item b\n  wraps\n\n"
        "| A | B |\n| - | - |\n| 1 | 2 |\n\n```\ncode\n```\n"
    )
    page = read_page(text)
    shown = [(block.kind, block.text) for block in page.blocks]
    assert shown == [
        ("heading", "T"),
        ("text", "One line continues."),
        ("text", "- item a"),
        ("text", "- item b wraps"),
        ("row", "| A | B |"),
        ("row", "| 1 | 2 |"),
    ]
    assert [block.item for block in page.blocks if block.kind == "text"] == [False, True, True]


def test_comments_and_fences_never_reach_a_block() -> None:
    page = read_page("# T\n\n<!-- a\ncomment -->\n\ntext\n\n````\n```\n# not a heading\n```\n````\n")
    assert [block.text for block in page.blocks] == ["T", "text"]
    assert [heading.text for heading in page.headings] == ["T"]


def test_a_table_knows_its_level_column() -> None:
    page = read_page("| Address | Evidence |\n| - | - |\n| 0x1 | x |\n\n| A | B |\n| - | - |\n| 1 | 2 |\n")
    assert [table.level_column for table in page.tables] == [1, None]
    assert page.tables[0].graded and not page.tables[1].graded


def test_cells_split_on_pipes_outside_code_and_escapes() -> None:
    assert split_cells("| a | `b | c` | d \\| e |") == ["a", "`b | c`", "d \\| e"]
    assert split_cells("a | b") == ["a", "b"]
    assert split_cells("| a | |") == ["a", ""]


@pytest.mark.parametrize(
    ("header", "column"),
    [
        ("| Address | Evidence |", 1),
        ("| Evidence level | Address |", 0),
        ("| A | **Confidence** |", 1),
        ("| Level | Address |", None),
        ("| Evidence of | x |", None),
    ],
)
def test_the_level_column_is_headed_evidence_or_confidence(header: str, column: int | None) -> None:
    assert level_column(header) == column
