# SPDX-License-Identifier: GPL-3.0-or-later
"""Behaviour statistics of a gang fight, from an all-humans trace of either game.

The two games draw different random numbers, so a fight cannot be compared update by update. This module reads one
row per human per update (Coney's `--trace-humans`, `coney-tools trace original` of the all-humans field set, with
coney_tools.fight_trace) and computes what a fight is judged by, by the rules of
docs/research/rumble.md#gang-battle-metrics: the fight's window (the brains going on to `RumbleOver`), who is out of
it, the winner, attacks, blocks, reactions, knock-downs and health losses per standing minute, the target changes by
kind and cause, the goals' shares and the fight goals' lengths, how many fighters go for one enemy and how many active
places are held, where the waiting ones stand, taunts, help calls and the idle share, and each side's tactic.
Every metric is worked out per human, per side and for the whole match. coney_tools.fight_compare puts the metrics of
several recordings of the original (min, median, max over the runs) beside Coney's and flags the ones outside the
original's spread (docs/guides/research-traces.md#comparing-with-coney, docs/guides/coney-tools-trace.md#trace).

The module is longer than the guides' rough 500 lines on purpose: reading (coney_tools.fight_trace) and comparing
(coney_tools.fight_compare) are already their own modules, and what is left is one pipeline whose steps share the
private per-human records (HumanMetrics, Analysis, the step view): the window and sides decide which rows the per-human
pass reads, and the numbers read what that pass kept. Split along those steps, each part would import the others'
private helpers, so they stay together.
"""

from __future__ import annotations

import math
import statistics
from collections import Counter, defaultdict
from dataclasses import dataclass, field

from coney_tools.fight_compare import Comparison, compare_runs, report_compare
from coney_tools.fight_trace import (
    ATTACK_COMMANDS,
    GOAL_NAMES,
    TARGET_CAUSES,
    FightTrace,
    FightTraceError,
    Metric,
    Sample,
    from_step,
    goal_name,
    load_fight_trace,
    load_goal_names,
    parse_fight_trace,
    set_target_cause,
    tactic_name,
)

__all__ = [
    "ATTACK_COMMANDS",
    "GOAL_NAMES",
    "Analysis",
    "Comparison",
    "FightTrace",
    "FightTraceError",
    "HumanMetrics",
    "Metric",
    "Sample",
    "Settings",
    "analyse",
    "compare_runs",
    "fighting_gangs",
    "gangs_of",
    "goal_name",
    "human_metrics",
    "load_fight_trace",
    "load_goal_names",
    "parse_fight_trace",
    "report_compare",
    "report_run",
    "scalars",
    "set_target_cause",
]

#: Updates a second of a trace: the characters' step.
DEFAULT_HZ = 30
#: How near a human's target must be for it to count as engaged, metres, when it has not attacked yet.
DEFAULT_ENGAGE_DISTANCE = 3.0
#: The block command (combat::command::kR1Held).
BLOCK_COMMAND = 4
#: Goal type ids the rules name: Fight, Blocking and HelpRespond (docs/references/goal-types.md).
FIGHT_GOAL, BLOCKING_GOAL, HELP_RESPOND_GOAL = 15, 27, 29
#: The taunt's clip (`0x25b`, docs/research/gang-fights.md#waiting).
TAUNT_CLIP = 0x25B
#: The crowd's brain type, whose gangs are not fighters.
CROWD_BRAIN = 4
#: The attack phase's wind-up bit (record `+0x08`).
WIND_UP = 0x1
#: Updates in a row with every fighter's goal stack empty that end a fight traced without `brain_off`.
QUIET_UPDATES = 30
#: A fight goal that lasts this long, seconds, is counted in `fight_goal.share_4s` (its deadline, 4000 ms).
LONG_FIGHT_GOAL_S = 4.0
#: The band a waiting attacker is sent to round his target, metres (docs/research/gang-fights.md#waiting).
WAIT_BAND = (4.0, 4.75)


def _is_ko(sample: Sample) -> bool:
    """Whether a row's human is out of the fight."""
    return bool(sample.ko)


# --- one human ---------------------------------------------------------------------------------------------------


@dataclass
class HumanMetrics:
    """What one human did over the fight. `alive_steps` counts its standing updates."""

    human: int
    gang: int
    alive_steps: int = 0
    goal_steps: Counter[str] = field(default_factory=Counter)
    attacks: Counter[str] = field(default_factory=Counter)
    attack_distances: list[float] = field(default_factory=list)
    blocks: int = 0
    held_blocks: int = 0
    reactions: int = 0
    knockdowns: int = 0
    health_losses: int = 0
    target_switches: int = 0
    target_sets: int = 0
    target_clears: int = 0
    goal_changes: int = 0
    goal_interruptions: int = 0
    distances: list[float] = field(default_factory=list)
    fight_goals: list[int] = field(default_factory=list)
    active: Counter[int] = field(default_factory=Counter)
    wait_mutual: list[float] = field(default_factory=list)
    wait_other: list[float] = field(default_factory=list)
    taunts: int = 0
    help_responds: int = 0
    idle_steps: int = 0
    help_calls: int = 0
    helpers_sent: int = 0
    #: Its target changes by cause (rule 7), from the `set-target` hook log or Coney's `target_causes`.
    target_causes: Counter[str] = field(default_factory=Counter)
    first_engage_step: int | None = None
    ko_step: int | None = None


@dataclass
class Analysis:
    """The metrics of one trace: per human, the groups of attackers, the sides and the fight's end."""

    trace: FightTrace
    hz: int
    end_step: int
    winner_gang: int | None
    winner_known: bool
    humans: dict[int, HumanMetrics]
    #: Per (step, target), the gangs of the standing humans going for it.
    attacker_groups: list[tuple[int, ...]]
    first_step: int
    #: Per side's gang, its fighters out of the fight at the end (when the end is RumbleOver or one side left).
    out_at_end: dict[int, int] = field(default_factory=dict)
    #: Per side's gang, its gang-wide columns at each step of the fight (one fighter's row a step).
    gang_rows: dict[int, list[Sample]] = field(default_factory=dict)


def match_end(trace: FightTrace, excluded: frozenset[int]) -> tuple[int, int | None, bool]:
    """The match's last step, its winning gang and whether a winner is known, for a trace without the brains' or the
    goals' columns that mark the end.

    The match ends at the first step where at most one gang has a human on its feet (more than one gang needs to
    have been in it). The winner is that gang; with every gang knocked out there is none (a draw). A match that is
    still going at the trace's end has no winner.
    """
    samples_all = [sample for samples in trace.by_step.values() for sample in samples if sample.human not in excluded]
    gangs = {sample.gang for sample in samples_all if sample.gang >= 0}
    if len(gangs) < 2 or not any(sample.ko is not None for sample in samples_all):
        return trace.last_step, None, False
    for step in trace.steps:
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


@dataclass
class _StepView:
    """What a human's metrics read of the others at each step: positions and targets."""

    positions: dict[int, dict[int, tuple[float, float]]]
    targets: dict[int, dict[int, int]]
    #: How near a target counts as engaged, metres.
    engage: float = DEFAULT_ENGAGE_DISTANCE

    def distance(self, step: int, human: int, other: int) -> float | None:
        """Metres from `human` to `other` at `step`; None when either has no position."""
        here = self.positions.get(step, {})
        if human in here and other in here:
            return math.dist(here[human], here[other])
        return None


def _goal_change(metrics: HumanMetrics, previous: Sample, sample: Sample) -> None:
    """Counts a goal change and interruption between two standing rows (from the stack, else the top goal)."""
    if sample.stack is not None and previous.stack is not None:
        changed, interrupted = _stack_change(previous.stack, sample.stack)
    elif sample.goal is not None and previous.goal is not None:
        changed = sample.goal != previous.goal
        interrupted = changed and previous.goal >= 0
    else:
        changed = interrupted = False
    metrics.goal_changes += changed
    metrics.goal_interruptions += interrupted


def _target_change(metrics: HumanMetrics, previous: Sample, sample: Sample) -> None:
    """Counts a change of target between two standing rows by kind: A to B, set from none, cleared to none."""
    if sample.target is None or previous.target is None or sample.target == previous.target:
        return
    if previous.target < 0:
        metrics.target_sets += 1
    elif sample.target < 0:
        metrics.target_clears += 1
    else:
        metrics.target_switches += 1


def _attack(metrics: HumanMetrics, previous: Sample | None, sample: Sample, view: _StepView, by_phase: bool) -> None:
    """Counts an attack starting at a standing row: a wind-up starting (`phase`), else a change to an attack
    command; with its distance to the target."""
    if by_phase:
        before = previous.phase if previous is not None and previous.phase is not None else 0
        started = sample.phase is not None and sample.phase & WIND_UP != 0 and before & WIND_UP == 0
        kind = None
    else:
        command = sample.command or 0
        before = previous.command if previous is not None and previous.command is not None else 0
        started = command != before and command in ATTACK_COMMANDS
        kind = ATTACK_COMMANDS.get(command)
    if not started:
        return
    metrics.attacks[kind or "windup"] += 1
    if metrics.first_engage_step is None:
        metrics.first_engage_step = sample.step
    if by_phase and sample.target is not None and sample.target >= 0:
        distance = view.distance(sample.step, sample.human, sample.target)
        if distance is not None:
            metrics.attack_distances.append(distance)


def _standing_row(
    metrics: HumanMetrics, previous: Sample | None, sample: Sample, view: _StepView, trace: FightTrace
) -> None:
    """Everything one standing row adds: its time, goal, attack, blocks, reactions, target and waiting."""
    metrics.alive_steps += 1
    top = sample.top
    before_top = previous.top if previous is not None and previous.top is not None else -1
    if top is not None:
        metrics.goal_steps[goal_name(top)] += 1
        metrics.idle_steps += top < 0
        metrics.blocks += top == BLOCKING_GOAL and before_top != BLOCKING_GOAL
        metrics.help_responds += top == HELP_RESPOND_GOAL and before_top != HELP_RESPOND_GOAL
    if sample.active is not None:
        metrics.active[sample.active] += 1
    _attack(metrics, previous, sample, view, "phase" in trace.columns)
    if sample.clip == TAUNT_CLIP and (previous is None or previous.clip != TAUNT_CLIP):
        metrics.taunts += 1
    if previous is not None:
        metrics.reactions += sample.react is not None and sample.react >= 0 and previous.react == -1
        metrics.knockdowns += sample.down == 1 and previous.down == 0
        if sample.health is not None and previous.health is not None:
            metrics.health_losses += sample.health < previous.health
    # The target: its distance, and the waiting ones' distance split by whom their target fights.
    if sample.target is not None and sample.target >= 0:
        distance = view.distance(sample.step, sample.human, sample.target)
        if distance is not None:
            metrics.distances.append(distance)
            if metrics.first_engage_step is None and distance <= view.engage:
                metrics.first_engage_step = sample.step
            if top == FIGHT_GOAL and sample.fight_active == 0:
                mutual = view.targets.get(sample.step, {}).get(sample.target) == sample.human
                (metrics.wait_mutual if mutual else metrics.wait_other).append(distance)


@dataclass
class _FightGoalRuns:
    """The runs of Fight on top of one human: a new one when `fight_updates` starts again (or, without it, when Fight
    comes back on top), each as its first and last step."""

    counted: bool
    runs: list[int] = field(default_factory=list)
    start: int | None = None
    last: int = 0
    last_updates: int | None = None

    def see(self, sample: Sample, standing: bool) -> None:
        """Takes one row."""
        on_top = standing and sample.top == FIGHT_GOAL
        if not on_top:
            if not self.counted or not standing:
                self.close()
            return
        updates = sample.fight_updates
        restarted = (
            self.counted and updates is not None and self.last_updates is not None and updates < self.last_updates
        )
        if self.start is None or restarted:
            self.close()
            self.start = sample.step
        self.last = sample.step
        if updates is not None:
            self.last_updates = updates

    def close(self) -> None:
        """Ends the run under way."""
        if self.start is not None:
            self.runs.append(self.last - self.start + 1)
        self.start = None
        self.last_updates = None


def _analyse_human(trace: FightTrace, samples: list[Sample], view: _StepView, end_step: int) -> HumanMetrics:
    """The metrics of one human from the fight's first step to its end."""
    first = samples[0]
    metrics = HumanMetrics(first.human, first.gang)
    runs = _FightGoalRuns(counted="fight_updates" in trace.columns)
    previous: Sample | None = None
    for sample in samples:
        if sample.step > end_step:
            break
        ko = _is_ko(sample)
        if ko and metrics.ko_step is None:
            metrics.ko_step = sample.step
        runs.see(sample, not ko)
        if not ko:
            standing_before = previous if previous is not None and not _is_ko(previous) else None
            _standing_row(metrics, standing_before, sample, view, trace)
            # Held blocks: the blocking state or the block command starting.
            blocking = bool(sample.block) or sample.command == BLOCK_COMMAND
            was = standing_before is not None and (
                bool(standing_before.block) or standing_before.command == BLOCK_COMMAND
            )
            metrics.held_blocks += blocking and not was
            if standing_before is not None:
                _target_change(metrics, standing_before, sample)
                _goal_change(metrics, standing_before, sample)
        previous = sample
    runs.close()
    metrics.fight_goals = runs.runs
    # Gone from the trace before the end: deleted after a knock-out.
    last = samples[-1].step
    if metrics.ko_step is None and last < end_step:
        later = [step for step in trace.steps if last < step <= end_step]
        metrics.ko_step = later[0] if later else None
    window = range(trace.steps[0], end_step + 1)
    metrics.help_calls = sum(1 for step, human in trace.help_calls or [] if human == first.human and step in window)
    metrics.helpers_sent = sum(1 for step, human in trace.helpers_sent or [] if human == first.human and step in window)
    metrics.target_causes.update(
        cause for step, human, cause in trace.target_causes or [] if human == first.human and step in window
    )
    return metrics


# --- the fight's window and sides ----------------------------------------------------------------------------------


def fighting_gangs(trace: FightTrace) -> frozenset[int]:
    """The gangs that fight: the ones with a human that had a target, attacked, blocked or was knocked out, less the
    crowd (gangs whose humans are mostly of brain type 4, rule 1 of docs/research/rumble.md#gang-battle-metrics).

    A level has bystander gangs (an audience, passers-by) that never fight; they are left out of the report. A trace
    without a target, command or health column cannot tell, so every gang counts.
    """
    samples = [sample for rows in trace.by_step.values() for sample in rows]
    if not any(s.target is not None or s.command is not None or s.ko is not None for s in samples):
        gangs = {s.gang for s in samples if s.gang >= 0}
    else:
        gangs = {
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
        }
    if "btype" in trace.columns:
        crowd: Counter[int] = Counter()
        size: Counter[int] = Counter()
        for rows in trace.by_human.values():
            size[rows[0].gang] += 1
            crowd[rows[0].gang] += rows[0].btype == CROWD_BRAIN
        gangs = {gang for gang in gangs if crowd[gang] * 2 <= size[gang]}
    return frozenset(gangs)


def _fight_start(trace: FightTrace, fighters: set[int]) -> int:
    """The first step of the fight: a fighter's brain on (`brain_off` 0), else the first goal pushed, else the
    trace's first step (rule 2)."""
    by_brain = "brain_off" in trace.columns
    if not by_brain and not {"goal", "goal0", "stack"} & trace.columns:
        return trace.steps[0]
    for step in trace.steps:
        for sample in trace.by_step[step]:
            if sample.human not in fighters:
                continue
            top = sample.top if sample.top is not None else -1
            if (sample.brain_off == 0) if by_brain else top >= 0:
                return step
    return trace.steps[0]


def _rumble_over(trace: FightTrace, fighters: set[int]) -> int | None:
    """The fight's last step, RumbleOver: the update before every fighter's brain is off, or before every fighter's
    goal stack is empty for QUIET_UPDATES in a row (rule 2); None when the trace shows neither."""
    rows = {step: [s for s in trace.by_step[step] if s.human in fighters] for step in trace.steps}
    if "brain_off" in trace.columns:
        for index, step in enumerate(trace.steps):
            if index > 0 and rows[step] and all(s.brain_off == 1 for s in rows[step]):
                return trace.steps[index - 1]
        return None
    if not {"goal", "goal0", "stack"} & trace.columns:
        return None
    quiet = 0
    for index, step in enumerate(trace.steps):
        empty = bool(rows[step]) and all((s.top if s.top is not None else 0) < 0 for s in rows[step])
        quiet = quiet + 1 if empty else 0
        if quiet == QUIET_UPDATES:
            start = index - QUIET_UPDATES + 1
            return trace.steps[start - 1] if start > 0 else None
    return None


def _sides_at(
    trace: FightTrace, gangs: frozenset[int], fighters: set[int], end: int
) -> tuple[dict[int, int], set[int]]:
    """Per gang, its fighters out at step `end` (out, or gone from the trace), and the gangs with one standing."""
    standing = {s.human for s in trace.by_step.get(end, []) if s.human in fighters and not _is_ko(s)}
    out: dict[int, int] = {}
    left: set[int] = set()
    for gang in gangs:
        members = {h for h, rows in trace.by_human.items() if h in fighters and rows[0].gang == gang}
        out[gang] = len(members - standing)
        if members & standing:
            left.add(gang)
    return out, left


def analyse(
    trace: FightTrace,
    hz: int = DEFAULT_HZ,
    exclude: frozenset[int] = frozenset(),
    engage_distance: float = DEFAULT_ENGAGE_DISTANCE,
    start: int = 1,
    sides: list[int] | None = None,
) -> Analysis:
    """Work out every metric of `trace` over its fight, from step `start` at the earliest.

    The fighters are the humans of `sides` (the sides' gang ids), or of the gangs that fight (fighting_gangs());
    the humans in `exclude` (a spectator, a pad-driven player) are left out too. The fight runs from the first update
    a fighter's brain is on to RumbleOver (rules 1, 2 and 4 of docs/research/rumble.md#gang-battle-metrics); a trace
    without the columns that mark them runs from `start` to the first step where one gang or none stands.
    """
    trace = from_step(trace, start)
    gangs = frozenset(sides) if sides else fighting_gangs(trace)
    fighters = {h for h, rows in trace.by_human.items() if rows[0].gang in gangs and h not in exclude}
    trace = from_step(trace, _fight_start(trace, fighters))
    excluded = frozenset(h for h in trace.by_human if h not in fighters)
    over = _rumble_over(trace, fighters)
    out_at_end: dict[int, int] = {}
    if over is not None:
        end_step = over
        out_at_end, left = _sides_at(trace, gangs, fighters, end_step)
        winner = next(iter(left)) if len(left) == 1 else None
        known = len(left) <= 1
    else:
        end_step, winner, known = match_end(trace, excluded)
        if known:
            out_at_end, _ = _sides_at(trace, gangs, fighters, end_step)
    view = _StepView(
        positions={
            step: {s.human: (s.x, s.y) for s in rows if s.x is not None and s.y is not None}
            for step, rows in trace.by_step.items()
        },
        targets={
            step: {s.human: s.target for s in rows if s.target is not None} for step, rows in trace.by_step.items()
        },
        engage=engage_distance,
    )
    humans = {human: _analyse_human(trace, trace.by_human[human], view, end_step) for human in sorted(fighters)}
    groups: list[tuple[int, ...]] = []
    gang_rows: dict[int, list[Sample]] = defaultdict(list)
    for step in trace.steps:
        if step > end_step:
            break
        rows = [s for s in trace.by_step[step] if s.human in fighters]
        attackers: dict[int, list[int]] = defaultdict(list)
        for sample in rows:
            if not _is_ko(sample) and sample.target is not None and sample.target >= 0:
                attackers[sample.target].append(sample.gang)
        groups.extend(tuple(gangs_of_target) for gangs_of_target in attackers.values())
        for gang in gangs:
            first = next((s for s in rows if s.gang == gang), None)
            if first is not None:
                gang_rows[gang].append(first)
    return Analysis(trace, hz, end_step, winner, known, humans, groups, trace.steps[0], out_at_end, dict(gang_rows))


# --- the numbers -------------------------------------------------------------------------------------------------

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


def _rates(analysis: Analysis, members: list[HumanMetrics], minutes: float) -> dict[str, Metric]:
    """The per-standing-minute rates of a set of humans, each when the trace has the columns it needs."""
    columns = analysis.trace.columns
    metrics: dict[str, Metric] = {}

    # A rate: the members' counts of `count` per minute.
    def rate(name: str, count: str) -> None:
        metrics[name] = sum(getattr(member, count) for member in members) / minutes

    if "phase" in columns or "command" in columns:
        attacks: Counter[str] = Counter()
        for member in members:
            attacks.update(member.attacks)
        if "phase" not in columns:
            for kind, kinds in attacks.items():
                metrics[f"attack.{kind}_per_min"] = kinds / minutes
        metrics["attack.total_per_min"] = sum(attacks.values()) / minutes
    if {"goal", "goal0", "stack"} & columns:
        rate("block.per_min", "blocks")
        rate("help.respond_per_min", "help_responds")
        rate("goal.changes_per_min", "goal_changes")
        rate("goal.interruptions_per_min", "goal_interruptions")
    if {"command", "block", "blocking"} & columns:
        rate("block.held_per_min", "held_blocks")
    for column, name, count in (
        ("react", "react.per_min", "reactions"),
        ("down", "down.per_min", "knockdowns"),
        ("health", "health.losses_per_min", "health_losses"),
        ("clip", "taunt.per_min", "taunts"),
    ):
        if column in columns:
            rate(name, count)
    if "target" in columns:
        rate("target.switch_per_min", "target_switches")
        rate("target.set_per_min", "target_sets")
        rate("target.clear_per_min", "target_clears")
        changes = sum(m.target_switches + m.target_sets + m.target_clears for m in members)
        metrics["target.changes_per_min"] = changes / minutes
    if analysis.trace.target_causes is not None:
        for cause in TARGET_CAUSES:
            metrics[f"target.cause.{cause}_per_min"] = sum(m.target_causes[cause] for m in members) / minutes
    if analysis.trace.help_calls is not None:
        rate("help.calls_per_min", "help_calls")
    if analysis.trace.helpers_sent is not None:
        rate("help.helpers_per_min", "helpers_sent")
    return metrics


def _distribution(metrics: dict[str, Metric], name: str, values: list[float]) -> None:
    """Adds the 10th, 50th and 90th percentiles of `values` as `<name>p10`, `<name>p50` and `<name>p90`, when there
    are any."""
    if values:
        for label, fraction in (("p10", 0.1), ("p50", 0.5), ("p90", 0.9)):
            metrics[f"{name}{label}"] = _percentile(values, fraction)


def _active_shares(metrics: dict[str, Metric], counts: Counter[int]) -> None:
    """Adds the shares of 0, 1 and 2 active places held (and of more, `active.share3plus`) of `counts`."""
    total = sum(counts.values())
    if total:
        for value in range(3):
            metrics[f"active.share{value}"] = counts.get(value, 0) / total
        metrics["active.share3plus"] = sum(count for value, count in counts.items() if value > 2) / total


def _wait_metrics(metrics: dict[str, Metric], members: list[HumanMetrics]) -> None:
    """Adds where the waiting ones stand (rule 10): the median distance and the share in the 4-4.75 m band, for a
    target who fights the waiter (`mutual`) and one who fights someone else (`other`)."""
    for kind in ("mutual", "other"):
        values = [d for m in members for d in getattr(m, f"wait_{kind}")]
        if values:
            metrics[f"wait.{kind}_p50"] = statistics.median(values)
            metrics[f"wait.{kind}_band"] = sum(1 for d in values if WAIT_BAND[0] <= d <= WAIT_BAND[1]) / len(values)


def _group_metrics(analysis: Analysis, members: list[HumanMetrics], gangs: set[int] | None) -> dict[str, Metric]:
    """The metrics of a set of humans (a side, or all); `gangs` limits the attackers counted per target."""
    hz = analysis.hz
    metrics: dict[str, Metric] = {}
    minutes = sum(member.alive_steps for member in members) / hz / 60
    metrics["humans"] = float(len(members))
    # The goals' shares of standing time and the idle share (rules 8 and 11).
    goals: Counter[str] = Counter()
    for member in members:
        goals.update(member.goal_steps)
    goal_total = sum(goals.values())
    if goal_total:
        for name, steps in goals.items():
            metrics[f"goal.{name}"] = steps / goal_total
        metrics["idle.share"] = sum(member.idle_steps for member in members) / goal_total
    if minutes > 0:
        metrics.update(_rates(analysis, members, minutes))
    _distribution(metrics, "attack.distance_", [d for m in members for d in m.attack_distances])
    # The fight goals' lengths (rule 8).
    runs = [run / hz for member in members for run in member.fight_goals]
    if runs:
        metrics["fight_goal.count"] = float(len(runs))
        metrics["fight_goal.median_s"] = statistics.median(runs)
        metrics["fight_goal.p90_s"] = _percentile(runs, 0.9)
        metrics["fight_goal.share_4s"] = sum(1 for run in runs if run >= LONG_FIGHT_GOAL_S - 1e-9) / len(runs)
    # Fighters on one enemy and the active places (rule 9).
    sizes = [len(group if gangs is None else [g for g in group if g in gangs]) for group in analysis.attacker_groups]
    sizes = [size for size in sizes if size > 0]
    if sizes:
        metrics["attackers.mean"] = statistics.fmean(sizes)
        metrics["attackers.p90"] = _percentile([float(size) for size in sizes], 0.9)
        metrics["attackers.max"] = float(max(sizes))
        for count in range(1, 5):
            metrics[f"attackers.share{count}"] = sizes.count(count) / len(sizes)
    active: Counter[int] = Counter()
    for member in members:
        active.update(member.active)
    _active_shares(metrics, active)
    _wait_metrics(metrics, members)
    _distribution(metrics, "distance.", [d for member in members for d in member.distances])
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


def _side_metrics(analysis: Analysis, gang: int) -> dict[str, Metric]:
    """A side's own metrics (rules 4 and 12): its fighters out at the end, its tactic and when it changes, the
    team-ups allowed, the order running and the shared target."""
    metrics: dict[str, Metric] = {}
    if gang in analysis.out_at_end:
        metrics["out.at_end"] = float(analysis.out_at_end[gang])
    rows = analysis.gang_rows.get(gang, [])
    tactics = [tactic_name(row.g_tactic) for row in rows if row.g_tactic is not None]
    if tactics:
        changes = [index for index in range(1, len(tactics)) if tactics[index] != tactics[index - 1]]
        metrics["tactic.first"] = tactics[0]
        metrics["tactic.last"] = tactics[-1]
        metrics["tactic.changes"] = float(len(changes))
        if changes:
            metrics["tactic.first_updates"] = float(changes[0])
    teamups = [row.g_teamups for row in rows if row.g_teamups is not None]
    if teamups:
        metrics["teamups.max"] = float(max(teamups))
    orders = [row.g_order for row in rows if row.g_order is not None]
    if orders:
        metrics["order.share"] = sum(1 for order in orders if order >= 0) / len(orders)
    chosen = [row.g_chosen for row in rows if row.g_chosen is not None]
    if chosen:
        metrics["chosen.share"] = sum(1 for human in chosen if human >= 0) / len(chosen)
    return metrics


def gangs_of(analysis: Analysis) -> list[int]:
    """The sides' gang ids, from the lowest (the humans in no gang, -1, are not a gang)."""
    return sorted({m.gang for m in analysis.humans.values() if m.gang >= 0})


def scalars(analysis: Analysis, labels: dict[int, str] | None = None) -> dict[str, Metric]:
    """Every metric of a run as one flat map: `all.*`, `match.*` and one prefix per side (its label in `labels`).

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
        for key, value in {**_group_metrics(analysis, gang_members, {gang}), **_side_metrics(analysis, gang)}.items():
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
    """The text report of one trace: the match, each side, and with `per_human` each human."""
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
