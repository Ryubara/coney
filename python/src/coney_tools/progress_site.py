# SPDX-License-Identifier: GPL-3.0-or-later
"""What the docs build publishes for the progress pictures: the page's HTML sections and the files of the site.

`build_site` is the one call the MkDocs hook makes (mkdocs_hooks.py). It returns the text that fills the progress
page's marker (the function picture, the mission picture), the history page's block, and the files to add to the
site: the standalone SVGs (`progress/progress.svg`, `progress-size.svg`, `missions.svg`, `history.svg`), the raw
numbers (`progress/progress.json`), the per-function table the page's script reads for tooltips
(`progress/functions.json`) and the history (`progress/history.json`). Nothing is committed.

The page's SVGs are inline, so the stylesheet's colours (light and dark) and the script's tooltips reach them. Where
a picture can say more than a table it still has one: the tables above and below it carry the same numbers, so the
picture is never the only way to read them.
"""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from html import escape
from pathlib import Path

from coney_tools import missions, progress_grid, progress_history, progress_picture, progress_render
from coney_tools.config import ConfigError
from coney_tools.progress import Progress

#: Fill of each mission and checkpoint status, light and dark (the same in both: validated on both surfaces).
MISSION_PALETTE = {
    "not-started": ("#6e7681", "#6e7681"),
    "in-progress": ("#b8860b", "#b8860b"),
    "pending-approval": ("#3b82f6", "#3b82f6"),
    "needs-fixes": ("#c2185b", "#c2185b"),
    "approved": ("#2ea043", "#2ea043"),
}
MISSION_CLASSES = {status: f"cg-m-{status}" for status in MISSION_PALETTE}
#: Where each file sits in the built site, relative to its root.
SITE_DIR = "progress"


@dataclass
class Site:
    """The generated parts of the progress pages and the files that go into the site, by path."""

    grid_html: str = ""
    missions_html: str = ""
    history_html: str = ""
    files: dict[str, str] = field(default_factory=dict)


# --- the mission picture ----------------------------------------------------------------------------------------------


def mission_blocks(mission_list: missions.MissionList, link: bool) -> list[progress_picture.Block]:
    """One block per mission, one square per checkpoint coloured by its status; `link` makes the label a link."""
    blocks = []
    labels = [m.label for m in mission_list.missions]
    for m in mission_list.missions:
        n = len(m.checkpoints)
        # The five Armies of the Night stages share one label; their slot ("... 3") tells them apart.
        label = m.label if labels.count(m.label) == 1 else m.slot
        status = missions.STATUSES[m.status][1]
        title = f"{m.heading}: {m.approved()}/{n} checkpoints approved\n{status}; {m.built()}/{n} built"
        blocks.append(
            progress_picture.Block(
                label,
                title,
                [c.status for c in m.checkpoints],
                f"../missions/{m.name}/" if link else "",
            )
        )
    return blocks


def render_missions_svg(mission_list: missions.MissionList, *, link: bool, standalone: bool = False) -> str:
    """The missions' picture as SVG: each mission's checkpoints in the order of the checklist."""
    total = sum(len(m.checkpoints) for m in mission_list.missions)
    return progress_picture.render_svg(
        mission_blocks(mission_list, link),
        uid="pm",
        title="Coney's missions, one square per checkpoint",
        description=f"{len(mission_list.missions)} missions and {total} checkpoints, coloured by status.",
        classes=MISSION_CLASSES,
        palette=MISSION_PALETTE,
        standalone=standalone,
    )


def missions_section(mission_list: missions.MissionList) -> str:
    """The page's mission block: the legend with counts of checkpoints, then the inline SVG."""
    counts = {s: 0 for s in missions.STATUSES}
    for m in mission_list.missions:
        for c in m.checkpoints:
            counts[c.status] += 1
    total = sum(counts.values())
    items = "".join(
        f'<li><span class="cg-key {MISSION_CLASSES[s]}"></span><strong>{escape(missions.STATUSES[s][1])}</strong> '
        f"{counts[s]:,}</li>"
        for s in missions.STATUSES
    )
    approved = counts["approved"]
    built = sum(counts[s] for s in missions.BUILT)
    return "\n".join(
        [
            '<div class="progress-grid progress-missions">',
            f'<p class="cg-headline"><strong>{built:,} of {total:,} checkpoints built</strong>, {approved:,} approved '
            f"({len(mission_list.missions)} missions)</p>",
            f'<ul class="cg-legend cg-legend-inline">{items}</ul>',
            render_missions_svg(mission_list, link=True),
            "</div>",
        ]
    )


# --- the function picture ---------------------------------------------------------------------------------------------


def _legend(grid: progress_grid.Grid) -> str:
    """The legend: each state, how many squares are in it and what it means; audited only once there is an audit."""
    items = []
    for state in progress_grid.STATES:
        count = sum(1 for f in grid.functions if f.state == state)
        if state == "audited" and not grid.audited_known:
            continue
        meaning = escape(progress_grid.STATE_MEANINGS[state]).replace("`@orig`", "<code>@orig</code>")
        items.append(
            f'<li><span class="cg-key {progress_grid.CLASSES[state]}"></span>'
            f"<strong>{progress_grid.STATE_LABELS[state]}</strong> {count:,}: {meaning}</li>"
        )
    return f'<ul class="cg-legend">{"".join(items)}</ul>'


def _headline(grid: progress_grid.Grid) -> str:
    """The measures as one line: reimplemented in bold, then understood and researched, with function counts."""
    total, _ = grid.total()
    parts = []
    for kind in progress_grid.MEASURES:
        if kind == "audited" and not grid.audited_known:
            continue
        n, _ = grid.measure(kind)
        text = f"{progress_grid.STATE_LABELS[kind]} {progress_render.percent(n, total)} ({n:,})"
        parts.append(f"<strong>{text}</strong>" if kind == "reimplemented" else text)
    return f'<p class="cg-headline">{" · ".join(parts)} of {total:,} functions</p>'


def grid_section(grid: progress_grid.Grid, repo_url: str | None) -> str:
    """The page's function block: headline, a toggle between the two pictures, the legend and both inline SVGs.

    The toggle is two radio buttons and the stylesheet's sibling selectors, so it works without the script. The
    script only adds the per-square tooltips (it reads `data-functions`, the per-function table).
    """
    # The table sits beside the page's own file in the site, so its address is relative to the page.
    attrs = 'data-functions="functions.json"'
    if repo_url:
        attrs += f' data-repo="{escape(repo_url.rstrip("/"), quote=True)}"'
    return "\n".join(
        [
            f'<div class="progress-grid progress-functions" {attrs}>',
            _headline(grid),
            '<input type="radio" name="cg-view" id="cg-view-f" class="cg-radio" checked>',
            '<label for="cg-view-f" class="cg-tab">One square per function</label>',
            '<input type="radio" name="cg-view" id="cg-view-s" class="cg-radio">',
            f'<label for="cg-view-s" class="cg-tab">One square per {progress_grid.BYTES_PER_SQUARE} bytes</label>',
            '<div class="cg-pane cg-pane-f">',
            progress_grid.render_functions_svg(grid, False, uid="pf"),
            "</div>",
            '<div class="cg-pane cg-pane-s">',
            progress_grid.render_functions_svg(grid, True, uid="ps"),
            "</div>",
            _legend(grid),
            '<div class="cg-tip" role="status" aria-live="polite" hidden></div>',
            "</div>",
        ]
    )


# --- the whole site ---------------------------------------------------------------------------------------------------


def _json(data: object) -> str:
    """Compact JSON with a trailing newline."""
    return json.dumps(data, separators=(",", ":")) + "\n"


def build_site(
    root: Path,
    state: Progress,
    *,
    repo_url: str | None = None,
    head: tuple[str, str] | None = None,
    history_cache: str | None = None,
    reconstruct_history: bool = True,
) -> Site:
    """Everything the progress pages need from the checkout at `root` and the tracker's `state`.

    `head` is (UTC date, abbreviated commit) of the build, for today's history point (the date today and no commit when
    not given). `history_cache` is the text of a previously published history.json. `reconstruct_history` False
    keeps the history to the cache plus today (a quick local build).
    """
    site = Site()
    try:
        mission_list = missions.load(root)
    except ConfigError:
        mission_list = None
    audited = progress_grid.load_audited(root)
    grid = progress_grid.build_grid(state, root, audited)
    if grid is not None:
        site.grid_html = grid_section(grid, repo_url)
        site.files[f"{SITE_DIR}/progress.svg"] = progress_grid.render_functions_svg(
            grid, False, uid="pf", standalone=True
        )
        site.files[f"{SITE_DIR}/progress-size.svg"] = progress_grid.render_functions_svg(
            grid, True, uid="ps", standalone=True
        )
        summary = progress_grid.summary(grid)
        site.files[f"{SITE_DIR}/functions.json"] = _json(progress_grid.functions_table(grid))
    else:
        summary = {"schema": 1, "functions": 0}
    if mission_list is not None:
        site.missions_html = missions_section(mission_list)
        site.files[f"{SITE_DIR}/missions.svg"] = render_missions_svg(mission_list, link=False, standalone=True)
        summary["missions"] = mission_summary(mission_list)
    site.files[f"{SITE_DIR}/progress.json"] = json.dumps(summary, indent=1) + "\n"

    date, commit = head or (progress_history.utc_day(int(time.time())), "")
    current = progress_history.point_of(state, date, commit)
    cache, cache_skipped = progress_history.load_cache(history_cache) if history_cache else ([], [])
    if reconstruct_history:
        points, skipped = progress_history.build(root, current, cache, cache_skipped=cache_skipped)
    else:
        points = [p for p in cache if p.date < date] + [current]
        skipped = [s for s in cache_skipped if s[0] < date]
    site.history_html = progress_history.render_section(points, [d for d, _ in skipped])
    site.files[progress_history.HISTORY_FILE] = json.dumps(progress_history.to_json(points, skipped), indent=1) + "\n"
    site.files[f"{SITE_DIR}/history.svg"] = _standalone_chart(points)
    return site


def mission_summary(mission_list: missions.MissionList) -> dict[str, object]:
    """Counts of missions and checkpoints per status, for progress.json."""
    mission_counts = {s: 0 for s in missions.STATUSES}
    checkpoint_counts = {s: 0 for s in missions.STATUSES}
    for m in mission_list.missions:
        mission_counts[m.status] += 1
        for c in m.checkpoints:
            checkpoint_counts[c.status] += 1
    return {"missions": mission_counts, "checkpoints": checkpoint_counts}


def _standalone_chart(points: list[progress_history.Point]) -> str:
    """The history chart as a file of its own, with the colours of the picture and a dark-mode switch."""
    svg = progress_history.render_chart(points)
    style = (
        "<style>"
        ".cg-grid{stroke:#8b949e;stroke-opacity:.35}.cg-tick{fill:#57606a;font:11px system-ui,sans-serif}"
        + "".join(
            f".cg-line-{m}{{stroke:{c};fill:{c}}}"
            for m, c in (("reimplemented", "#2ea043"), ("understood", "#3b82f6"), ("researched", "#b8860b"))
        )
        + "polyline{fill:none;stroke-width:2}.cg-pt{stroke:none}"
        "@media (prefers-color-scheme:dark){.cg-tick{fill:#9da7b3}}"
        "</style>"
    )
    return svg.replace('class="cg-chart"', 'xmlns="http://www.w3.org/2000/svg" class="cg-chart"', 1).replace(
        "</title>", f"</title>{style}", 1
    )
