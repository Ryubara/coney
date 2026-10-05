# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares two per-update traces: the original's (`coney-tools pcsx2 record`) and Coney's (`coney --trace`).

Both are CSV files with a `step` column (1 for the first update of a run; frame N of an input script is step N + 1)
and the same names for the same quantities. The diff takes each column the two share, aligns the rows by step (Coney's
step + `shift` against the original's step), and reports for each the first step whose difference passes the
column's tolerance, the largest difference and the rows around the first divergence.

The two games rarely start at the same spot: Coney starts a level where its script puts the player, the original
where the save state was made. With the start frame on, each trace is moved into its own player's frame at the
first compared step: positions relative to the player's feet there, turned so that the player faces +y, and headings
relative to the player's heading there. A walk then compares as metres forward and to the side.
"""

from __future__ import annotations

import csv
import math
from dataclasses import dataclass, field
from pathlib import Path

#: Columns that are headings in degrees: compared modulo 360.
ANGLE_COLUMNS = frozenset({"heading", "cam_yaw"})
#: The position columns the start frame moves, as (x, y, z) triples.
POSITION_COLUMNS = (
    ("x", "y", "z"),
    ("cam_x", "cam_y", "cam_z"),
    ("look_x", "look_y", "look_z"),
    ("wanted_x", "wanted_y", "wanted_z"),
)
#: The tolerance of a column whose values are all whole numbers, and of any other, unless one is given.
INTEGER_TOLERANCE = 0.0
FLOAT_TOLERANCE = 1e-3


class TraceError(Exception):
    """A trace that cannot be read or compared; the message names the file and the problem."""


@dataclass
class Trace:
    """A trace's columns and its numeric values by step (None where a value is not a number)."""

    path: Path
    header: list[str]
    rows: dict[int, dict[str, float | None]]
    integer: dict[str, bool]


def _number(text: str) -> float | None:
    """A cell as a number, or None for text (Coney's traversal names) and empty cells."""
    try:
        value = float(text)
    except ValueError:
        return None
    return value if math.isfinite(value) else None


def load_trace(path: Path) -> Trace:
    """Read a trace CSV. Raises TraceError when it cannot be read, has no `step` column or repeats a step."""
    try:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.reader(handle)
            header = next(reader, None)
            lines = list(reader)
    except (OSError, UnicodeDecodeError, csv.Error) as error:
        raise TraceError(f"{path}: {error}") from error
    if not header or "step" not in header:
        raise TraceError(f"{path}: no step column; is it a trace?")
    rows: dict[int, dict[str, float | None]] = {}
    integer = dict.fromkeys(header, True)
    for number, line in enumerate(lines, start=2):
        if not line:
            continue
        if len(line) != len(header):
            raise TraceError(f"{path}: line {number} has {len(line)} cells, the header {len(header)}")
        cells = dict(zip(header, line, strict=True))
        step = _number(cells["step"])
        if step is None or step != int(step):
            raise TraceError(f"{path}: line {number}: the step {cells['step']!r} is not a whole number")
        if int(step) in rows:
            raise TraceError(f"{path}: step {int(step)} appears twice")
        values = {name: _number(text) for name, text in cells.items()}
        for name, text in cells.items():
            if values[name] is None or not text.lstrip("-").isdigit():
                integer[name] = False
        rows[int(step)] = values
    return Trace(path, header, rows, integer)


def _wrap(degrees: float) -> float:
    """Degrees wrapped to (-180, 180]."""
    wrapped = math.fmod(degrees + 180.0, 360.0)
    if wrapped <= 0:
        wrapped += 360.0
    return wrapped - 180.0


def to_start_frame(trace: Trace, step: int) -> Trace:
    """`trace` moved into its player's frame at `step`: see the module docstring. Raises TraceError when that row
    lacks the player's x, y, z or heading."""
    origin = trace.rows.get(step)
    if origin is None or any(origin.get(name) is None for name in ("x", "y", "z", "heading")):
        raise TraceError(f"{trace.path}: step {step} has no x, y, z and heading to take the start frame from")
    ox, oy, oz, heading = (float(origin[name] or 0.0) for name in ("x", "y", "z", "heading"))
    turn = math.radians(heading)
    cos, sin = math.cos(turn), math.sin(turn)
    rows: dict[int, dict[str, float | None]] = {}
    for key, row in trace.rows.items():
        moved = dict(row)
        for xn, yn, zn in POSITION_COLUMNS:
            x, y, z = row.get(xn), row.get(yn), row.get(zn)
            if x is not None and y is not None:
                # Turn by -heading: the player's facing (-sin h, cos h) becomes +y.
                dx, dy = x - ox, y - oy
                moved[xn] = dx * cos + dy * sin
                moved[yn] = -dx * sin + dy * cos
            if z is not None:
                moved[zn] = z - oz
        for name in ANGLE_COLUMNS:
            value = row.get(name)
            if value is not None:
                moved[name] = _wrap(value - heading)
        rows[key] = moved
    integer = dict(trace.integer)
    for triple in POSITION_COLUMNS:
        for name in triple:
            integer[name] = False
    return Trace(trace.path, trace.header, rows, integer)


@dataclass
class ColumnResult:
    """How one column compares."""

    name: str
    tolerance: float
    compared: int = 0
    first: int | None = None
    max_error: float = 0.0
    max_step: int | None = None
    around: list[tuple[int, float | None, float | None]] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        """Whether every compared step is within the tolerance."""
        return self.first is None


@dataclass
class Comparison:
    """The result of a diff: the steps compared and each column's result."""

    start: int
    end: int
    shift: int
    missing: list[int]
    columns: list[ColumnResult]

    @property
    def ok(self) -> bool:
        """Whether every column is within its tolerance."""
        return all(column.ok for column in self.columns)


def shared_columns(original: Trace, coney: Trace) -> list[str]:
    """The numeric columns both traces have, in the original's order (`step` excluded)."""

    def numeric(trace: Trace, name: str) -> bool:
        """Whether the column holds a number anywhere in the trace."""
        return any(row.get(name) is not None for row in trace.rows.values())

    return [
        name
        for name in original.header
        if name != "step" and name in coney.header and numeric(original, name) and numeric(coney, name)
    ]


def _tolerance(name: str, tolerances: dict[str, float], integer: bool) -> float:
    """A column's tolerance: its own, else `*`'s, else exact for whole numbers and FLOAT_TOLERANCE for the rest."""
    if name in tolerances:
        return tolerances[name]
    if "*" in tolerances:
        return tolerances["*"]
    return INTEGER_TOLERANCE if integer else FLOAT_TOLERANCE


def _error(name: str, a: float | None, b: float | None) -> float:
    """The difference of two values (modulo 360 for a heading); infinite when only one of them is a number."""
    if a is None or b is None:
        return 0.0 if a is None and b is None else math.inf
    difference = b - a
    return abs(_wrap(difference)) if name in ANGLE_COLUMNS else abs(difference)


def compare(
    original: Trace,
    coney: Trace,
    columns: list[str] | None = None,
    tolerances: dict[str, float] | None = None,
    start: int = 1,
    end: int | None = None,
    shift: int = 0,
    context: int = 3,
) -> Comparison:
    """Compare the traces from step `start` to `end` (default: the last step both have). Raises TraceError for a
    column one of them lacks or an empty range."""
    tolerances = tolerances or {}
    names = columns or shared_columns(original, coney)
    for name in names:
        for trace in (original, coney):
            if name not in trace.header:
                raise TraceError(f"{trace.path}: has no column {name}")
    last = min(max(original.rows), max(coney.rows) - shift) if original.rows and coney.rows else start - 1
    end = last if end is None else min(end, last)
    if end < start:
        raise TraceError(f"no steps to compare from {start} (the traces end at {last})")
    steps = [s for s in range(start, end + 1) if s in original.rows and s + shift in coney.rows]
    missing = [s for s in range(start, end + 1) if s not in steps]
    results = []
    for name in names:
        integer = original.integer.get(name, False) and coney.integer.get(name, False)
        result = ColumnResult(name, _tolerance(name, tolerances, integer))
        for step in steps:
            a, b = original.rows[step][name], coney.rows[step + shift][name]
            error = _error(name, a, b)
            result.compared += 1
            if result.max_step is None or error > result.max_error:
                result.max_error, result.max_step = error, step
            if result.first is None and error > result.tolerance:
                result.first = step
        if result.first is not None:
            for step in range(result.first - context, result.first + context + 1):
                if step in original.rows and step + shift in coney.rows:
                    result.around.append((step, original.rows[step][name], coney.rows[step + shift][name]))
        results.append(result)
    return Comparison(start, end, shift, missing, results)


def _cell(value: float | None) -> str:
    """A value for the report: whole numbers bare, others with four decimals."""
    if value is None:
        return "-"
    if math.isinf(value):
        return "inf"
    return str(int(value)) if value == int(value) else f"{value:.4f}"


def report(comparison: Comparison) -> str:
    """The comparison as text: a summary line, one line per column, then a short table at each divergence."""
    lines = [
        f"steps {comparison.start}-{comparison.end} (original step s against Coney step s + {comparison.shift}); "
        f"{len(comparison.missing)} step(s) missing from a trace"
        + (f": {', '.join(map(str, comparison.missing[:10]))}" if comparison.missing else "")
    ]
    width = max([len(c.name) for c in comparison.columns] + [6])
    lines.append(f"{'column':<{width}}  {'first divergence':>16}  {'max error':>10}  {'at step':>7}  {'tolerance':>9}")
    for column in comparison.columns:
        first = "ok" if column.ok else f"step {column.first}"
        lines.append(
            f"{column.name:<{width}}  {first:>16}  {_cell(column.max_error):>10}  "
            f"{_cell(column.max_step) if column.max_step is not None else '-':>7}  {_cell(column.tolerance):>9}"
        )
    for column in comparison.columns:
        if column.ok:
            continue
        lines.append("")
        lines.append(f"{column.name}: first divergence at step {column.first}")
        lines.append(f"  {'step':>5}  {'original':>10}  {'coney':>10}  {'error':>10}")
        for step, a, b in column.around:
            marker = " <" if step == column.first else ""
            lines.append(f"  {step:>5}  {_cell(a):>10}  {_cell(b):>10}  {_cell(_error(column.name, a, b)):>10}{marker}")
    return "\n".join(lines)
