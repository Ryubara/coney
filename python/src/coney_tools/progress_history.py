# SPDX-License-Identifier: GPL-3.0-or-later
"""The progress history: one point per day of `main`, rebuilt from git, never committed.

A point is the tracker's own headline numbers (reimplemented, researched and understood bytes with their
denominators, and the function counts) for the last first-parent commit of a UTC day. The days come from git, so the
history needs no state of its own: `reconstruct` extracts the few inputs the tracker reads (docs/progress, the
research pages, the roadmap, the bindings and the `@orig` tags in `src/`) from each day's commit into a temporary
folder and loads them with today's code, so every day is measured by the same definition. A day whose inputs cannot
be loaded (before the tracker existed, or a format since changed) is left out. Today's point is the build's own state.

A previously published `history.json` can be passed in as a cache (`merge`): its days are kept, only missing days are
extracted, so a deploy costs one snapshot instead of all of them. The file carries `SCHEMA`; a cache with another
schema is ignored, and the history is rebuilt in full. Bump `SCHEMA` when a definition changes.
"""

from __future__ import annotations

import io
import json
import subprocess
import tarfile
import tempfile
from collections.abc import Callable, Iterable
from dataclasses import asdict, dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any
from xml.sax.saxutils import escape

from coney_tools import progress
from coney_tools.config import ConfigError
from coney_tools.progress import Progress

SCHEMA = 1
#: Where the file sits in the built site; the deploy fetches the published copy as a cache.
HISTORY_FILE = "progress/history.json"
#: The inputs the tracker reads, as git paths: everything `progress.load` needs and nothing else (no images, no code).
INPUTS = ("docs/progress", "docs/research", "docs/roadmap.md", "research/bindings", "src")
MEASURES = ("reimplemented", "understood", "researched")


@dataclass(frozen=True)
class Point:
    """The tracker's headline numbers on one day."""

    date: str  # UTC, `2026-10-21`
    commit: str  # abbreviated hash the numbers were taken at
    game_bytes: int
    reimplemented_bytes: int
    reimplemented_functions: int
    researched_bytes: int
    understood_bytes: int | None = None  # None before the Ghidra listing existed
    understood_functions: int | None = None
    listing_functions: int | None = None
    listing_bytes: int | None = None

    def share(self, measure: str) -> float | None:
        """The measure as a percentage of its own denominator (the Progress page's bars), or None when unknown."""
        if measure == "reimplemented":
            part, whole = self.reimplemented_bytes, self.game_bytes
        elif measure == "researched":
            part, whole = self.researched_bytes, self.game_bytes
        else:
            if self.understood_bytes is None or not self.listing_bytes:
                return None
            part, whole = self.understood_bytes, self.listing_bytes
        return 100.0 * part / whole if whole else None


def point_of(state: Progress, date: str, commit: str) -> Point:
    """The point for the tracker's `state` as of `date` and `commit`."""
    totals = state.totals
    u = state.understanding
    return Point(
        date,
        commit,
        totals.game_bytes,
        state.reimplemented_bytes,
        len(state.functions),
        totals.researched_bytes,
        None if u is None else u.understood_bytes,
        None if u is None else u.understood_functions,
        None if u is None else u.functions,
        None if u is None else u.bytes,
    )


def load_cache(text: str) -> list[Point]:
    """The points of a published `history.json`, or none when it is not one of this schema (so it is rebuilt)."""
    try:
        data = json.loads(text)
        if data.get("schema") != SCHEMA:
            return []
        return [Point(**p) for p in data["points"]]
    except (ValueError, TypeError, KeyError, AttributeError):
        return []


def to_json(points: list[Point], skipped: list[str]) -> dict[str, Any]:
    """The history as plain data (`history.json`): the schema, the points in date order and the days left out."""
    return {"schema": SCHEMA, "points": [asdict(p) for p in points], "skipped": skipped}


# --- git --------------------------------------------------------------------------------------------------------------


def _git(root: Path, *args: str) -> bytes:
    """Run git in `root` and return its output; a failure raises ConfigError."""
    try:
        done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ConfigError(f"git {' '.join(args[:2])}: {error}") from error
    return done.stdout


def utc_day(stamp: int) -> str:
    """The UTC date of a Unix time, `2026-10-21`."""
    return datetime.fromtimestamp(stamp, UTC).strftime("%Y-%m-%d")


def day_commits(root: Path, ref: str = "HEAD") -> list[tuple[str, str]]:
    """(date, abbreviated hash) of the last first-parent commit of each UTC day, oldest day first."""
    out = _git(root, "log", "--first-parent", "--format=%h%x09%ct", ref).decode("utf-8")
    days: dict[str, str] = {}
    for line in out.splitlines():
        short, stamp = line.split("\t")
        days.setdefault(utc_day(int(stamp)), short)  # newest first, so the first one seen is the day's last
    return sorted(days.items())


def extract_snapshot(root: Path, commit: str, dest: Path) -> bool:
    """Write the tracker's inputs as of `commit` under `dest`; False when the commit has none of them."""
    present = _git(root, "ls-tree", "--name-only", commit, "--", *INPUTS).decode("utf-8").split()
    if not present:
        return False
    data = _git(root, "archive", "--format=tar", commit, "--", *present)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        archive.extractall(dest, filter="data")
    return True


def snapshot_point(root: Path, date: str, commit: str) -> Point | None:
    """The point at `commit`, or None when the tracker cannot read its inputs there."""
    with tempfile.TemporaryDirectory(prefix="coney-progress-") as folder:
        dest = Path(folder)
        try:
            if not extract_snapshot(root, commit, dest):
                return None
            return point_of(progress.load(dest), date, commit)
        except (ConfigError, OSError, tarfile.TarError, ValueError, KeyError):
            return None


def reconstruct(
    root: Path,
    days: Iterable[tuple[str, str]],
    known: dict[str, Point] | None = None,
    measure: Callable[[Path, str, str], Point | None] = snapshot_point,
) -> tuple[list[Point], list[str]]:
    """Points for `days` (date, commit), reusing `known` points by date; returns (points, dates left out)."""
    known = known or {}
    points: list[Point] = []
    skipped: list[str] = []
    for date, commit in days:
        point = known.get(date)
        # A cached day counts only for the commit it was measured at: a rewritten history measures the day again.
        if point is not None and not (commit.startswith(point.commit) or point.commit.startswith(commit)):
            point = None
        point = point or measure(root, date, commit)
        if point is None:
            skipped.append(date)
        else:
            points.append(point)
    return points, skipped


def build(
    root: Path,
    current: Point,
    cache: list[Point] | None = None,
    measure: Callable[[Path, str, str], Point | None] = snapshot_point,
) -> tuple[list[Point], list[str]]:
    """The history up to `current`: git's days before today (from the cache where it has them), then today's point.

    Without git history (a shallow clone, no repository) only `current` is known and the history is that one point.
    """
    try:
        days = [d for d in day_commits(root) if d[0] < current.date]
    except ConfigError:
        days = []
    points, skipped = reconstruct(root, days, {p.date: p for p in cache or []}, measure)
    return [*points, current], skipped


# --- the chart --------------------------------------------------------------------------------------------------------

_W, _H = 640, 260
_LEFT, _RIGHT, _TOP, _BOTTOM = 40, 12, 12, 26
LINE_CLASSES = {m: f"cg-line-{m}" for m in MEASURES}
LABELS = {"reimplemented": "Reimplemented", "understood": "Understood", "researched": "Researched"}


def _pct(value: float) -> str:
    """A percentage with one decimal."""
    return f"{value:.1f}%"


def _x_positions(points: list[Point]) -> list[float]:
    """Each point's x by date, so a gap of days shows as a gap; a single point sits in the middle."""
    days = [datetime.strptime(p.date, "%Y-%m-%d").replace(tzinfo=UTC) for p in points]
    span = (days[-1] - days[0]) / timedelta(days=1) if len(days) > 1 else 0
    width = _W - _LEFT - _RIGHT
    if span == 0:
        return [_LEFT + width / 2 for _ in days]
    return [_LEFT + width * ((d - days[0]) / timedelta(days=1)) / span for d in days]


def render_chart(points: list[Point], uid: str = "ph") -> str:
    """The line chart of the three measures over the days, as SVG with one titled marker per day and measure.

    Percent of each measure's own denominator, as the Progress page's bars. Lines are 2 px, markers 3 px with a
    tooltip; the colours are the picture's state colours (extra.css). The standalone styling is the page's.
    """
    xs = _x_positions(points)
    plot_h = _H - _TOP - _BOTTOM

    def y(value: float) -> float:
        return _TOP + plot_h * (1 - value / 100)

    out = [
        f'<svg class="cg-chart" viewBox="0 0 {_W} {_H}" role="img" aria-labelledby="{uid}-t {uid}-d">',
        f'<title id="{uid}-t">Progress by day</title>',
        f'<desc id="{uid}-d">Reimplemented, understood and researched share of the original, one point per day.</desc>',
    ]
    for tick in (0, 25, 50, 75, 100):
        out.append(
            f'<line class="cg-grid" x1="{_LEFT}" x2="{_W - _RIGHT}" y1="{y(tick):.1f}" y2="{y(tick):.1f}"/>'
            f'<text class="cg-tick" x="{_LEFT - 6}" y="{y(tick) + 4:.1f}" text-anchor="end">{tick}%</text>'
        )
    # The first and last dates, plus the ones between when they fit.
    labelled = {0, len(points) - 1}
    if len(points) > 4:
        step = max(1, len(points) // 5)
        labelled |= set(range(0, len(points), step)) - {len(points) - 1 - k for k in range(1, step)}
    for i in sorted(labelled):
        anchor = (
            "start" if i == 0 and len(points) > 1 else "end" if i == len(points) - 1 and len(points) > 1 else "middle"
        )
        out.append(
            f'<text class="cg-tick" x="{xs[i]:.1f}" y="{_H - 8}" text-anchor="{anchor}">{points[i].date[5:]}</text>'
        )
    for measure in MEASURES:
        known = [(x, p, p.share(measure)) for x, p in zip(xs, points, strict=True) if p.share(measure) is not None]
        if len(known) > 1:
            line = " ".join(f"{x:.1f},{y(v or 0):.1f}" for x, _, v in known)
            out.append(f'<polyline class="{LINE_CLASSES[measure]}" points="{line}"/>')
        for x, p, v in known:
            tip = f"{p.date}: {LABELS[measure]} {_pct(v or 0)} (commit {p.commit})"
            out.append(
                f'<circle class="{LINE_CLASSES[measure]} cg-pt" cx="{x:.1f}" cy="{y(v or 0):.1f}" r="3">'
                f"<title>{escape(tip)}</title></circle>"
            )
    out.append("</svg>")
    return "\n".join(out)


def render_section(points: list[Point], skipped: list[str]) -> str:
    """The history page's generated block: the chart, its legend and a table of every day (newest first)."""
    legend = "".join(f'<li><span class="cg-key {LINE_CLASSES[m]}"></span>{LABELS[m]}</li>' for m in MEASURES)
    lines = [
        '<div class="progress-grid">',
        render_chart(points),
        f'<ul class="cg-legend cg-legend-inline">{legend}</ul>',
        "</div>",
        "",
        "| Date | Commit | Reimplemented | Functions | Understood | Researched |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for p in reversed(points):
        shares = {m: p.share(m) for m in MEASURES}

        def cell(m: str, shares: dict[str, float | None] = shares) -> str:
            v = shares[m]
            return "n/a" if v is None else _pct(v)

        lines.append(
            f"| {p.date} | `{p.commit}` | {cell('reimplemented')} | {p.reimplemented_functions:,} "
            f"| {cell('understood')} | {cell('researched')} |"
        )
    if skipped:
        lines += [
            "",
            f"Days left out because the tracker's inputs could not be read at their last commit: {len(skipped)}.",
        ]
    return "\n".join(lines)


__all__ = [
    "HISTORY_FILE",
    "SCHEMA",
    "Point",
    "build",
    "day_commits",
    "load_cache",
    "point_of",
    "reconstruct",
    "render_chart",
    "render_section",
    "to_json",
]
