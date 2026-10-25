# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the home page's progress picture: the progress picture's data in three states and the home palette.

The home page (docs/index.md) shows the same squares as the progress page, one per function, folded into three states
(reimplemented, understood, not yet started) and coloured by the home page's own CSS classes. The inputs are
test_progress_grid's made-up checkout: Alpha_Init reimplemented, Alpha_Done understood, Alpha_Step and Beta_Run
researched and the unnamed helper untouched. One test builds the picture of the real checkout to hold its weight.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from test_progress_grid import grid_of, make_grid_checkout

from coney_tools import progress, progress_grid, progress_picture, progress_render, progress_site

REPO = Path(__file__).resolve().parents[2]
SQUARE = f"h{progress_picture.CELL}v{progress_picture.CELL}h-{progress_picture.CELL}z"


def squares_by_index(svg: str) -> dict[tuple[int, int], str]:
    """Each square of `svg` as (block index, square index) to the class of the path that draws it.

    Decodes every path's moves back to corners and turns a corner into its index from the block's data attributes,
    exactly as the page's script finds the square under the pointer.
    """
    found: dict[tuple[int, int], str] = {}
    for block, attrs, body in re.findall(r'<g class="cg-b" data-b="(\d+)"([^>]*)>(.*?)</g>', svg):
        data = dict(re.findall(r'data-(\w)="(\d+)"', attrs))
        x, y, cols = int(data["x"]), int(data["y"]), int(data["c"])
        for cls, d in re.findall(r'<path class="([^"]+)" d="([^"]+)"/>', body):
            position = [0, 0]
            for command, a, b in re.findall(rf"([Mm])(-?\d+) (-?\d+){SQUARE}", d):
                moved = [position[0] + int(a), position[1] + int(b)]
                position = [int(a), int(b)] if command == "M" else moved
                column = (position[0] - x) // progress_picture.PITCH
                row = (position[1] - y) // progress_picture.PITCH
                found[(int(block), row * cols + column)] = cls
    return found


# --- the states -------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("state", "home"),
    [
        ("audited", "reimplemented"),
        ("reimplemented", "reimplemented"),
        ("understood", "understood"),
        ("researched", "not-started"),
        ("untouched", "not-started"),
    ],
)
def test_each_progress_state_folds_into_one_home_state(state: str, home: str) -> None:
    """Audited counts as reimplemented; placed by the source map alone is not yet started."""
    assert progress_grid.HOME_STATE_OF[state] == home


def test_the_home_states_are_three_with_a_class_label_and_palette_each() -> None:
    assert progress_grid.HOME_STATES == ("reimplemented", "understood", "not-started")
    assert set(progress_grid.HOME_STATE_OF) == set(progress_grid.STATES)
    assert set(progress_grid.HOME_STATE_OF.values()) == set(progress_grid.HOME_STATES)
    assert progress_grid.HOME_CLASSES == {
        "reimplemented": "cg-h-reimplemented",
        "understood": "cg-h-understood",
        "not-started": "cg-h-not-started",
    }
    assert progress_grid.HOME_LABELS["not-started"] == "Not yet started"
    assert set(progress_grid.HOME_PALETTE) == set(progress_grid.HOME_STATES)


# --- the picture ------------------------------------------------------------------------------------------------------


def test_every_square_gets_the_home_class_of_its_function(tmp_path: Path) -> None:
    """Square i of block b is function i of block b of the grid (what the tooltips assume), in its home class."""
    grid = grid_of(make_grid_checkout(tmp_path))
    svg = progress_grid.render_home_svg(grid)
    squares = squares_by_index(svg)
    expected = {
        (b, i): progress_grid.HOME_CLASSES[progress_grid.HOME_STATE_OF[f.state]]
        for b, block in enumerate(grid.blocks)
        for i, f in enumerate(block.functions)
    }
    assert squares == expected
    by_address = {
        f.address: squares[(b, i)] for b, block in enumerate(grid.blocks) for i, f in enumerate(block.functions)
    }
    assert by_address == {
        0x1000: "cg-h-reimplemented",
        0x1100: "cg-h-not-started",
        0x1180: "cg-h-understood",
        0x1200: "cg-h-not-started",
        0x1500: "cg-h-not-started",
    }


def test_an_audited_function_is_a_reimplemented_square(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path), (0x1000,))
    assert squares_by_index(progress_grid.render_home_svg(grid))[(0, 0)] == "cg-h-reimplemented"


def test_the_home_picture_is_wide_unlabelled_and_uses_only_the_home_classes(tmp_path: Path) -> None:
    """No block labels (they would be unreadable at the home page's scale), so the picture is squares only."""
    svg = progress_grid.render_home_svg(grid_of(make_grid_checkout(tmp_path)))
    assert f'viewBox="0 0 {progress_grid.HOME_COLS * progress_picture.PITCH} ' in svg
    assert 'data-label="0"' in svg and 'data-view="functions"' in svg
    assert "<text" not in svg and "cg-s-" not in svg and "style" not in svg
    assert "\n\n" not in svg  # Markdown ends an HTML block at a blank line
    assert svg.count("<title>") == 1 + len(grid_of(make_grid_checkout(tmp_path / "b")).blocks)


def test_the_progress_pictures_keep_their_labels(tmp_path: Path) -> None:
    svg = progress_grid.render_functions_svg(grid_of(make_grid_checkout(tmp_path)), False, uid="pf")
    assert f'data-label="{progress_picture.LABEL_HEIGHT}"' in svg and "<text" in svg


def test_layout_without_labels_starts_at_the_top_and_stays_inside_the_width() -> None:
    blocks = [progress_picture.Block(f"A long block label {i}", "t", ["x"] * n) for i, n in enumerate([900, 3, 40])]
    placements, height = progress_picture.layout(blocks, max_cols=150, labels=False)
    places = [p for p in placements if p is not None]
    assert places[0].y == 0 and places[0].label_y == 0
    for place in places:
        assert place.x + place.cols * progress_picture.PITCH <= 150 * progress_picture.PITCH
        assert place.y + place.rows * progress_picture.PITCH <= height
    # A three-square block is three squares wide plus nothing for its label.
    labelled, _ = progress_picture.layout(blocks, max_cols=150)
    assert sum(p.cols for p in places) >= sum(p.cols for p in labelled if p is not None)


# --- the section and the page -----------------------------------------------------------------------------------------


def test_the_home_section_counts_the_three_states_and_starts_the_tooltips(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path))
    html = progress_site.home_section(grid, "https://example.invalid/r")
    assert '<div class="progress-grid progress-functions progress-home"' in html
    # The page is the site's root, so the table is one folder down; the script and its tooltip are the progress page's.
    assert 'data-functions="progress/functions.json"' in html and 'data-repo="https://example.invalid/r"' in html
    assert '<div class="cg-tip" role="status" aria-live="polite" hidden></div>' in html
    legend = re.findall(r'<span class="cg-key (cg-h-[\w-]+)"></span><strong>([^<]+)</strong> ([\d,]+)', html)
    assert legend == [
        ("cg-h-reimplemented", "Reimplemented", "1"),
        ("cg-h-understood", "Understood", "1"),
        ("cg-h-not-started", "Not yet started", "3"),
    ]
    assert "1 of 5 functions" in html
    assert "\n\n" not in html


def test_the_site_carries_the_home_section(tmp_path: Path) -> None:
    root = make_grid_checkout(tmp_path)
    site = progress_site.build_site(root, progress.load(root), head=("2026-10-25", "abc1234"))
    # One table serves both pages' tooltips: the home page adds no file of its own.
    assert "progress-home" in site.home_html and "progress/functions.json" in site.files
    assert not any("home" in name for name in site.files)
    assert "progress-home" not in site.grid_html


def test_the_home_page_needs_exactly_one_marker() -> None:
    text = f"# Coney\n\n{progress_render.HOME_MARKER}\n"
    assert "GRID" in progress_render.fill_home_page(text, "GRID", "index.md")
    with pytest.raises(progress.ProgressError, match=r"index\.md: needs exactly one <!-- progress:home -->"):
        progress_render.fill_home_page("no marker", "GRID", "index.md")


def test_the_real_home_page_has_its_marker() -> None:
    text = (REPO / progress.HOME_PAGE_FILE).read_text(encoding="utf-8")
    assert text.count(progress_render.HOME_MARKER) == 1


def test_the_real_home_picture_stays_light() -> None:
    """Every function is drawn and the home page gains well under the progress page's weight (its limit is 600 KB)."""
    state = progress.load(REPO)
    grid = progress_grid.build_grid(state, REPO)
    assert grid is not None
    html = progress_site.home_section(grid, None)
    assert html.count(SQUARE) == grid.total()[0]
    assert len(html.encode()) < 250_000, len(html)


# --- the stylesheet ---------------------------------------------------------------------------------------------------


def test_the_stylesheet_holds_the_home_palette_in_both_themes() -> None:
    """progress-grid.css defines each home colour, light and dark, as HOME_PALETTE has it, and fills each class."""
    css = (REPO / "docs" / "stylesheets" / "progress-grid.css").read_text(encoding="utf-8")
    light = css[css.index(".progress-home {") :].split("}", 1)[0]
    dark = css[css.index('[data-md-color-scheme="slate"] .progress-home {') :].split("}", 1)[0]
    for state, (day, night) in progress_grid.HOME_PALETTE.items():
        assert re.search(rf"--cg-h-{state}:\s*{day};", light, re.IGNORECASE), state
        assert re.search(rf"--cg-h-{state}:\s*{night};", dark, re.IGNORECASE), state
        cls = progress_grid.HOME_CLASSES[state]
        assert re.search(rf"\.progress-home svg \.{cls}\s*\{{\s*fill: var\(--cg-h-{state}\)", css), cls
        assert f".progress-home .cg-key.{cls}" in css, cls


def test_the_tooltip_swatch_on_the_home_page_has_the_squares_colour() -> None:
    """The shared tooltip names the progress page's state; on the home page its swatch shows the home colour."""
    css = (REPO / "docs" / "stylesheets" / "progress-grid.css").read_text(encoding="utf-8")
    for state, home in progress_grid.HOME_STATE_OF.items():
        rule = re.search(rf"\.progress-home \.cg-key\.cg-s-{state}\b[^{{]*\{{([^}}]*)\}}", css)
        assert rule is not None, state
        assert f"var(--cg-h-{home})" in rule.group(1), state
