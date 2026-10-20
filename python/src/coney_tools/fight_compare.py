# SPDX-License-Identifier: GPL-3.0-or-later
"""Comparing the fight metrics of the original's recordings with Coney's runs (coney_tools.fight_report).

The two games draw different random numbers, so Coney is compared against the original's spread: the range of each
metric over the original's recordings of one orientation (which gang is side 1), since the gangs and the sides each
change the numbers (docs/research/rumble.md#gang-battle-metrics).
"""

from __future__ import annotations

import statistics
from collections import Counter
from dataclasses import dataclass

from coney_tools.fight_trace import Metric

#: Metric families where a run without the metric means zero (a goal never used, an attack never made, a share of a
#: count never seen).
ZERO_FILLED = ("goal.", "attack.", "attackers.share", "active.share")


@dataclass(frozen=True)
class Comparison:
    """One metric across the original's runs and Coney's."""

    name: str
    original: list[Metric]
    coney: list[Metric]
    flag: str  # "", "below", "above", "missing", "different"


def _spread(values: list[float]) -> tuple[float, float, float]:
    """The minimum, median and maximum of `values`."""
    return min(values), statistics.median(values), max(values)


def compare_runs(
    original: list[dict[str, Metric]], coney: list[dict[str, Metric]], margin: float = 0.0
) -> list[Comparison]:
    """Each metric of the original's runs against Coney's, flagging Coney's outside the original's spread.

    A metric is flagged when Coney's median is below the original's minimum or above its maximum, each widened by
    `margin` times the spread's width; a metric Coney lacks that the original has is `missing`; a name (a winner, a
    tactic) Coney's runs have and the original's never did is `different`. A run without a goal, an attack or a share
    metric counts as zero for it (ZERO_FILLED).
    """
    names = sorted({name for run in [*original, *coney] for name in run})
    results = []
    for name in names:
        fill = name.split(".", 1)[1] if "." in name else name
        zero = any(fill.startswith(prefix) for prefix in ZERO_FILLED)
        seen = [run[name] for run in [*original, *coney] if name in run]
        sides = []
        for runs in (original, coney):
            values = [run.get(name, 0.0 if zero else None) for run in runs]
            sides.append([value for value in values if value is not None])
        theirs, ours = sides
        if not theirs:
            continue  # only Coney has it: nothing to compare against
        if not ours:
            results.append(Comparison(name, theirs, ours, "missing"))
            continue
        if any(isinstance(value, str) for value in seen):
            outside = {str(v) for v in ours} - {str(v) for v in theirs}
            results.append(Comparison(name, theirs, ours, "different" if outside else ""))
            continue
        low, _, high = _spread([float(v) for v in theirs])
        slack = (high - low) * margin + 1e-9
        median = statistics.median(float(v) for v in ours)
        flag = "below" if median < low - slack else "above" if median > high + slack else ""
        results.append(Comparison(name, theirs, ours, flag))
    return results


def _format(value: Metric) -> str:
    """A metric as the report shows it."""
    if isinstance(value, str):
        return value
    return f"{value:.3f}".rstrip("0").rstrip(".") if value != int(value) else str(int(value))


def _spread_text(values: list[Metric]) -> str:
    """`min / median / max` of a metric's values, or the names of a categorical one."""
    if not values:
        return "-"
    if isinstance(values[0], str):
        counts = Counter(str(v) for v in values)
        return ", ".join(f"{name} x{count}" for name, count in sorted(counts.items()))
    low, middle, high = _spread([float(v) for v in values])
    return f"{_format(low)} / {_format(middle)} / {_format(high)}"


def report_compare(comparisons: list[Comparison], runs: tuple[int, int]) -> str:
    """The comparison table: each metric's original spread against Coney's, the outliers marked."""
    lines = [
        f"original: {runs[0]} runs, Coney: {runs[1]} runs; each cell is min / median / max over the runs; "
        "'<<' marks a Coney median outside the original's spread"
    ]
    width = max((len(c.name) for c in comparisons), default=6)
    original_cells = [_spread_text(c.original) for c in comparisons]
    original_width = max((len(cell) for cell in original_cells), default=8)
    lines.append(f"{'metric'.ljust(width)}  {'original'.ljust(original_width)}  coney")
    flagged = 0
    for comparison, original_cell in zip(comparisons, original_cells, strict=True):
        mark = ""
        if comparison.flag:
            flagged += 1
            mark = f"  << {comparison.flag}"
        lines.append(
            f"{comparison.name.ljust(width)}  {original_cell.ljust(original_width)}  "
            f"{_spread_text(comparison.coney)}{mark}"
        )
    lines.append(f"{flagged} of {len(comparisons)} metrics outside the original's spread")
    return "\n".join(lines)
