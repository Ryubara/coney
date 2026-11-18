# SPDX-License-Identifier: GPL-3.0-or-later
"""The comparison of `coney-tools perf bench --compare` (docs/engine/renderer.md#budget).

Each scene runs three times; its figure is the median of the runs' mean frame times. A scene is over the budget when
that median is slower than the baseline's by more than the larger of 5 % and the baseline's own spread (its slowest run
over its fastest), so the gate is wider than the baseline's run-to-run noise. The gate fails only on a quiet machine:
while another build holds a build slot the comparison is printed and the exit stays 0, because a busy machine spreads
one scene's runs by more than the budget.
"""

from __future__ import annotations

import statistics
from dataclasses import dataclass
from typing import Any

# The least a scene may slow by before it is over the budget, as a fraction of the baseline's median.
MIN_TOLERANCE = 0.05


def median(values: list[float]) -> float:
    """The median of the runs' values."""
    return statistics.median(values)


def spread(values: list[float]) -> float:
    """How far apart a scene's runs are: the slowest over the fastest, minus 1 (0 with one run)."""
    if len(values) < 2 or min(values) <= 0:
        return 0.0
    return max(values) / min(values) - 1.0


@dataclass(frozen=True)
class SceneComparison:
    """One scene against its baseline: both medians, the change, the slowest the budget allows, and the verdict."""

    scene: str
    current: float  # ms, the median of the runs' mean frame times
    baseline: float | None  # None: the baseline has no such scene
    change_percent: float
    limit: float | None
    verdict: str  # "ok", "over" or "no baseline"


def compare_scenes(current: dict[str, list[float]], baseline: dict[str, list[float]]) -> list[SceneComparison]:
    """Each current scene (in the order given) against the baseline's scene of that name; `current` and `baseline` hold
    each scene's runs' mean frame times in ms. A scene the baseline lacks is "no baseline", never a failure."""
    rows = []
    for scene, runs in current.items():
        now = median(runs)
        base_runs = baseline.get(scene)
        if not base_runs:
            rows.append(SceneComparison(scene, now, None, 0.0, None, "no baseline"))
            continue
        base = median(base_runs)
        limit = base * (1.0 + max(MIN_TOLERANCE, spread(base_runs)))
        verdict = "over" if now > limit else "ok"
        rows.append(SceneComparison(scene, now, base, (now / base - 1.0) * 100.0, limit, verdict))
    return rows


def quiet_holders(status: dict[str, Any]) -> list[str]:
    """Who holds a build slot, from `build status --json`'s object, in every lane (a compile, a full suite): one
    description each, `agent (preset, step, lane lane)`. A stale claim (its process gone) holds nothing. Empty: the
    machine is quiet."""
    holders = []
    for lane, snapshot in status.items():
        for slot in snapshot.get("slots", []):
            claim = slot.get("claim")
            if claim is None or slot.get("stale"):
                continue
            holders.append(
                f"{claim.get('agent', '?')} ({claim.get('preset', '?')}, {claim.get('step', '?')}, {lane} lane)"
            )
    return holders


def format_comparison(rows: list[SceneComparison], titles: dict[str, str], holders: list[str]) -> str:
    """The comparison as a Markdown table with the budget's verdicts, then a line on whether the machine was quiet
    (and who held a slot when it was not), and one on what the exit code is."""
    lines = [
        "| Scene | Baseline ms | Now ms | Change | Limit ms | Verdict |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        title = titles.get(row.scene, row.scene)
        if row.baseline is None or row.limit is None:
            lines.append(f"| {title} | none | {row.current:.2f} | none | none | {row.verdict} |")
            continue
        lines.append(
            f"| {title} | {row.baseline:.2f} | {row.current:.2f} | {row.change_percent:+.1f} % | "
            f"{row.limit:.2f} | {row.verdict} |"
        )
    over = [row for row in rows if row.verdict == "over"]
    lines.append("")
    if holders:
        lines.append("Machine not quiet: build slots held by " + ", ".join(holders) + ".")
        verdict = "the comparison is reported, not enforced" if over else "nothing is over the budget anyway"
        lines.append(f"Exit 0: {verdict}.")
    else:
        lines.append("Machine quiet: no build holds a build slot.")
        lines.append(
            f"Exit 1: {len(over)} scene(s) over the budget." if over else "Exit 0: every scene is within the budget."
        )
    return "\n".join(lines) + "\n"


def exit_code(rows: list[SceneComparison], holders: list[str]) -> int:
    """1 when a scene is over the budget and the machine was quiet; 0 otherwise."""
    return 1 if not holders and any(row.verdict == "over" for row in rows) else 0
