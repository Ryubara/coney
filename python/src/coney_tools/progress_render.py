# SPDX-License-Identifier: GPL-3.0-or-later
"""What the progress tracker publishes: the docs page's generated part and the README's badge data.

Nothing here is committed. The MkDocs build (mkdocs_hooks.py) fills PAGE_MARKER in docs/progress/index.md with
`render_page` and writes the endpoint JSON of `badge_files` into the site, so the published numbers are always
those of `main` and no pull request has to carry them. Bars are Unicode block characters in Markdown tables, so they
render the same everywhere without any CSS.
"""

from __future__ import annotations

import json
from urllib.parse import quote

from coney_tools.progress import UNATTRIBUTED, Progress, ProgressError

#: Where docs/progress/index.md says "the generated tables go here".
PAGE_MARKER = "<!-- progress:generated -->"
#: Where docs/progress/history.md says "the chart and the table of days go here".
HISTORY_MARKER = "<!-- progress:history -->"
#: Where docs/index.md, the home page, says "the compact picture of functions goes here".
HOME_MARKER = "<!-- progress:home -->"
GENERATED_NOTE = (
    "<!-- Generated when the docs are built, from docs/progress/totals.toml, the function database\n"
    "     (research/functions/), the @orig tags in src/ and the roadmap's status table. Edit those, not this. -->"
)
#: Where the badge JSON files sit in the built site, below its root.
BADGES_DIR = "progress/badges"
BAR_WIDTH = 20
_EIGHTHS = " ▏▎▍▌▋▊▉"
_FULL = "█"
_EMPTY = "░"


def bar(part: int, whole: int, width: int = BAR_WIDTH) -> str:
    """A bar of `width` cells, filled in eighths of a cell, as a code span so every cell has the same width."""
    fraction = min(max(part / whole, 0.0), 1.0) if whole > 0 else 0.0
    eighths = round(fraction * width * 8)
    if part > 0 and whole > 0 and eighths == 0:
        eighths = 1  # any progress at all shows
    full, rest = divmod(eighths, 8)
    cells = _FULL * full + (_EIGHTHS[rest] if rest else "")
    return f"`{cells}{_EMPTY * (width - len(cells))}`"


def percent(part: int, whole: int) -> str:
    """`part` of `whole` as a percentage with one decimal; a share too small to show is `<0.1%`."""
    if whole <= 0:
        return "n/a"
    value = 100.0 * part / whole
    if 0 < value < 0.05:
        return "<0.1%"
    return f"{value:.1f}%"


def badge_color(part: int, whole: int) -> str:
    """The shields.io colour of a percentage, from grey (nothing yet) to green."""
    value = 100.0 * part / whole if whole > 0 else 0.0
    color = "lightgrey"
    for threshold, name in ((0.0, "red"), (10.0, "orange"), (30.0, "yellow"), (60.0, "yellowgreen"), (90.0, "green")):
        if part > 0 and value >= threshold:
            color = name
    return color


def badge_url(label: str, part: int, whole: int) -> str:
    """A shields.io static badge for a percentage, coloured from grey (nothing yet) to green."""
    color = badge_color(part, whole)
    # In a static badge path, `-` separates the fields, so a literal one is doubled; `%` must be escaped.
    text = quote(percent(part, whole).replace("-", "--"), safe="")
    return f"https://img.shields.io/badge/{quote(label.replace('-', '--'), safe='')}-{text}-{color}"


def _share_cell(part: int, whole: int | None, what: str) -> str:
    """`22.7% of the game's own functions (2,629 of 11,582)`: the share cell of a summary row, from counts."""
    if whole is None:
        return f"n/a: {what} needs the Ghidra listing, which counts the functions ({part:,} recorded)"
    return f"{percent(part, whole)} {what} ({part:,} of {whole:,})"


def _summary_rows(progress: Progress, roadmap_link: str) -> list[str]:
    """The summary table of the docs page; `roadmap_link` is the roadmap's address relative to it.

    Every share counts functions of the game's own code in the Ghidra listing (the maintainer's decision of
    2026-10-29, docs/progress/index.md#by-function), so the rows agree with the picture's headline and the badges.
    """
    game = progress.game_functions or 0
    milestones = progress.milestones
    finished = sum(1 for m in milestones if m.status == "done")
    reimplemented = progress.reimplemented_functions
    researched = progress.researched_functions or 0
    own = "of the game's own functions"
    return [
        "| | Progress | Share |",
        "| --- | --- | --- |",
        f"| **Reimplemented** | {bar(reimplemented, game)} "
        f"| {_share_cell(reimplemented, progress.game_functions, own)} |",
        f"| **Researched** | {bar(researched, game)} "
        f"| {_share_cell(researched, progress.game_functions, 'placed in a source directory')} |",
        *_understood_row(progress),
        *_compared_row(progress),
        f"| **[Milestones]({roadmap_link})** | {bar(finished, len(milestones))} "
        f"| {finished} of {len(milestones)} done |",
    ]


def _compared_row(progress: Progress) -> list[str]:
    """The summary's "Compared" row: how much of the reimplemented code is checked side by side with the original."""
    done = progress.reimplemented_functions
    if not done:
        return []
    compared = progress.compared_functions
    return [
        f"| **Compared** | {bar(compared, done)} | {percent(compared, done)} of the reimplemented functions "
        f"compared with the original ({compared:,} of {done:,}) |"
    ]


def _understood_row(progress: Progress) -> list[str]:
    """The summary's "Understood" row, or nothing before the checkout has a Ghidra listing."""
    u = progress.understanding
    if u is None:
        return []
    return [
        f"| **Understood** | {bar(u.understood_functions, u.functions)} "
        f"| {percent(u.understood_functions, u.functions)} named in Ghidra and cited with evidence "
        f"({u.understood_functions:,} of {u.functions:,}) |"
    ]


def _badges(progress: Progress) -> str:
    """The shields.io badges: reimplemented, researched and, with a Ghidra listing, understood (all by function)."""
    game = progress.game_functions or 0
    done = progress.reimplemented_functions
    researched = progress.researched_functions or 0
    badges = (
        f"![Reimplemented: {percent(done, game)}]({badge_url('reimplemented', done, game)}) "
        f"![Researched: {percent(researched, game)}]({badge_url('researched', researched, game)})"
    )
    u = progress.understanding
    if u is not None:
        # On a line of its own (the same paragraph), so the line stays within the docs' 120 columns.
        understood = badge_url("understood", u.understood_functions, u.functions)
        badges += f"\n![Understood: {percent(u.understood_functions, u.functions)}]({understood})"
    return badges


def _subsystem_table(progress: Progress) -> list[str]:
    """One row per subsystem, then unattributed code; a subsystem with no functions in the listing shows n/a."""
    lines = [
        "| Subsystem | Reimplemented | Share | Functions reimplemented | Functions |",
        "| --- | --- | --- | --- | --- |",
    ]
    for name, total, done in progress.subsystem_rows():
        label = "Unattributed" if name == UNATTRIBUTED else f"`{name}`"
        if total == 0:
            lines.append(f"| {label} | {bar(0, 0)} | n/a | {done} | none in the listing |")
        else:
            lines.append(f"| {label} | {bar(done, total)} | {percent(done, total)} | {done:,} | {total:,} |")
    return lines


def _understood_table(progress: Progress) -> list[str]:
    """The "Understood" measure per subsystem: functions understood, named and cited."""
    u = progress.understanding
    if u is None:
        return ["Not measured yet: the checkout has no docs/progress/ghidra-functions.tsv."]
    lines = [
        "| Subsystem | Understood | Share | Functions understood | Named | Cited |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for r in u.by_subsystem([s.name for s in progress.totals.subsystems]):
        label = "Unattributed" if r.name == UNATTRIBUTED else f"`{r.name}`"
        lines.append(
            f"| {label} | {bar(r.understood_functions, r.functions)} | {percent(r.understood_functions, r.functions)} "
            f"| {r.understood_functions:,} of {r.functions:,} | {r.named:,} | {r.cited:,} |"
        )
    total_named = sum(1 for s in u.statuses if s.function.named)
    total_cited = sum(1 for s in u.statuses if s.cited)
    lines.append(
        f"| **All** | {bar(u.understood_functions, u.functions)} | {percent(u.understood_functions, u.functions)} "
        f"| {u.understood_functions:,} of {u.functions:,} | {total_named:,} | {total_cited:,} |"
    )
    return lines


def _coverage_table(progress: Progress) -> list[str]:
    """The source map's coverage categories as sizes of `.text`: a size table, with no share."""
    lines = ["| Category | Bytes |", "| --- | --- |"]
    lines += [f"| {c.label} | {c.bytes:,} |" for c in progress.totals.coverage]
    return lines


def _middleware_table(progress: Progress) -> list[str]:
    """The middleware Coney replaces, with its size."""
    lines = ["| Library | Bytes | Replaced by |", "| --- | --- | --- |"]
    for m in progress.totals.middleware:
        lines.append(f"| {m.name} | {m.bytes:,} | {m.replaced_by} |")
    return lines


def _function_table(progress: Progress) -> list[str]:
    """Every reimplemented function in address order, or "None yet."."""
    if not progress.functions:
        return ["None yet."]
    lines = ["| Address | Original name | Subsystem | Bytes |", "| --- | --- | --- | --- |"]
    for f in sorted(progress.functions, key=lambda f: f.address):
        size = f"{f.size:,}" if f.size is not None else "not in the listing"
        lines.append(f"| `{f.address:#010x}` | `{f.name}` | `{f.subsystem}` | {size} |")
    return lines


def render_page(progress: Progress, grid_html: str = "", missions_html: str = "") -> str:
    """The docs page's generated block (without the markers).

    `grid_html` and `missions_html` are the pictures (progress_site.py), put after the summary and the milestones;
    the page has neither before the checkout has a Ghidra listing or a missions list.
    """
    lines = ["## Overall", "", _badges(progress), "", *_summary_rows(progress, "../roadmap.md"), ""]
    missing = sum(1 for f in progress.functions if f.size is None)
    if missing:
        lines += [f"{missing} reimplemented function(s) are not counted above: {progress.no_size_reason}.", ""]
    if grid_html:
        lines += ["## Every function", "", grid_html, ""]
    lines += ["## Milestones", "", "| Milestone | Status |", "| --- | --- |"]
    lines += [f"| [{m.name}](../roadmap.md#{m.anchor}) | {m.status} |" for m in progress.milestones]
    if missions_html:
        lines += ["", "## Missions", "", missions_html]
    lines += ["", "## By subsystem", "", *_subsystem_table(progress), ""]
    lines += ["## Understood by subsystem", "", *_understood_table(progress), ""]
    sizes = "Sizes of the executable's `.text`, not progress."
    lines += ["## Research coverage", "", sizes, "", *_coverage_table(progress), ""]
    lines += ["## Middleware: replaced, not reimplemented", "", *_middleware_table(progress), ""]
    lines += ["## Reimplemented functions", "", *_function_table(progress)]
    return "\n".join(lines)


def fill_page(text: str, block: str, path: str) -> str:
    """`text` (the committed docs/progress/index.md) with PAGE_MARKER replaced by the generated note and `block`.

    Raises ProgressError, naming `path`, when the marker is missing: the page would then show no numbers.
    """
    if text.count(PAGE_MARKER) != 1:
        raise ProgressError(f"{path}: needs exactly one {PAGE_MARKER} line, where the generated tables go")
    return text.replace(PAGE_MARKER, f"{GENERATED_NOTE}\n{block}")


def fill_history_page(text: str, block: str, path: str) -> str:
    """`text` (the committed docs/progress/history.md) with HISTORY_MARKER replaced by the generated `block`."""
    if text.count(HISTORY_MARKER) != 1:
        raise ProgressError(f"{path}: needs exactly one {HISTORY_MARKER} line, where the chart and table go")
    return text.replace(HISTORY_MARKER, f"{GENERATED_NOTE}\n{block}")


def fill_home_page(text: str, block: str, path: str) -> str:
    """`text` (the committed docs/index.md) with HOME_MARKER replaced by the home page's picture `block`."""
    if text.count(HOME_MARKER) != 1:
        raise ProgressError(f"{path}: needs exactly one {HOME_MARKER} line, where the picture of functions goes")
    return text.replace(HOME_MARKER, f"{GENERATED_NOTE}\n{block}")


def _endpoint(label: str, message: str, color: str) -> str:
    """The JSON of one shields.io endpoint badge (https://shields.io/badges/endpoint-badge)."""
    data = {"schemaVersion": 1, "label": label, "message": message, "color": color}
    return json.dumps(data, indent=2) + "\n"


def badge_files(progress: Progress) -> dict[str, str]:
    """The README's badges as shields.io endpoint JSON, by file name (`reimplemented.json`, ...).

    The README points shields.io at these files on the published site, so its badges follow `main` without a
    pull request ever committing a number. The "understood" badge exists once the checkout has a Ghidra listing.
    """
    game = progress.game_functions or 0
    done = progress.reimplemented_functions
    researched = progress.researched_functions or 0
    files = {
        "reimplemented.json": _endpoint("reimplemented", percent(done, game), badge_color(done, game)),
        "researched.json": _endpoint("researched", percent(researched, game), badge_color(researched, game)),
    }
    u = progress.understanding
    if u is not None:
        files["understood.json"] = _endpoint(
            "understood",
            percent(u.understood_functions, u.functions),
            badge_color(u.understood_functions, u.functions),
        )
        compared = progress.compared_functions
        files["compared.json"] = _endpoint("compared", percent(compared, done), badge_color(compared, done))
    finished = sum(1 for m in progress.milestones if m.status == "done")
    count = len(progress.milestones)
    files["milestones.json"] = _endpoint("milestones", f"{finished} of {count} done", badge_color(finished, count))
    return files
