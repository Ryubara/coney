# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the README's progress picture: the home page's squares as a PNG, light and dark.

The inputs are test_progress_grid's made-up checkout: Alpha_Init reimplemented, Alpha_Done understood, Alpha_Step and
Beta_Run researched and the unnamed helper untouched, so the squares are reimplemented, not-started, understood,
not-started, not-started. One test builds the picture of the real checkout to hold the file's weight.
"""

from __future__ import annotations

import io
import re
import tomllib
from pathlib import Path

import pytest
from PIL import Image
from test_progress_grid import grid_of, make_grid_checkout

from coney_tools import progress, progress_cli, progress_grid, progress_png, progress_site

REPO = Path(__file__).resolve().parents[2]


def rgb(colour: str) -> tuple[int, int, int]:
    """`#rrggbb` as a pixel."""
    return (int(colour[1:3], 16), int(colour[3:5], 16), int(colour[5:7], 16))


def decode(data: bytes) -> Image.Image:
    """The PNG's pixels as RGB."""
    return Image.open(io.BytesIO(data)).convert("RGB")


# --- the squares ------------------------------------------------------------------------------------------------------


def test_the_cells_are_the_home_pictures_squares_in_order(tmp_path: Path) -> None:
    """One cell per function, in the home picture's order, each in its home state."""
    grid = grid_of(make_grid_checkout(tmp_path))
    assert progress_png.home_cells(grid) == [
        "reimplemented",
        "not-started",
        "understood",
        "not-started",
        "not-started",
    ]
    expected = [progress_grid.HOME_STATE_OF[f.state] for block in grid.blocks for f in block.functions]
    assert progress_png.home_cells(grid) == expected


@pytest.mark.parametrize("theme", ["light", "dark"])
def test_each_square_has_the_home_colour_of_its_state_in_that_theme(tmp_path: Path, theme: str) -> None:
    """The centre of square i is HOME_PALETTE's colour for its state: the one source the home chart uses."""
    grid = grid_of(make_grid_checkout(tmp_path))
    image = decode(progress_png.render_home_png(grid, theme))
    column = 0 if theme == "light" else 1
    for index, state in enumerate(progress_png.home_cells(grid)):
        x, y = progress_png.square_centre(index)
        assert image.getpixel((x, y)) == rgb(progress_grid.HOME_PALETTE[state][column]), (theme, index, state)


@pytest.mark.parametrize("theme", ["light", "dark"])
def test_the_gaps_and_the_margin_are_the_background(tmp_path: Path, theme: str) -> None:
    image = decode(progress_png.render_home_png(grid_of(make_grid_checkout(tmp_path)), theme))
    background = rgb(progress_png.THEMES[theme].background)
    x, y = progress_png.square_centre(0)
    # The first pixel of the gap right of square 0 (the centre is half a square in from its left edge).
    assert image.getpixel((x + progress_png.SQUARE * progress_png.SCALE // 2, y)) == background
    assert image.getpixel((0, 0)) == background
    assert image.getpixel((image.width - 1, image.height - 1)) == background


def test_the_picture_has_the_documented_size(tmp_path: Path) -> None:
    """Whole rows of squares: the width is fixed (it is shown at 880 px), the height follows the function count."""
    grid = grid_of(make_grid_checkout(tmp_path))
    image = decode(progress_png.render_home_png(grid, "light"))
    assert image.width == progress_png.WIDTH == 880 * progress_png.SCALE
    count = len(progress_png.home_cells(grid))
    assert progress_png.height_of(count) == image.height
    assert progress_png.height_of(progress_png.COLS + 1) - progress_png.height_of(progress_png.COLS) == (
        progress_png.PITCH * progress_png.SCALE
    )


def test_the_square_centre_wraps_at_the_column_count() -> None:
    x0, y0 = progress_png.square_centre(0)
    x1, y1 = progress_png.square_centre(1)
    xw, yw = progress_png.square_centre(progress_png.COLS)
    step = progress_png.PITCH * progress_png.SCALE
    assert (x1 - x0, y1 - y0, xw - x0, yw - y0) == (step, 0, 0, step)


# --- the words --------------------------------------------------------------------------------------------------------


def test_the_header_and_the_legend_are_drawn_with_the_themes_text_and_the_swatches(tmp_path: Path) -> None:
    """Text pixels above the squares and a swatch of each state's colour below them."""
    grid = grid_of(make_grid_checkout(tmp_path))
    for theme, column in (("light", 0), ("dark", 1)):
        image = decode(progress_png.render_home_png(grid, theme))
        background = rgb(progress_png.THEMES[theme].background)
        top = {image.getpixel((x, y)) for x in range(image.width) for y in range(progress_png.CHART_TOP)}
        assert background in top and len(top) > 2  # text, with its anti-aliasing
        assert rgb(progress_png.THEMES[theme].text) in top
        bottom = {
            image.getpixel((x, y))
            for x in range(image.width)
            for y in range(progress_png.chart_bottom(len(progress_png.home_cells(grid))), image.height)
        }
        for state in progress_grid.HOME_STATES:
            assert rgb(progress_grid.HOME_PALETTE[state][column]) in bottom, (theme, state)


def test_the_words_carry_the_numbers_so_they_change_with_the_counts(tmp_path: Path) -> None:
    """The percentage and the counts are in the picture: an audit result (one more reimplemented) redraws the header."""
    root = make_grid_checkout(tmp_path)
    before = decode(progress_png.render_home_png(grid_of(root), "light"))
    after = decode(progress_png.render_home_png(grid_of(root, (0x1180,)), "light"))
    header = (0, 0, before.width, progress_png.CHART_TOP)
    assert before.crop(header).tobytes() != after.crop(header).tobytes()


def test_the_text_is_the_legends_names_and_the_home_headline(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path))
    lines = progress_png.text_lines(grid)
    assert lines.title == "Coney: 20.0% reimplemented"
    assert lines.subtitle == "1 of 5 functions of the original's own code, one square each, in address order"
    assert lines.legend == [
        (progress_grid.HOME_LABELS["reimplemented"], "1", "reimplemented"),
        (progress_grid.HOME_LABELS["understood"], "1", "understood"),
        (progress_grid.HOME_LABELS["not-started"], "3", "not-started"),
    ]


# --- the file ---------------------------------------------------------------------------------------------------------


def test_the_png_is_deterministic_and_a_png(tmp_path: Path) -> None:
    """The same data gives the same bytes (the site is rebuilt on every push), and it is a real PNG."""
    grid = grid_of(make_grid_checkout(tmp_path))
    first = progress_png.render_home_png(grid, "dark")
    assert first == progress_png.render_home_png(grid, "dark")
    assert first.startswith(b"\x89PNG\r\n\x1a\n")
    assert first != progress_png.render_home_png(grid, "light")


def test_an_unknown_theme_is_refused(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="theme"):
        progress_png.render_home_png(grid_of(make_grid_checkout(tmp_path)), "sepia")


def test_the_site_publishes_both_pngs_beside_the_other_pictures(tmp_path: Path) -> None:
    root = make_grid_checkout(tmp_path)
    site = progress_site.build_site(root, progress.load(root), head=("2026-10-26", "abc1234"))
    assert set(progress_png.FILES) == {"light", "dark"}
    for theme, name in progress_png.FILES.items():
        data = site.files[f"{progress_site.SITE_DIR}/{name}"]
        assert isinstance(data, bytes)
        assert data == progress_png.render_home_png(grid_of(root), theme)


def test_progress_render_writes_a_binary_file_as_bytes_and_a_text_file_as_text(tmp_path: Path) -> None:
    progress_cli.write_site_file(tmp_path / "a" / "x.png", b"\x89PNG\r\n")
    progress_cli.write_site_file(tmp_path / "a" / "y.json", '{"é":1}\n')
    assert (tmp_path / "a" / "x.png").read_bytes() == b"\x89PNG\r\n"
    assert (tmp_path / "a" / "y.json").read_text(encoding="utf-8") == '{"é":1}\n'


# --- the README and the build -----------------------------------------------------------------------------------------


def test_the_readme_shows_the_sites_pngs_for_each_theme_and_links_the_home_page() -> None:
    """The README is the one file that holds the site's address; it embeds the files the build publishes."""
    readme = (REPO / "README.md").read_text(encoding="utf-8")
    site = re.search(r"https://[\w.-]+/coney/", readme)
    assert site is not None
    base = site.group(0)
    start = readme.index("<picture>")
    picture = readme[start : readme.index("</picture>")]
    assert f'srcset="{base}{progress_site.SITE_DIR}/{progress_png.FILES["dark"]}"' in picture
    assert 'media="(prefers-color-scheme: dark)"' in picture
    assert f'src="{base}{progress_site.SITE_DIR}/{progress_png.FILES["light"]}"' in picture
    assert f'<a href="{base}">' in readme[max(0, start - 200) : start]
    # An HTML block (it starts with `<p>`, as Next.js's and uv's READMEs do), not inline HTML inside a paragraph:
    # GitHub's renderer emptied a `<picture>` written inline.
    assert readme[:start].endswith(f'\n<p>\n  <a href="{base}">\n    ')
    assert f'width="{progress_png.WIDTH // progress_png.SCALE}"' in picture
    assert "alt=" in picture


def test_the_docs_build_installs_the_pillow_the_tools_are_locked_to() -> None:
    """The docs job installs only requirements-docs.txt: it pins Pillow to the version uv.lock has, so no drift."""
    pin = re.search(r"^pillow==([\d.]+)$", (REPO / "requirements-docs.txt").read_text(encoding="utf-8"), re.MULTILINE)
    assert pin is not None
    lock = tomllib.loads((REPO / "python" / "uv.lock").read_text(encoding="utf-8"))
    versions = {p["version"] for p in lock["package"] if p["name"] == "pillow"}
    assert versions == {pin.group(1)}


def test_the_real_pictures_stay_light() -> None:
    """Every function is drawn and each PNG is well under what a README should load (60 KB)."""
    state = progress.load(REPO)
    grid = progress_grid.build_grid(state, REPO)
    assert grid is not None
    for theme in progress_png.FILES:
        data = progress_png.render_home_png(grid, theme)
        assert len(data) < 60_000, (theme, len(data))
        assert decode(data).height == progress_png.height_of(grid.total()[0])
