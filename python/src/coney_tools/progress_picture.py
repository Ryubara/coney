# SPDX-License-Identifier: GPL-3.0-or-later
"""The drawing half of the progress picture: blocks of small squares laid out and written as SVG.

Nothing here knows what a function or a mission is. A `Block` is a label, a tooltip and a list of cell states; `layout`
packs the blocks into shelves of a fixed width, and `render_svg` writes them as one SVG whose squares are a few `<path>`
elements per block (one per state), so about 14,000 squares come to a few hundred kilobytes. The SVG is plain static
markup: blocks carry a `<title>` (the browser's own tooltip), and the data attributes on each block let the page's
script (docs/javascripts/progress-grid.js) find the square under the pointer without a per-square element.

The colours are CSS classes (`cg-s-<state>`, `cg-m-<status>`) so one drawing serves both themes. The docs site defines
them in docs/stylesheets/extra.css; the standalone file (`standalone=True`) carries its own `<style>` with a
`prefers-color-scheme` switch, so it also works as an image.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass
from xml.sax.saxutils import escape, quoteattr

#: Side of one square and the distance between two squares' corners, in SVG user units.
CELL = 5
PITCH = 6
#: Squares in the widest row; the drawing is MAX_COLS * PITCH units wide and the page scales it to its column.
MAX_COLS = 96
WIDTH = MAX_COLS * PITCH
#: Room above each block for its label, the label's font size and the gaps between blocks and between shelves.
LABEL_HEIGHT = 17
FONT_SIZE = 11
BLOCK_GAP = 2  # in squares
SHELF_GAP = 9  # in units
#: Average glyph width in em, to size a block for its label without measuring text.
_GLYPH_EM = 0.6
#: Aspect of a block of squares: columns per row, so big blocks come out wider than tall like a paragraph of text.
_ASPECT = 2.2


@dataclass(frozen=True)
class Block:
    """One labelled block of squares: `cells` holds one state name per square, in drawing order."""

    label: str
    title: str
    cells: Sequence[str]
    href: str = ""


@dataclass(frozen=True)
class Placement:
    """Where a block sits: its squares start at (x, y) and run `cols` to a row; the label is above."""

    x: int
    y: int
    cols: int
    rows: int
    label_y: int


def label_cols(label: str) -> int:
    """How many squares wide a label is, so a block never draws its text over its neighbour's squares."""
    return math.ceil(len(label) * FONT_SIZE * _GLYPH_EM / PITCH)


def block_cols(cells: int) -> int:
    """The columns of a block of `cells` squares: roughly 2.2 times as wide as tall."""
    wanted = math.ceil(math.sqrt(cells * _ASPECT))
    return max(1, min(MAX_COLS, wanted, cells))


def layout(blocks: Sequence[Block]) -> tuple[list[Placement | None], int]:
    """Pack `blocks` in order into shelves; returns each block's placement (None for an empty one) and the height.

    Two passes. First blocks go on the current shelf at their preferred width while they fit, otherwise a new shelf is
    started. Then each shelf's blocks are widened in proportion until the shelf is exactly MAX_COLS wide, so the
    picture has no ragged right edge and a block alone on its shelf spans the whole width. Order is kept (the caller
    sorts), so the picture reads like a page of text.
    """
    preferred = [max(block_cols(len(b.cells)), label_cols(b.label)) if b.cells else 0 for b in blocks]
    shelves: list[list[int]] = []
    used = 0
    for i, block in enumerate(blocks):
        if not block.cells:
            continue
        if shelves and used + preferred[i] <= MAX_COLS:
            shelves[-1].append(i)
            used += preferred[i] + BLOCK_GAP
        else:
            shelves.append([i])
            used = preferred[i] + BLOCK_GAP
    placements: list[Placement | None] = [None] * len(blocks)
    top = 0
    for shelf in shelves:
        widths = _widen([preferred[i] for i in shelf], [len(blocks[i].cells) for i in shelf])
        x_cols = 0
        height = 0
        for i, width in zip(shelf, widths, strict=True):
            n = len(blocks[i].cells)
            cols = min(width, n)
            rows = math.ceil(n / cols)
            placements[i] = Placement(x_cols * PITCH, top + LABEL_HEIGHT, cols, rows, top + FONT_SIZE)
            height = max(height, LABEL_HEIGHT + rows * PITCH)
            x_cols += width + BLOCK_GAP
        top += height + SHELF_GAP
    return placements, max(top - SHELF_GAP, 0)


def _widen(preferred: list[int], cells: list[int]) -> list[int]:
    """`preferred` widths grown in proportion to fill MAX_COLS between the gaps; leftover columns go to the biggest."""
    available = MAX_COLS - BLOCK_GAP * (len(preferred) - 1)
    scale = available / sum(preferred)
    widths = [max(p, math.floor(p * scale)) for p in preferred]
    order = sorted(range(len(widths)), key=lambda k: -cells[k])
    left = available - sum(widths)
    k = 0
    while left > 0:
        widths[order[k % len(order)]] += 1
        left -= 1
        k += 1
    return widths


def _path(indexes: Sequence[int], placement: Placement) -> str:
    """The `d` of one path drawing the squares at `indexes`, each as a relative `h5v5h-5z` after a move.

    After `z` the current point is the square's own corner, so the next move is relative to that corner.
    """
    parts: list[str] = []
    px = py = 0
    for k, i in enumerate(indexes):
        x = placement.x + (i % placement.cols) * PITCH
        y = placement.y + (i // placement.cols) * PITCH
        move = f"M{x} {y}" if k == 0 else f"m{x - px} {y - py}"
        parts.append(f"{move}h{CELL}v{CELL}h-{CELL}z")
        px, py = x, y
    return "".join(parts)


def render_svg(
    blocks: Sequence[Block],
    *,
    uid: str,
    title: str,
    description: str,
    classes: dict[str, str],
    palette: dict[str, tuple[str, str]],
    standalone: bool = False,
    data_view: str = "",
) -> str:
    """The SVG of `blocks`; `classes` maps a state to its CSS class; `uid` keeps ids unique when several share a page.

    `palette` is each class's (light, dark) fill, written into the `<style>` of a standalone file only: on the docs
    page the stylesheet owns the colours. The result has no blank line, so Markdown keeps it as one HTML block.
    """
    placements, height = layout(blocks)
    height = max(height, 1)
    root_attrs = (
        f'viewBox="0 0 {WIDTH} {height}" role="img" aria-labelledby="{uid}-t {uid}-d" class="cg" data-pitch="{PITCH}"'
    )
    if data_view:
        root_attrs += f' data-view="{escape(data_view)}"'
    if standalone:
        root_attrs = f'xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" ' + root_attrs
    out = [f"<svg {root_attrs}>"]
    if standalone:
        out.append(_standalone_style(classes, palette))
    out.append(f'<title id="{uid}-t">{escape(title)}</title><desc id="{uid}-d">{escape(description)}</desc>')
    for index, (block, place) in enumerate(zip(blocks, placements, strict=True)):
        if place is None:
            continue
        out.append(_block_svg(index, block, place, classes))
    out.append("</svg>")
    return "\n".join(out)


def _block_svg(index: int, block: Block, place: Placement, classes: dict[str, str]) -> str:
    """One block: its group with the tooltip, the label and one path per state present."""
    by_state: dict[str, list[int]] = {}
    for i, state in enumerate(block.cells):
        by_state.setdefault(state, []).append(i)
    group = (
        f'<g class="cg-b" data-b="{index}" data-x="{place.x}" data-y="{place.y}" data-c="{place.cols}" '
        f'data-n="{len(block.cells)}">'
    )
    text = f'<text class="cg-l" x="{place.x}" y="{place.label_y}" font-size="{FONT_SIZE}">{escape(block.label)}</text>'
    if block.href:
        text = f"<a href={quoteattr(block.href)}>{text}</a>"
    paths = "".join(
        f'<path class="{classes[state]}" d="{_path(indexes, place)}"/>'
        for state, indexes in by_state.items()
        if state in classes
    )
    return f"{group}<title>{escape(block.title)}</title>{text}{paths}</g>"


def _standalone_style(classes: dict[str, str], palette: dict[str, tuple[str, str]]) -> str:
    """The `<style>` of a standalone file: light colours, dark ones under `prefers-color-scheme: dark`."""
    light = "".join(f".{cls}{{fill:{palette[state][0]}}}" for state, cls in classes.items() if state in palette)
    dark = "".join(f".{cls}{{fill:{palette[state][1]}}}" for state, cls in classes.items() if state in palette)
    return (
        "<style>"
        ".cg-l{fill:#24292f;font-family:system-ui,-apple-system,'Segoe UI',sans-serif}"
        f"{light}"
        "@media (prefers-color-scheme:dark){"
        f".cg-l{{fill:#e6edf3}}{dark}"
        "}</style>"
    )
