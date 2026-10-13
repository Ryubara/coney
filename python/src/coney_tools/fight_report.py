# SPDX-License-Identifier: GPL-3.0-or-later
"""Behaviour statistics of a gang fight, from an all-humans trace of either game.

The two games draw different random numbers, so a fight cannot be compared update by update. This module reads one
row per human per update (Coney's `--trace-humans`, `coney-tools pcsx2 record` of the all-humans field set) and
computes what a fight is judged by: the time each human spends on each goal, the attacks by kind and the blocks per
minute, how many humans go for one target at once, the distance to the target, how often the target changes, how
often a goal is interrupted, the time until the first engagement, the knock-out times, and the match's length and
winner. Every metric is worked out per human, per gang and for the whole match. A compare mode puts the metrics of
several recordings of the original (min, median, max over the runs) beside Coney's and flags the ones outside the
original's spread (docs/guides/research-workflow.md#comparing-with-coney, docs/guides/coney-tools.md#trace).

The trace is CSV with the columns `step`, `human` (a number per human) and `gang`; the others are optional, and a
metric whose columns are missing is left out:

* `x`, `y` (metres): the distance to the target.
* `health` (and `ko`): a human is knocked out when `ko` is 1 or `health` is 0 or less.
* `command`: the command id of the update; an attack is a change to an attack command (ATTACK_COMMANDS).
* `blocking` (or `block`): 1 while blocking (the block command, 4, counts too).
* `goal` (the top goal's type id; -1 for none) and the goal stack, as `goal0`, `goal1`, ... (the original's and
  Coney's columns: the type ids from the bottom, -1 above the top) or `stack` (the ids joined with `>`).
* `target`: the number of the human it fights (-1 for none).
"""

from __future__ import annotations

import csv
import math
import re
import statistics
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import yaml

#: Updates a second of a trace: the characters' step.
DEFAULT_HZ = 30
#: How near a human's target must be for it to count as engaged, metres, when it has not attacked yet.
DEFAULT_ENGAGE_DISTANCE = 3.0
#: The block command (combat::command::kR1Held).
BLOCK_COMMAND = 4

#: The attack commands (combat::command, docs/research/combat.md#commands) by the name a report shows.
ATTACK_COMMANDS = {
    0x0D: "grab",
    0x0E: "tackle",
    0x0F: "square",
    0x10: "cross",
    0x11: "square",
    0x12: "cross",
    0x19: "grab_spin",
    0x1E: "throw",
    0x20: "charge",
    0x21: "dive",
    0x22: "power",
    0x23: "grapple",
}

#: The goal type ids (src/ai/goal.h, docs/research/ai.md#goals) by the name a report shows.
GOAL_NAMES = {
    0x00: "idle",
    0x01: "move_to_flag",
    0x06: "move_to_human",
    0x08: "melee",
    0x0B: "engage_enemy",
    0x0F: "fight",
    0x10: "spectate",
    0x12: "react_grabbing",
    0x13: "react_tackling",
    0x14: "react_grabbed",
    0x15: "react_tackled",
    0x17: "react_knocked_down",
    0x18: "react_stunned",
    0x1B: "block",
    0x1D: "help_respond",
    0x1F: "grab_target",
    0x32: "follow_player",
    0x33: "follow_formation",
    0x34: "follow_and_attack",
    0x36: "hold_position",
    0x41: "find_enemy",
    0x54: "riot",
    0x9B: "backoff",
}

#: Metric families where a run without the metric means zero (a goal never used, an attack never made).
ZERO_FILLED = ("goal.", "attack.", "attackers.share")


class FightTraceError(Exception):
    """A trace that cannot be read; the message names the file and the problem."""


def load_goal_names(path: Path) -> None:
    """Name the goals by the reference list `path` (research/references/goal-types.yaml), a goal id to its class name
    in snake case. Raises FightTraceError when the file cannot be read."""
    try:
        entries = yaml.safe_load(path.read_text(encoding="utf-8"))["entries"]
    except (OSError, yaml.YAMLError, KeyError, TypeError) as error:
        raise FightTraceError(f"{path}: {error}") from error
    for entry in entries:
        name = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", str(entry["name"])).lower()
        GOAL_NAMES[int(entry["id"])] = name


def goal_name(goal: int) -> str:
    """A goal id as the report shows it."""
    if goal < 0:
        return "none"
    return GOAL_NAMES.get(goal, f"goal_{goal:#04x}")


@dataclass(frozen=True)
class Sample:
    """One human's row of one update; None where the trace has no such column."""

    step: int
    human: int
    gang: int
    x: float | None = None
    y: float | None = None
    ko: bool | None = None
    command: int | None = None
    block: bool | None = None
    goal: int | None = None
    stack: tuple[int, ...] | None = None
    target: int | None = None


@dataclass
class FightTrace:
    """A trace's rows by human (in step order) and by step."""

    name: str
    columns: frozenset[str]
    by_human: dict[int, list[Sample]]
    by_step: dict[int, list[Sample]]

    @property
    def last_step(self) -> int:
        """The last step with a row."""
        return max(self.by_step)


def _number(cell: str | None) -> float | None:
    """A cell as a number; None for an empty or non-numeric cell."""
    if cell is None or cell.strip() == "":
        return None
    try:
        value = float(cell)
    except ValueError:
        return None
    return value if math.isfinite(value) else None


def _whole(cell: str | None) -> int | None:
    """A cell as a whole number (a hex id like `0x0f` too); None for an empty or non-numeric cell."""
    if cell is None or cell.strip() == "":
        return None
    try:
        return int(cell.strip(), 0)
    except ValueError:
        value = _number(cell)
        return None if value is None else int(value)


def _stack(cell: str | None) -> tuple[int, ...] | None:
    """A `stack` cell (`8>65>15`; `-` or empty for none) as the goal ids."""
    if cell is None:
        return None
    text = cell.strip()
    if text in ("", "-"):
        return ()
    try:
        return tuple(int(part, 0) for part in text.split(">"))
    except ValueError:
        return None


def _stack_columns(row: dict[str, str], columns: frozenset[str]) -> tuple[int, ...] | None:
    """The goal stack from the columns `goal0`, `goal1`, ... (type ids from the bottom, -1 above the top); None
    when the trace has none."""
    if "goal0" not in columns:
        return None
    stack = []
    for index in range(len(columns)):
        goal = _whole(row.get(f"goal{index}")) if f"goal{index}" in columns else None
        if goal is None or goal < 0:
            break
        stack.append(goal)
    return tuple(stack)


def parse_fight_trace(text: str, name: str = "trace") -> FightTrace:
    """Read an all-humans trace from CSV `text`. Raises FightTraceError when it lacks `step`, `human` or `gang`."""
    reader = csv.DictReader(text.splitlines())
    columns = frozenset(reader.fieldnames or [])
    missing = [column for column in ("step", "human", "gang") if column not in columns]
    if missing:
        raise FightTraceError(f"{name}: no {', '.join(missing)} column: not an all-humans trace")
    by_human: dict[int, list[Sample]] = defaultdict(list)
    by_step: dict[int, list[Sample]] = defaultdict(list)
    seen: set[tuple[int, int]] = set()
    for line, row in enumerate(reader, start=2):
        step, human, gang = _whole(row["step"]), _whole(row["human"]), _whole(row["gang"])
        if step is None or human is None or gang is None:
            raise FightTraceError(f"{name}: line {line}: step, human and gang must be whole numbers")
        if (step, human) in seen:
            raise FightTraceError(f"{name}: line {line}: human {human} twice at step {step}")
        seen.add((step, human))
        health = _number(row.get("health"))
        ko = None
        if "ko" in columns:
            flag = _whole(row.get("ko"))
            ko = None if flag is None else flag != 0
        elif health is not None:
            ko = health <= 0
        block = None
        block_column = "blocking" if "blocking" in columns else "block"
        if block_column in columns:
            flag = _whole(row.get(block_column))
            block = None if flag is None else flag != 0
        sample = Sample(
            step=step,
            human=human,
            gang=gang,
            x=_number(row.get("x")),
            y=_number(row.get("y")),
            ko=ko,
            command=_whole(row.get("command")),
            block=block,
            goal=_whole(row.get("goal")),
            stack=_stack(row.get("stack")) if "stack" in columns else _stack_columns(row, columns),
            target=_whole(row.get("target")),
        )
        by_human[human].append(sample)
        by_step[step].append(sample)
    if not by_step:
        raise FightTraceError(f"{name}: no rows")
    for samples in by_human.values():
        samples.sort(key=lambda sample: sample.step)
    return FightTrace(name, columns, dict(by_human), dict(by_step))


def load_fight_trace(path: Path) -> FightTrace:
    """Read an all-humans trace file. Raises FightTraceError when it cannot be read."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise FightTraceError(f"{path}: {error}") from error
    return parse_fight_trace(text, str(path))


# --- one human ---------------------------------------------------------------------------------------------------


@dataclass
class HumanMetrics:
    """What one human did over the match."""

    human: int
    gang: int
    alive_steps: int = 0
    goal_steps: Counter[str] = field(default_factory=Counter)
    attacks: Counter[str] = field(default_factory=Counter)
    blocks: int = 0
    target_switches: int = 0
    goal_changes: int = 0
    goal_interruptions: int = 0
    distances: list[float] = field(default_factory=list)
    first_engage_step: int | None = None
    ko_step: int | None = None


@dataclass
class Analysis:
    """The metrics of one trace: per human, the groups of attackers, and the match's end."""

    trace: FightTrace
    hz: int
    end_step: int
    winner_gang: int | None
    winner_known: bool
    humans: dict[int, HumanMetrics]
    #: Per (step, target), the gangs of the humans going for it.
    attacker_groups: list[tuple[int, ...]]
    first_step: int


def _is_ko(sample: Sample) -> bool:
    """Whether a row is a knocked-out human's."""
    return bool(sample.ko)


def match_end(trace: FightTrace, excluded: frozenset[int]) -> tuple[int, int | None, bool]:
    """The match's last step, its winning gang and whether a winner is known.

    The match ends at the first step where at most one gang has a human on its feet (more than one gang needs to
    have been in it). The winner is that gang; with every gang knocked out there is none (a draw). A match that is
    still going at the trace's end has no winner.
    """
    samples_all = [sample for samples in trace.by_step.values() for sample in samples if sample.human not in excluded]
    gangs = {sample.gang for sample in samples_all if sample.gang >= 0}
    if len(gangs) < 2 or not any(sample.ko is not None for sample in samples_all):
        return trace.last_step, None, False
    for step in sorted(trace.by_step):
        standing = {s.gang for s in trace.by_step[step] if s.gang >= 0 and s.human not in excluded and not _is_ko(s)}
        if len(standing) <= 1:
            return step, (next(iter(standing)) if standing else None), True
    return trace.last_step, None, False


def _stack_change(previous: tuple[int, ...], current: tuple[int, ...]) -> tuple[bool, bool]:
    """Whether the goal stack changed from `previous` to `current`, and whether that interrupted a goal.

    A goal pushed over another suspends it, and a stack replaced instead of popped aborts the goals it replaced:
    both interrupt. A stack that only lost its top ended a goal, which is not an interruption.
    """
    if previous == current:
        return False, False
    if len(current) > len(previous) and current[: len(previous)] == previous:
        return True, bool(previous)
    if len(current) < len(previous) and previous[: len(current)] == current:
        return True, False
    return True, True


def _analyse_human(
    trace: FightTrace,
    samples: list[Sample],
    positions: dict[int, dict[int, tuple[float, float]]],
    end_step: int,
    engage_distance: float,
) -> HumanMetrics:
    """The metrics of one human up to the match's end."""
    first = samples[0]
    metrics = HumanMetrics(first.human, first.gang)
    previous: Sample | None = None
    previous_blocking = False
    previous_command = 0
    for sample in samples:
        if sample.step > end_step:
            break
        ko = _is_ko(sample)
        if ko and metrics.ko_step is None:
            metrics.ko_step = sample.step
        if not ko:
            metrics.alive_steps += 1
            if sample.goal is not None:
                metrics.goal_steps[goal_name(sample.goal)] += 1
        # Attacks and blocks: the first update of a command (a held one is one attack).
        command = sample.command if sample.command is not None else 0
        if not ko and command != previous_command and command in ATTACK_COMMANDS:
            metrics.attacks[ATTACK_COMMANDS[command]] += 1
            if metrics.first_engage_step is None:
                metrics.first_engage_step = sample.step
        previous_command = command
        blocking = (bool(sample.block) or command == BLOCK_COMMAND) and not ko
        if blocking and not previous_blocking:
            metrics.blocks += 1
        previous_blocking = blocking
        # The target: switches, and the distance to it.
        if sample.target is not None and sample.target >= 0 and not ko:
            if (
                previous is not None
                and previous.target is not None
                and previous.target >= 0
                and previous.target != sample.target
            ):
                metrics.target_switches += 1
            here = positions.get(sample.step, {})
            if sample.human in here and sample.target in here:
                distance = math.dist(here[sample.human], here[sample.target])
                metrics.distances.append(distance)
                if metrics.first_engage_step is None and distance <= engage_distance:
                    metrics.first_engage_step = sample.step
        # The goals: changes and interruptions, not counting what a knock-out clears.
        if previous is not None and not ko and not _is_ko(previous):
            if sample.stack is not None and previous.stack is not None:
                changed, interrupted = _stack_change(previous.stack, sample.stack)
            elif sample.goal is not None and previous.goal is not None:
                changed = sample.goal != previous.goal
                interrupted = changed and previous.goal >= 0
            else:
                changed = interrupted = False
            metrics.goal_changes += changed
            metrics.goal_interruptions += interrupted
        previous = sample
    return metrics


def fighting_gangs(trace: FightTrace) -> frozenset[int]:
    """The gangs that fight: the ones with a human that had a target, attacked or was knocked out.

    A level has bystander gangs (an audience, passers-by) that never fight; they are left out of the report. A
    human that blocked counts as fighting too. A trace
    without a target, command or health column cannot tell, so every gang counts.
    """
    samples = [sample for rows in trace.by_step.values() for sample in rows]
    if not any(s.target is not None or s.command is not None or s.ko is not None for s in samples):
        return frozenset(s.gang for s in samples if s.gang >= 0)
    return frozenset(
        s.gang
        for s in samples
        if s.gang >= 0
        and (
            (s.target is not None and s.target >= 0)
            or s.command in ATTACK_COMMANDS
            or s.command == BLOCK_COMMAND
            or bool(s.block)
            or _is_ko(s)
        )
    )


def from_step(trace: FightTrace, start: int) -> FightTrace:
    """`trace` without the steps before `start` (an intro, a countdown); times are then counted from `start`."""
    if start <= min(trace.by_step):
        return trace
    by_step = {step: rows for step, rows in trace.by_step.items() if step >= start}
    if not by_step:
        raise FightTraceError(f"{trace.name}: no rows from step {start}")
    by_human: dict[int, list[Sample]] = defaultdict(list)
    for step in sorted(by_step):
        for sample in by_step[step]:
            by_human[sample.human].append(sample)
    return FightTrace(trace.name, trace.columns, dict(by_human), by_step)


def analyse(
    trace: FightTrace,
    hz: int = DEFAULT_HZ,
    exclude: frozenset[int] = frozenset(),
    engage_distance: float = DEFAULT_ENGAGE_DISTANCE,
    start: int = 1,
) -> Analysis:
    """Work out every metric of `trace` from step `start` on.

    The humans in `exclude` (a spectator, a pad-driven player) are left out, and so are the humans of gangs that
    never fight (fighting_gangs()).
    """
    trace = from_step(trace, start)
    fighting = fighting_gangs(trace)
    exclude = exclude | frozenset(
        sample.human for samples in trace.by_human.values() for sample in samples if sample.gang not in fighting
    )
    end_step, winner, known = match_end(trace, exclude)
    positions: dict[int, dict[int, tuple[float, float]]] = {}
    for step, samples in trace.by_step.items():
        positions[step] = {s.human: (s.x, s.y) for s in samples if s.x is not None and s.y is not None}
    humans = {
        human: _analyse_human(trace, samples, positions, end_step, engage_distance)
        for human, samples in trace.by_human.items()
        if human not in exclude
    }
    groups: list[tuple[int, ...]] = []
    for step, samples in trace.by_step.items():
        if step > end_step:
            continue
        standing = {s.human: s for s in samples if not _is_ko(s) and s.human not in exclude}
        attackers: dict[int, list[int]] = defaultdict(list)
        for sample in standing.values():
            if sample.target is not None and sample.target in standing:
                attackers[sample.target].append(sample.gang)
        groups.extend(tuple(gangs) for gangs in attackers.values())
    return Analysis(trace, hz, end_step, winner, known, humans, groups, min(trace.by_step))


# --- the numbers -------------------------------------------------------------------------------------------------

Metric = float | str
#: analyse()'s settings after the trace: updates a second, humans left out, engage distance, first step.
Settings = tuple[int, frozenset[int], float, int]


def _percentile(values: list[float], fraction: float) -> float:
    """The `fraction` (0 to 1) percentile of `values` by linear interpolation."""
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = fraction * (len(ordered) - 1)
    low = math.floor(position)
    high = math.ceil(position)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def _group_metrics(analysis: Analysis, members: list[HumanMetrics], gangs: set[int] | None) -> dict[str, Metric]:
    """The metrics of a set of humans (a gang, or all); `gangs` limits the attackers counted per target."""
    hz = analysis.hz
    metrics: dict[str, Metric] = {}
    alive = sum(member.alive_steps for member in members)
    minutes = alive / hz / 60
    metrics["humans"] = float(len(members))
    goal_total = sum(sum(member.goal_steps.values()) for member in members)
    if goal_total:
        goals: Counter[str] = Counter()
        for member in members:
            goals.update(member.goal_steps)
        for name, steps in goals.items():
            metrics[f"goal.{name}"] = steps / goal_total
    columns = analysis.trace.columns
    if minutes > 0:
        if "command" in columns:
            attacks: Counter[str] = Counter()
            for member in members:
                attacks.update(member.attacks)
            for name, count in attacks.items():
                metrics[f"attack.{name}_per_min"] = count / minutes
            metrics["attack.total_per_min"] = sum(attacks.values()) / minutes
        if "command" in columns or "block" in columns:
            metrics["block.per_min"] = sum(member.blocks for member in members) / minutes
        if "target" in columns:
            metrics["target.switches_per_min"] = sum(member.target_switches for member in members) / minutes
        if "goal" in columns or "stack" in columns:
            metrics["goal.changes_per_min"] = sum(member.goal_changes for member in members) / minutes
            metrics["goal.interruptions_per_min"] = sum(member.goal_interruptions for member in members) / minutes
    sizes = [len(group if gangs is None else [g for g in group if g in gangs]) for group in analysis.attacker_groups]
    sizes = [size for size in sizes if size > 0]
    if sizes:
        metrics["attackers.mean"] = statistics.fmean(sizes)
        metrics["attackers.p90"] = _percentile([float(size) for size in sizes], 0.9)
        metrics["attackers.max"] = float(max(sizes))
        metrics["attackers.share1"] = sizes.count(1) / len(sizes)
        metrics["attackers.share2"] = sizes.count(2) / len(sizes)
        metrics["attackers.share3plus"] = sum(1 for size in sizes if size >= 3) / len(sizes)
    distances = [d for member in members for d in member.distances]
    if distances:
        metrics["distance.p10"] = _percentile(distances, 0.1)
        metrics["distance.p50"] = _percentile(distances, 0.5)
        metrics["distance.p90"] = _percentile(distances, 0.9)
    engaged = [(m.first_engage_step - analysis.first_step) / hz for m in members if m.first_engage_step is not None]
    if members:
        metrics["engage.share"] = len(engaged) / len(members)
    if engaged:
        metrics["engage.first_s"] = min(engaged)
        metrics["engage.median_s"] = statistics.median(engaged)
    knockouts = sorted((m.ko_step - analysis.first_step) / hz for m in members if m.ko_step is not None)
    metrics["ko.count"] = float(len(knockouts))
    if knockouts:
        metrics["ko.first_s"] = knockouts[0]
        metrics["ko.median_s"] = statistics.median(knockouts)
        metrics["ko.last_s"] = knockouts[-1]
    return metrics


def gangs_of(analysis: Analysis) -> list[int]:
    """The gang ids in the trace, from the lowest (the humans in no gang, -1, are not a gang)."""
    return sorted({m.gang for m in analysis.humans.values() if m.gang >= 0})


def scalars(analysis: Analysis, labels: dict[int, str] | None = None) -> dict[str, Metric]:
    """Every metric of a run as one flat map: `all.*`, `match.*` and one prefix per gang (its label in `labels`).

    `labels` names the gangs (a gang's id to `g0`, `g1`, ...); a gang without a label is called `g<id>`.
    """
    labels = labels or {}
    out: dict[str, Metric] = {}
    members = list(analysis.humans.values())
    for key, value in _group_metrics(analysis, members, None).items():
        out[f"all.{key}"] = value
    for gang in gangs_of(analysis):
        label = labels.get(gang, f"g{gang}")
        gang_members = [m for m in members if m.gang == gang]
        for key, value in _group_metrics(analysis, gang_members, {gang}).items():
            out[f"{label}.{key}"] = value
    out["match.length_s"] = (analysis.end_step - analysis.first_step + 1) / analysis.hz
    if analysis.winner_known:
        winner = analysis.winner_gang
        out["match.winner"] = "draw" if winner is None else labels.get(winner, f"g{winner}")
    return out


def human_metrics(analysis: Analysis, member: HumanMetrics) -> dict[str, Metric]:
    """The metrics of one human."""
    metrics = _group_metrics(analysis, [member], {member.gang})
    for key in [key for key in metrics if key.startswith("attackers.")]:
        del metrics[key]
    del metrics["humans"]
    return metrics


# --- the text ----------------------------------------------------------------------------------------------------


def _format(value: Metric) -> str:
    """A metric as the report shows it."""
    if isinstance(value, str):
        return value
    return f"{value:.3f}".rstrip("0").rstrip(".") if value != int(value) else str(int(value))


def _table(rows: dict[str, Metric], indent: str = "  ") -> list[str]:
    """Aligned `name  value` lines."""
    if not rows:
        return [f"{indent}(nothing)"]
    width = max(len(name) for name in rows)
    return [f"{indent}{name.ljust(width)}  {_format(value)}" for name, value in sorted(rows.items())]


def report_run(analysis: Analysis, per_human: bool = False, labels: dict[int, str] | None = None) -> str:
    """The text report of one trace: the match, each gang, and with `per_human` each human."""
    flat = scalars(analysis, labels)
    lines = [
        f"{analysis.trace.name}: {len(analysis.humans)} humans, {len(analysis.trace.by_step)} steps, "
        f"match {_format(flat['match.length_s'])} s, winner {_format(flat.get('match.winner', 'not known'))}"
    ]
    scopes = ["all", *[(labels or {}).get(g, f"g{g}") for g in gangs_of(analysis)]]
    for scope in scopes:
        title = "all humans" if scope == "all" else f"gang {scope}"
        lines.append(f"{title}:")
        prefix = f"{scope}."
        lines.extend(_table({k.removeprefix(prefix): v for k, v in flat.items() if k.startswith(prefix)}))
    if per_human:
        for number in sorted(analysis.humans):
            member = analysis.humans[number]
            lines.append(f"human {number} (gang {member.gang}):")
            lines.extend(_table(human_metrics(analysis, member)))
    return "\n".join(lines)


# --- comparing runs ----------------------------------------------------------------------------------------------


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
    `margin` times the spread's width; a metric Coney lacks that the original has is `missing`; a winner (a name)
    Coney's runs have and the original's never did is `different`. A run without a goal or an attack metric counts as
    zero for it (ZERO_FILLED).
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
