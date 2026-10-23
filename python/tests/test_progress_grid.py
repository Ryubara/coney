# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the progress picture: states, blocks, layout, the SVG, the numbers and the page it goes on.

The inputs are the made-up checkout of test_progress (two subsystems, one function listed) with a Ghidra listing,
research pages, a source map and missions added; one test builds the picture of the real checkout to hold the page's
size.
"""

from __future__ import annotations

import json
import math
import re
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest
from test_progress import entry, make_checkout

from coney_tools import missions, progress, progress_grid, progress_picture, progress_render, progress_site

REPO = Path(__file__).resolve().parents[2]

# Alpha holds three functions (reimplemented, named but uncited, understood), Beta one (named, uncited), and one lies
# in no subsystem (unnamed, cited): states reimplemented, researched, understood, researched and untouched.
LISTING = (
    "0x00001000\t256\tAlpha_Init\t1\n"
    "0x00001100\t64\tAlpha_Step\t0\n"
    "0x00001180\t100\tAlpha_Done\t1\n"
    "0x00001200\t128\tBeta_Run\t0\n"
    "0x00001500\t32\t\t0\n"
    "0x00001c00\t512\tlib_thing\t0\n"
)
FUNCTIONS = entry(0x1000, "Alpha_Init", "src/core/thing.h:alphaInit")
TAG = "/// Starts it.\n/// @orig 0x00001000 Alpha_Init (Alpha.cpp)\nvoid alphaInit();\n"
PAGE = """# Alpha

| Address | Name | Evidence |
| --- | --- | --- |
| `0x00001000` | `Alpha_Init` | confirmed (code) |
| `0x00001100` | `Alpha_Step` | |
| `0x00001180` | `Alpha_Done` | inferred |

The helper at `0x00001500` is dead code: not needed: never called.
"""
SOURCE_FILES = """[[file]]
directory = "Alpha"
path = "Alpha/Alpha.cpp"
start = 0x00001000
end = 0x00001100
functions = 1

[[file]]
directory = "Alpha"
path = "Alpha/Gone.cpp"
path_string = 0x00546ec0
functions = 0

[[file]]
directory = "Device/ps2"
path = "Device/ps2/fileio/Wad.cpp"
start = 0x00001100
end = 0x00001200
functions = 2
"""


def make_grid_checkout(root: Path) -> Path:
    """The test_progress checkout with the listing, research page, source map and a tag for Alpha_Init."""
    make_checkout(root, FUNCTIONS, TAG, LISTING)
    (root / "docs" / "research").mkdir(parents=True)
    (root / "docs" / "research" / "alpha.md").write_text(PAGE, encoding="utf-8")
    (root / "research").mkdir(exist_ok=True)
    (root / "research" / "source-files.toml").write_text(SOURCE_FILES, encoding="utf-8")
    return root


def grid_of(root: Path, audited: tuple[int, ...] = ()) -> progress_grid.Grid:
    """The picture's model for a checkout."""
    grid = progress_grid.build_grid(progress.load(root), root, audited)
    assert grid is not None
    return grid


def states_of(grid: progress_grid.Grid) -> dict[int, str]:
    """Each drawn function's state by address."""
    return {f.address: f.state for f in grid.functions}


# --- the model --------------------------------------------------------------------------------------------------------


def test_each_function_shows_the_furthest_state(tmp_path: Path) -> None:
    """Reimplemented beats understood beats researched (placed by the source map) beats untouched."""
    grid = grid_of(make_grid_checkout(tmp_path))
    assert states_of(grid) == {
        0x1000: "reimplemented",
        0x1100: "researched",  # named, cited without an evidence level, in Alpha's range
        0x1180: "understood",
        0x1200: "researched",  # in Beta's range
        0x1500: "untouched",  # no subsystem, unnamed
    }  # the middleware function is not game code and is not drawn


def test_the_audit_hook_adds_the_audited_state_and_nothing_else(tmp_path: Path) -> None:
    """`load_audited` finds no audit yet; handing addresses to the builder gives them the top state."""
    root = make_grid_checkout(tmp_path)
    assert progress_grid.load_audited(root) == frozenset()
    plain = grid_of(root)
    assert not plain.audited_known
    audited = grid_of(root, (0x1000, 0x1180))
    assert audited.audited_known
    assert states_of(audited)[0x1000] == "audited" and states_of(audited)[0x1180] == "audited"
    assert audited.measure("reimplemented")[0] == 2  # an audited function is also reimplemented


def test_measures_follow_the_trackers_definitions(tmp_path: Path) -> None:
    """Understood and researched are cumulative over states, exactly as the tracker counts them."""
    root = make_grid_checkout(tmp_path)
    state = progress.load(root)
    grid = grid_of(root)
    assert state.understanding is not None
    assert grid.total() == (state.understanding.functions, state.understanding.bytes)
    assert grid.measure("understood") == (
        state.understanding.understood_functions,
        state.understanding.understood_bytes,
    )
    assert grid.measure("reimplemented") == (1, 256)
    assert grid.measure("researched")[0] == 4  # every function in a subsystem but the unattributed one


def test_blocks_are_subsystems_in_address_order_unattributed_last(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path))
    assert [b.name for b in grid.blocks] == ["Alpha", "Beta", "unattributed"]
    assert [b.label for b in grid.blocks][-1] == "Unattributed"
    assert [len(b.functions) for b in grid.blocks] == [3, 1, 1]


def test_reimplemented_functions_missing_from_the_listing_are_counted_not_drawn(tmp_path: Path) -> None:
    extra = FUNCTIONS + entry(0x1040, "Inner", "src/core/thing.h:inner")
    root = make_checkout(tmp_path, extra, TAG + "/// @orig 0x00001040 Inner (Alpha.cpp)\nvoid inner();\n")
    (root / "docs" / "progress" / "ghidra-functions.tsv").write_text(LISTING, encoding="utf-8")
    grid = grid_of(root)
    assert grid.unlisted_reimplemented == 1
    assert 0x1040 not in states_of(grid)


def test_no_listing_means_no_picture(tmp_path: Path) -> None:
    root = make_checkout(tmp_path, FUNCTIONS, TAG)  # no listing file
    assert progress_grid.build_grid(progress.load(root), root) is None


def test_source_files_and_the_coney_line_come_from_the_data_and_the_tags(tmp_path: Path) -> None:
    """The file ranges place functions by address; a file without a range places none."""
    functions = {f.address: f for f in grid_of(make_grid_checkout(tmp_path)).functions}
    assert functions[0x1000].source_file == "Alpha/Alpha.cpp"
    assert functions[0x1100].source_file == "Device/ps2/fileio/Wad.cpp"
    assert functions[0x1200].source_file == ""  # the range ends before it
    assert functions[0x1000].coney == "src/core/thing.h:2"
    assert functions[0x1100].coney == ""


# --- the layout and the SVG -------------------------------------------------------------------------------------------


def test_layout_keeps_every_block_inside_the_width_and_apart() -> None:
    blocks = [
        progress_picture.Block(f"Block {i}", "t", ["x"] * n)
        for i, n in enumerate([3000, 5, 400, 1, 90, 90, 700, 20, 0])
    ]
    placements, height = progress_picture.layout(blocks)
    assert placements[-1] is None  # an empty block draws nothing
    boxes = []
    for place in placements:
        if place is None:
            continue
        assert place.x >= 0 and place.x + place.cols * progress_picture.PITCH <= progress_picture.WIDTH
        assert place.y + place.rows * progress_picture.PITCH <= height
        boxes.append((place.x, place.y, place.x + place.cols * progress_picture.PITCH, place.y + place.rows * 6))
    for i, a in enumerate(boxes):
        for b in boxes[i + 1 :]:
            assert a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1], (a, b)


def test_a_block_alone_on_its_shelf_spans_the_width() -> None:
    placements, _ = progress_picture.layout([progress_picture.Block("Big", "t", ["x"] * 5000)])
    assert placements[0] is not None and placements[0].cols == progress_picture.MAX_COLS


def square_count(svg: str) -> int:
    """Squares drawn: every one is the same short subpath."""
    return svg.count(f"h{progress_picture.CELL}v{progress_picture.CELL}h-{progress_picture.CELL}z")


def test_the_svg_has_one_square_per_function_and_a_title_per_block(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path))
    svg = progress_grid.render_functions_svg(grid, False, uid="t")
    assert square_count(svg) == 5
    root = ET.fromstring(svg.replace("<svg ", '<svg xmlns="http://www.w3.org/2000/svg" ', 1))
    titles = [t.text for t in root.iter("{http://www.w3.org/2000/svg}title")]
    assert "Alpha: 1/3 reimplemented (33.3%)\nReimplemented 1 · Understood 1 · Researched 1" in titles
    assert "\n\n" not in svg  # Markdown ends an HTML block at a blank line
    assert 'class="cg-s-reimplemented"' in svg and "style" not in svg  # the page's stylesheet owns the colours


def test_the_picture_by_size_has_a_square_per_512_bytes_and_at_least_one_per_function(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path))
    expected = sum(max(1, math.ceil(f.size / progress_grid.BYTES_PER_SQUARE)) for f in grid.functions)
    assert expected == 5  # every function here is under one square
    big = progress_grid.GridFunction(0x2000, 2000, "Big", "Alpha", "understood", True, True)
    assert progress_grid.square_count(big.size) == 4 and progress_grid.square_count(1) == 1
    svg = progress_grid.render_functions_svg(grid, True, uid="s")
    assert square_count(svg) == expected and 'data-view="size"' in svg


def test_the_standalone_file_carries_both_themes() -> None:
    blocks = [progress_picture.Block("A", "t", ["understood", "untouched"])]
    svg = progress_picture.render_svg(
        blocks,
        uid="x",
        title="t",
        description="d",
        classes=progress_grid.CLASSES,
        palette=progress_grid.PALETTE,
        standalone=True,
    )
    ET.fromstring(svg)  # well-formed on its own
    assert "prefers-color-scheme:dark" in svg and 'xmlns="http://www.w3.org/2000/svg"' in svg
    assert progress_grid.PALETTE["understood"][0] in svg


def test_squares_are_where_the_data_attributes_say() -> None:
    """The script finds a square from the block's origin and columns, so the path must put it exactly there."""
    block = progress_picture.Block("A", "t", ["understood"] * 7)
    svg = progress_picture.render_svg(
        [block], uid="x", title="t", description="d", classes=progress_grid.CLASSES, palette=progress_grid.PALETTE
    )
    attrs = dict(re.findall(r'data-(\w)="(\d+)"', svg))
    x, y, cols = int(attrs["x"]), int(attrs["y"]), int(attrs["c"])
    d = re.search(r' d="([^"]+)"', svg)
    assert d is not None
    position = [0, 0]
    corners = []
    for command, a, b in re.findall(r"([Mm])(-?\d+) (-?\d+)h5v5h-5z", d.group(1)):
        position = [int(a), int(b)] if command == "M" else [position[0] + int(a), position[1] + int(b)]
        corners.append(tuple(position))
    assert corners == [
        (x + (i % cols) * progress_picture.PITCH, y + (i // cols) * progress_picture.PITCH) for i in range(7)
    ]


# --- the numbers ------------------------------------------------------------------------------------------------------


def test_summary_carries_the_trackers_numbers(tmp_path: Path) -> None:
    root = make_grid_checkout(tmp_path)
    grid = grid_of(root)
    data = progress_grid.summary(grid)
    assert data["functions"] == 5 and data["bytes"] == 256 + 64 + 100 + 128 + 32
    assert data["states"] == {"audited": 0, "reimplemented": 1, "understood": 1, "researched": 2, "untouched": 1}
    assert data["measures"]["reimplemented"]["functions"] == 1
    assert data["measures"]["understood"]["functions"] == 2  # Alpha_Init is understood as well
    alpha = data["blocks"][0]
    assert (
        alpha["name"] == "Alpha" and alpha["functions"] == 3 and alpha["measures"]["reimplemented"]["percent"] == 33.333
    )
    json.dumps(data)  # plain data


def test_the_function_table_is_compact_and_round_trips(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path))
    table = progress_grid.functions_table(grid)
    block = table["blocks"][0]
    assert block["name"] == "Alpha" and block["s"] == "rpu"
    addresses = []
    total = 0
    for step in block["a"]:
        total += step
        addresses.append(total)
    assert addresses == [0x1000, 0x1100, 0x1180] and block["z"] == [256, 64, 100]
    assert block["n"] == ["Alpha_Init", "Alpha_Step", "Alpha_Done"]
    assert table["coney"][block["c"]["0"]] == "src/core/thing.h:2" and "1" not in block["c"]
    assert table["files"][block["f"][0]] == "Alpha/Alpha.cpp"
    assert block["f"][1] == block["f"][2] and table["files"][block["f"][2]] == "Device/ps2/fileio/Wad.cpp"
    assert table["blocks"][1]["f"] == [-1]  # Beta_Run lies in no file's range
    assert table["unit"] == progress_grid.BYTES_PER_SQUARE


# --- missions ---------------------------------------------------------------------------------------------------------


def mission(level: int, label: str, status: str, *checkpoints: missions.Checkpoint) -> missions.Mission:
    """A made-up mission with the given checkpoints."""
    return missions.Mission(level, "story", "s", label, "", "x", "research", status, checkpoints)


def sample_missions() -> missions.MissionList:
    """Two made-up missions: one approved checkpoint of two, and one not started."""
    cp = missions.Checkpoint
    first = mission(99, "Mission 1", "in-progress", cp(1, "approved"), cp(2, "in-progress"))
    second = mission(80, "Mission 2", "not-started", cp(1, "not-started"))
    return missions.MissionList("", "", (first, second))


def test_the_mission_picture_has_a_square_per_checkpoint_and_links_to_the_pages() -> None:
    mission_list = sample_missions()
    svg = progress_site.render_missions_svg(mission_list, link=True)
    assert square_count(svg) == 3
    assert 'href="../missions/level99/"' in svg and "Mission 1: 1/2 checkpoints approved" in svg
    assert "href" not in progress_site.render_missions_svg(mission_list, link=False, standalone=True)
    # Missions that share a label are told apart by their slot.
    twin = mission(61, "Armies", "not-started", missions.Checkpoint(1, "not-started"))
    twin2 = mission(62, "Armies", "not-started", missions.Checkpoint(1, "not-started"))
    twins = missions.MissionList("", "", (twin, twin2))
    assert [b.label for b in progress_site.mission_blocks(twins, False)] == ["s", "s"]
    section = progress_site.missions_section(mission_list)
    assert "1 of 3 checkpoints built</strong>, 1 approved (2 missions)" in section  # approved counts as built


# --- the site ---------------------------------------------------------------------------------------------------------


def test_the_site_holds_the_pictures_numbers_and_history(tmp_path: Path) -> None:
    root = make_grid_checkout(tmp_path)
    site = progress_site.build_site(
        root, progress.load(root), repo_url="https://example.invalid/r", head=("2026-10-21", "abc1234")
    )
    assert sorted(site.files) == [
        "progress/functions.json",
        "progress/history.json",
        "progress/history.svg",
        "progress/progress-size.svg",
        "progress/progress.json",
        "progress/progress.svg",
    ]
    assert (
        'data-repo="https://example.invalid/r"' in site.grid_html
        and 'data-functions="functions.json"' in site.grid_html
    )
    assert "One square per function" in site.grid_html and "audited" not in site.grid_html.lower()
    assert json.loads(site.files["progress/progress.json"])["functions"] == 5
    history = json.loads(site.files["progress/history.json"])
    assert history["points"][-1]["date"] == "2026-10-21" and history["points"][-1]["commit"] == "abc1234"


def test_the_headline_and_legend_show_audited_only_with_an_audit(tmp_path: Path) -> None:
    grid = grid_of(make_grid_checkout(tmp_path), (0x1000,))
    html = progress_site.grid_section(grid, None)
    assert "Audited" in html and "data-repo" not in html
    assert "Audited" not in progress_site.grid_section(grid_of(make_grid_checkout(tmp_path / "again")), None)


def test_the_page_gets_the_pictures_after_the_summary(tmp_path: Path) -> None:
    root = make_grid_checkout(tmp_path)
    state = progress.load(root)
    page = progress_render.render_page(state, "<div>GRID</div>", "<div>MISSIONS</div>")
    assert page.index("## Overall") < page.index("GRID") < page.index("## Milestones") < page.index("MISSIONS")
    assert "GRID" not in progress_render.render_page(state) and "## Missions" not in progress_render.render_page(state)


def test_history_page_needs_exactly_one_marker() -> None:
    text = f"# History\n\n{progress_render.HISTORY_MARKER}\n"
    assert "CHART" in progress_render.fill_history_page(text, "CHART", "h.md")
    with pytest.raises(progress.ProgressError, match=r"h\.md: needs exactly one <!-- progress:history -->"):
        progress_render.fill_history_page("no marker", "CHART", "h.md")


# --- the real checkout ------------------------------------------------------------------------------------------------


def test_the_real_pictures_stay_lean() -> None:
    """Every function of the original's own code is drawn, and the page's weight stays small.

    The inline page holds both pictures; the per-function table is fetched on the first hover. The limits are the
    brief's (a page under about 1.5 MB, the compared project's picture is 1.1 MB for 10,000 squares).
    """
    state = progress.load(REPO)
    grid = progress_grid.build_grid(state, REPO)
    assert grid is not None and state.understanding is not None
    assert grid.total()[0] == state.understanding.functions
    page = progress_site.grid_section(grid, None)
    table = json.dumps(progress_grid.functions_table(grid), separators=(",", ":"))
    assert len(page.encode()) < 600_000, len(page)
    assert len(table.encode()) < 900_000, len(table)
    assert square_count(page) >= grid.total()[0]


def test_the_stylesheet_uses_the_palettes_of_the_standalone_files() -> None:
    """The page's colours (progress-grid.css) and the published SVGs' (the Python palettes) are the same, per theme."""
    css = (REPO / "docs" / "stylesheets" / "progress-grid.css").read_text(encoding="utf-8")
    light = css[css.index(".progress-grid {") :].split("}", 1)[0]
    dark = css[css.index('[data-md-color-scheme="slate"] .progress-grid {') :].split("}", 1)[0]

    def value(block: str, name: str) -> str | None:
        found = re.search(rf"--cg-{name}:\s*(#[0-9a-fA-F]{{6}});", block)
        return found.group(1).lower() if found else None

    for state, (day, night) in progress_grid.PALETTE.items():
        assert value(light, state) == day.lower(), state
        assert value(dark, state) == night.lower(), state
    for status, (day, night) in progress_site.MISSION_PALETTE.items():
        assert value(light, f"m-{status}") == day.lower(), status
        assert (value(dark, f"m-{status}") or day.lower()) == night.lower(), status  # unset in dark: the light one
