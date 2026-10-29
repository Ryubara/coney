# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the home page's progress picture: the progress picture's data in three states and the home palette.

The home page (docs/index.md) shows the same squares as the progress page, one per function, folded into three states
(reimplemented, understood, not yet started) and coloured by the home page's own CSS classes. The inputs are
test_progress_grid's made-up checkout: Alpha_Init reimplemented, Alpha_Done understood, Alpha_Step and Beta_Run
researched and the unnamed helper untouched. One test builds the picture of the real checkout to hold its weight.
"""

from __future__ import annotations

import json
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
    for block, attrs, body in re.findall(r'<g class="cg-b" data-b="(\d+)"([^>]*)>(.*?)</g>', svg, re.DOTALL):
        data = dict(re.findall(r'data-(\w)="(\d+)"', attrs))
        x, y, cols, offset = int(data["x"]), int(data["y"]), int(data["c"]), int(data.get("o", 0))
        for cls, d in re.findall(r'<path class="([^"]+)" d="([^"]+)"/>', body):
            position = [0, 0]
            for command, a, b in re.findall(rf"([Mm])(-?\d+) (-?\d+){SQUARE}", d):
                moved = [position[0] + int(a), position[1] + int(b)]
                position = [int(a), int(b)] if command == "M" else moved
                column = (position[0] - x) // progress_picture.PITCH
                row = (position[1] - y) // progress_picture.PITCH
                found[(int(block), row * cols + column - offset)] = cls
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


def test_the_home_picture_is_one_unlabelled_run_in_only_the_home_classes(tmp_path: Path) -> None:
    """No block labels (they would be unreadable at the home page's scale), so the picture is squares only."""
    svg = progress_grid.render_home_svg(grid_of(make_grid_checkout(tmp_path)))
    width = progress_grid.HOME_COLS * progress_picture.PITCH
    assert f'viewBox="0 0 {width} {progress_picture.PITCH}"' in svg  # five functions fill part of one row
    assert 'data-label="0"' in svg and 'data-view="functions"' in svg
    assert "<text" not in svg and "cg-s-" not in svg and "style" not in svg
    assert "\n\n" not in svg  # Markdown ends an HTML block at a blank line
    assert svg.count("<title") == 1 + len(grid_of(make_grid_checkout(tmp_path / "b")).blocks)


def test_the_progress_pictures_keep_their_labels(tmp_path: Path) -> None:
    svg = progress_grid.render_functions_svg(grid_of(make_grid_checkout(tmp_path)), uid="pf")
    assert f'data-label="{progress_picture.LABEL_HEIGHT}"' in svg and "<text" in svg


def test_flow_puts_each_block_where_the_last_one_ended() -> None:
    """Blocks of 250, 3, none and 40 squares, 100 to a row: offsets 0, 250 and 253 and 293 squares in 3 rows."""
    blocks = [progress_picture.Block(f"B{i}", "t", ["x"] * n) for i, n in enumerate([250, 3, 0, 40])]
    placements, height = progress_picture.flow(blocks, 100)
    assert placements[2] is None
    assert [(p.x, p.y, p.cols, p.offset, p.rows) for p in placements if p is not None] == [
        (0, 0, 100, 0, 3),
        (0, 0, 100, 250, 3),
        (0, 0, 100, 253, 3),
    ]
    assert height == 3 * progress_picture.PITCH


def test_a_flowing_block_draws_its_squares_after_the_offset() -> None:
    """Square 0 of a block at offset 98 in rows of 100 is column 98; square 2 wraps to column 0 of the next row."""
    blocks = [progress_picture.Block("A", "t", ["u"] * 98), progress_picture.Block("B", "t", ["r"] * 3)]
    svg = progress_picture.render_svg(
        blocks, uid="x", title="t", description="d", classes={"u": "u", "r": "r"}, palette={}, flow_cols=100
    )
    assert 'data-o="98"' in svg and 'data-label="0"' in svg and "<text" not in svg
    pitch = progress_picture.PITCH
    assert f'<path class="r" d="M{98 * pitch} 0{SQUARE}m{pitch} 0{SQUARE}m-{99 * pitch} {pitch}{SQUARE}"/>' in svg
    assert squares_by_index(svg)[(1, 2)] == "r"


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
    assert html.count(SQUARE) == grid.total()
    assert len(html.encode()) < 250_000, len(html)


# --- one name for each state -----------------------------------------------------------------------------------------


def test_the_tooltip_labels_are_the_legends_for_every_progress_state() -> None:
    """A hovered square names its state as the legend does: researched and untouched are both "Not yet started"."""
    labels = progress_grid.home_state_labels()
    assert labels == {
        "a": "Reimplemented",
        "r": "Reimplemented",
        "u": "Understood",
        "p": "Not yet started",
        "t": "Not yet started",
    }
    assert set(labels) == set(progress_grid.STATE_CODES.values())
    assert set(labels.values()) == set(progress_grid.HOME_LABELS.values())


def test_the_home_section_hands_the_tooltip_script_the_legends_names(tmp_path: Path) -> None:
    """data-labels is HOME_LABELS by state code, and the shared script prefers it to its own progress page names."""
    html = progress_site.home_section(grid_of(make_grid_checkout(tmp_path)), None)
    attr = re.search(r"data-labels='([^']*)'", html)
    assert attr is not None
    assert json.loads(attr.group(1)) == progress_grid.home_state_labels()
    script = (REPO / "docs" / "javascripts" / "progress-grid.js").read_text(encoding="utf-8")
    assert 'getAttribute("data-labels")' in script
    # The progress page's own picture has no such attribute, so its tooltips keep the five state names.
    root = make_grid_checkout(tmp_path / "b")
    page = progress_site.build_site(root, progress.load(root), head=("2026-10-25", "abc1234"))
    assert "data-labels" not in page.grid_html


def test_the_home_blocks_text_names_only_home_states(tmp_path: Path) -> None:
    """The block text shown before the table loads counts the folded states, never "Researched" or "Untouched"."""
    svg = progress_grid.render_home_svg(grid_of(make_grid_checkout(tmp_path)))
    titles = re.findall(r"<title>([^<]*)</title>", svg)
    assert "Alpha: 1/3 reimplemented (33.3%)\nReimplemented 1 · Understood 1 · Not yet started 1" in titles
    for word in ("Researched", "Untouched", "Audited"):
        assert word not in svg


def test_the_legend_and_the_tooltip_use_one_label_table() -> None:
    """Nothing in the home path spells a state name of its own: every one is read from HOME_LABELS."""
    source = (REPO / "python" / "src" / "coney_tools" / "progress_site.py").read_text(encoding="utf-8")
    home = source[source.index("def home_section") : source.index("# --- the whole site")]
    assert "HOME_LABELS" in home and "Not yet started" not in home


# --- the colours ---------------------------------------------------------------------------------------------------


def _luminance(colour: str) -> float:
    """Relative luminance (WCAG) of `#rrggbb`."""

    def channel(i: int) -> float:
        c = int(colour[1 + 2 * i : 3 + 2 * i], 16) / 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return 0.2126 * channel(0) + 0.7152 * channel(1) + 0.0722 * channel(2)


def _lightness(colour: str) -> float:
    """CIE L* of `#rrggbb`, a perceptual lightness from 0 to 100."""
    y = _luminance(colour)
    return 116 * y ** (1 / 3) - 16 if y > 0.008856 else 903.3 * y


def _contrast(a: str, b: str) -> float:
    """WCAG contrast ratio of two colours."""
    high, low = sorted((_luminance(a), _luminance(b)), reverse=True)
    return (high + 0.05) / (low + 0.05)


#: The docs site's page background in each theme (Material's default and slate schemes).
PAGE = ("#ffffff", "#1e2129")


def test_the_greys_are_swapped_so_understood_is_the_paler_one_on_the_light_page() -> None:
    """The maintainer's scheme of 2026-10-26: Not yet started is the stronger grey and Understood the softer one."""
    palette = progress_grid.HOME_PALETTE
    light_understood, dark_understood = palette["understood"]
    light_waiting, dark_waiting = palette["not-started"]
    # Light page: Understood is lighter than Not yet started but darker than the old near-white (#e6e9ed).
    assert _lightness(light_understood) > _lightness(light_waiting)
    assert _lightness(light_understood) < _lightness("#e6e9ed")
    # Dark page: Understood is the dimmer grey, as on the light page it is the softer one.
    assert _lightness(dark_understood) < _lightness(dark_waiting)


@pytest.mark.parametrize("theme", [0, 1])
def test_the_three_states_are_distinct_and_readable_in_both_themes(theme: int) -> None:
    """Each pair differs clearly in lightness, and every state stands out from the page behind it."""
    colours = {state: palette[theme] for state, palette in progress_grid.HOME_PALETTE.items()}
    states = list(colours)
    # The green is a signal and needs the usual 3:1 against the page; the greys are quieter, but still show on it.
    minimum = {"reimplemented": 3.0, "understood": 1.4, "not-started": 1.4}
    for i, a in enumerate(states):
        assert _contrast(colours[a], PAGE[theme]) >= minimum[a], a
        for b in states[i + 1 :]:
            assert abs(_lightness(colours[a]) - _lightness(colours[b])) >= 8, (a, b)


def test_the_stylesheet_holds_the_swapped_greys() -> None:
    css = (REPO / "docs" / "stylesheets" / "progress-grid.css").read_text(encoding="utf-8")
    light = css[css.index(".progress-home {") :].split("}", 1)[0]
    assert re.search(r"--cg-h-understood:\s*#d0d6dc;", light, re.IGNORECASE)
    assert re.search(r"--cg-h-not-started:\s*#b0b8c2;", light, re.IGNORECASE)


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
    """The shared tooltip's swatch shows the square's home colour on the home page (its name is the legend's)."""
    css = (REPO / "docs" / "stylesheets" / "progress-grid.css").read_text(encoding="utf-8")
    for state, home in progress_grid.HOME_STATE_OF.items():
        rule = re.search(rf"\.progress-home \.cg-key\.cg-s-{state}\b[^{{]*\{{([^}}]*)\}}", css)
        assert rule is not None, state
        assert f"var(--cg-h-{home})" in rule.group(1), state
