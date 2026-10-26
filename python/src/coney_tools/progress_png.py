# SPDX-License-Identifier: GPL-3.0-or-later
"""The README's progress picture: the docs home page's squares as a PNG, once for each colour theme.

GitHub shows the README but cannot run the docs site's script or read its stylesheet, and `main` takes no direct
pushes, so a picture committed there would go stale. The docs build therefore draws it (`progress_site.build_site`
adds `FILES` to the site, beside the SVG pictures) and the README embeds it by the site's address, with GitHub's
`<picture>` choosing the dark file for a dark page. The squares, their order, their three states and their colours are
the home page's (`HOME_STATE_OF`, `HOME_PALETTE`, `HOME_LABELS`): nothing here defines a colour or a
name. Only the layout is its own, because a README column is narrower than the home page's: `COLS` squares to a row,
drawn `SCALE` times larger than they are shown (880 px wide) so a high-density screen stays sharp.

The words (headline, counts, legend) are in the picture, because a README cannot update text from the build; Pillow
draws them with its bundled font, so the output is the same on every machine. The PNG holds no date or other
varying metadata: the same data is the same bytes.

Docs: docs/guides/coney-tools-references.md (progress)
"""

from __future__ import annotations

import io
import math
from dataclasses import dataclass

from PIL import Image, ImageDraw, ImageFont

from coney_tools import progress_grid, progress_render

#: What the picture is called in the site (under `progress_site.SITE_DIR`), by theme.
FILES = {"light": "readme-light.png", "dark": "readme-dark.png"}

#: Squares in a row, the side of a square and the distance between two squares' corners, in pixels as shown.
COLS = 170
SQUARE = 4
PITCH = 5
#: Edge of the picture around the squares, as shown.
MARGIN = 15
#: The picture is drawn this many times larger than it is shown, and the README's width attribute scales it back.
SCALE = 2
#: Width of the file in pixels (880 as shown).
WIDTH = (2 * MARGIN + COLS * PITCH) * SCALE
#: Top of the first row of squares: room above for the headline and its second line.
CHART_TOP = 72 * SCALE
#: Sizes of the text, as shown.
TITLE_SIZE = 24
LINE_SIZE = 13
LEGEND_SIZE = 14
#: Gap from the squares to the legend, a legend line's height and the room below it, as shown.
LEGEND_GAP = 14
LEGEND_HEIGHT = 16
BOTTOM = 15
#: Distance from a legend entry to the next one, as shown.
LEGEND_SPACING = 28


@dataclass(frozen=True)
class Theme:
    """The colours around the squares: the page they sit on and the two greys of text, as `#rrggbb`."""

    background: str
    text: str
    muted: str


#: GitHub's page and text colours in each theme; the squares' own colours are `progress_grid.HOME_PALETTE`'s.
THEMES = {
    "light": Theme("#ffffff", "#1f2328", "#59636e"),
    "dark": Theme("#0d1117", "#e6edf3", "#9198a1"),
}


@dataclass(frozen=True)
class TextLines:
    """What the picture says: the headline, the line under it and the legend as (name, count, home state)."""

    title: str
    subtitle: str
    legend: list[tuple[str, str, str]]


def home_cells(grid: progress_grid.Grid) -> list[str]:
    """The home state of each function in the home picture's order (blocks in address order, flowing on)."""
    return [
        progress_grid.HOME_STATE_OF[state]
        for block in progress_grid.picture_blocks(grid, False)
        for state in block.cells
    ]


def text_lines(grid: progress_grid.Grid) -> TextLines:
    """The words of the picture, from the same numbers and names as the home page's headline and legend."""
    total, _ = grid.total()
    done, _ = grid.measure("reimplemented")
    counts = progress_grid.home_counts(grid)
    return TextLines(
        title=f"Coney: {progress_render.percent(done, total)} reimplemented",
        subtitle=f"{done:,} of {total:,} functions of the original's own code, one square each, in address order",
        legend=[(progress_grid.HOME_LABELS[s], f"{counts[s]:,}", s) for s in progress_grid.HOME_STATES],
    )


def _rows(count: int) -> int:
    """Rows of squares for `count` functions (one at least, so an empty picture still has its words)."""
    return max(1, math.ceil(count / COLS))


def chart_bottom(count: int) -> int:
    """The pixel row just below the last row of squares of a picture of `count` functions."""
    return CHART_TOP + _rows(count) * PITCH * SCALE


def height_of(count: int) -> int:
    """The file's height in pixels for `count` functions: header, whole rows of squares, legend."""
    return chart_bottom(count) + (LEGEND_GAP + LEGEND_HEIGHT + BOTTOM) * SCALE


def square_origin(index: int) -> tuple[int, int]:
    """The pixel at the top left of square `index`, counting along the rows from the top left."""
    column, row = index % COLS, index // COLS
    return (MARGIN + column * PITCH) * SCALE, CHART_TOP + row * PITCH * SCALE


def square_centre(index: int) -> tuple[int, int]:
    """The pixel at the middle of square `index`."""
    x, y = square_origin(index)
    return x + SQUARE * SCALE // 2, y + SQUARE * SCALE // 2


def _rgb(colour: str) -> tuple[int, int, int]:
    """`#rrggbb` as the tuple Pillow draws with."""
    return (int(colour[1:3], 16), int(colour[3:5], 16), int(colour[5:7], 16))


def render_home_png(grid: progress_grid.Grid, theme: str) -> bytes:
    """The picture for `theme` ("light" or "dark") as PNG bytes: words above, the squares, the legend below."""
    if theme not in THEMES:
        raise ValueError(f"theme must be one of {', '.join(THEMES)}, not {theme!r}")
    colours = THEMES[theme]
    palette = 0 if theme == "light" else 1
    cells = home_cells(grid)
    lines = text_lines(grid)
    image = Image.new("RGB", (WIDTH, height_of(len(cells))), _rgb(colours.background))
    draw = ImageDraw.Draw(image)

    draw.text(
        (MARGIN * SCALE, 14 * SCALE),
        lines.title,
        fill=_rgb(colours.text),
        font=ImageFont.load_default(TITLE_SIZE * SCALE),
    )
    draw.text(
        (MARGIN * SCALE, 46 * SCALE),
        lines.subtitle,
        fill=_rgb(colours.muted),
        font=ImageFont.load_default(LINE_SIZE * SCALE),
    )

    # One rectangle per square, in the colour of its home state.
    fills = {state: _rgb(pair[palette]) for state, pair in progress_grid.HOME_PALETTE.items()}
    side = SQUARE * SCALE
    for index, state in enumerate(cells):
        x, y = square_origin(index)
        draw.rectangle((x, y, x + side - 1, y + side - 1), fill=fills[state])

    # The legend: a swatch, the state's name and its count, left to right.
    font = ImageFont.load_default(LEGEND_SIZE * SCALE)
    y = chart_bottom(len(cells)) + LEGEND_GAP * SCALE
    x = MARGIN * SCALE
    swatch = LEGEND_HEIGHT * SCALE * 3 // 4
    for name, count, state in lines.legend:
        draw.rectangle((x, y + 1, x + swatch - 1, y + swatch), fill=fills[state])
        x += swatch + 6 * SCALE
        label = f"{name} {count}"
        draw.text((x, y), label, fill=_rgb(colours.text), font=font)
        x += round(draw.textlength(label, font=font)) + LEGEND_SPACING * SCALE

    out = io.BytesIO()
    image.save(out, format="PNG", optimize=True)
    return out.getvalue()
