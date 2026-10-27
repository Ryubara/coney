# SPDX-License-Identifier: GPL-3.0-or-later
"""Reading an all-humans trace of either game: one row per human per update, and the hook logs beside it.

The trace is CSV with the columns `step`, `human` (a number per human) and `gang`; the others are optional, and a
metric whose columns are missing is left out of the report (coney_tools.fight_report):

* `x`, `y` (metres): the distance to the target.
* `clip`, `state`, `state_hi`, `health` (and `ko`): whether a human is out of the fight. With a `clip` or `state`
  column (the gang-battle field set) a human is out in clip 349 or with `Human_IsDownOrDead`'s mask: state
  `0x80050000` (dying, knocked out, wounded) or `state_hi` `0x1` (dead); otherwise when `ko` is 1 or `health` is 0
  or less.
* `phase` (attacks are wind-up starts) or `command` (an attack is a change to an attack command, ATTACK_COMMANDS).
* `blocking` (or `block`): 1 while blocking (the block command, 4, counts too).
* `goal` (the top goal's type id; -1 for none) and the goal stack, as `goal0`, `goal1`, ... (the type ids from the
  bottom, -1 above the top) or `stack` (the ids joined with `>`).
* `target`: the number of the human it fights (-1 for none).
* the gang-battle columns of `research/traces/fields.toml` (`[[humans]]`): `btype`, `down`, `react`, `active`,
  `fight_updates`, `fight_active`, `g_tactic`, `g_teamups`, `g_order`, `g_chosen`, `brain_off`; and Coney's
  `help_calls`, `helpers_sent` (counts so far) and `target_causes` (target changes so far by cause), which stand for
  the original's `help-call`, `send-helper` and `set-target` hook logs `<trace stem>.<hook>.csv`.

Research: docs/research/rumble.md#gang-battle-metrics
"""

from __future__ import annotations

import csv
import math
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import yaml

from coney_tools.events import read_hook_logs

#: A metric's value: a number, or a name (a winner, a tactic).
Metric = float | str

#: The attack commands (combat::command, docs/research/combat/index.md#commands) by the name a report shows.
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

#: The goal type ids (src/ai/goal.h, docs/research/ai/index.md#goals) by the name a report shows.
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

#: The tactics the gang-battle recordings name, by the original's vtable (docs/research/gang-fights.md#runtime rule 1);
#: Coney's trace writes 1 for a tactic the research names no vtable for.
TACTIC_NAMES = {0: "none", 1: "other", 0x00543740: "confront", 0x00543320: "attack", 0x00544160: "warrior_attack"}

#: The clip a knocked-out fighter lies in (`ANIM_DEAD_IDLE`, docs/research/rumble.md#gang-battle-results).
DEAD_IDLE_CLIP = 349
#: The state bits that put a human out of the fight, the low half of `Human_IsDownOrDead`'s mask `0x180050000`
#: (docs/research/rumble.md#gang-battle-metrics rule 3): dying (`0x80000000`), knocked out (`0x40000`) and wounded
#: (`0x10000`); and of the high half (`state_hi`), dead (`0x1`).
OUT_STATES = 0x80000000 | 0x40000 | 0x10000
OUT_STATES_HIGH = 0x1

#: The causes target changes are split by (rule 7, docs/research/gang-fights.md#set-target-causes), as Coney's
#: `target_causes` column names them (ai::TargetCause); `unknown` is a caller of the original's the table below does
#: not name yet.
TARGET_CAUSES = ("pick", "front", "hit_back", "help", "engage", "displaced", "reaction", "order", "other", "unknown")

#: The `set-target` hook's callers and their causes, the table of docs/research/gang-fights.md#set-target-causes:
#: (`ra`, `ra2`) to a cause, `ra2` None for a row that holds whatever `ra2` is (`ra2` names a cause only where the
#: caller is a wrapper that saved its own return address at `0(sp)`). A row for a new caller is one line here.
#: The page's bucket list gives the rows the rule-7 paragraph first left out: the grab goals `reaction`,
#: AttackAction_Start `hit_back`, the item sites and the reset `order`, Spectate `pick`. The rows the page gives no
#: bucket (Gang_AddMember, the teardown drop, the sync copy) stay `other`.
SET_TARGET_CAUSES: dict[tuple[int, int | None], str] = {
    (0x0029F298, None): "pick",  # Brain_PickBestEnemy's clear
    (0x0029F368, None): "pick",  # Brain_PickBestEnemy's set
    (0x002B3CA8, None): "front",  # FightGoal_Process: the man in front
    (0x00305220, None): "hit_back",  # GangBrain_OnEvent, event 1
    (0x0028D330, 0x00305200): "hit_back",  # Brain_Fight from GangBrain_OnEvent
    (0x0028D330, 0x00291C6C): "help",  # Brain_Fight from Brain_OnViolenceSeen
    (0x00291C80, None): "help",  # Brain_OnViolenceSeen
    (0x002AFCD0, None): "engage",  # EngageEnemyGoal_Process
    (0x0028E0FC, None): "displaced",  # Brain_ClaimAttackSlot
    (0x002B4BDC, None): "reaction",  # StunnedGoal_Process
    (0x002B52FC, None): "reaction",  # WoundedGoal_Process
    (0x00226FD8, 0x0028FB30): "reaction",  # Human_DropTarget from Brain_MaybeSpotPlayers: his own state
    (0x00226FD8, 0x0028C414): "reaction",  # Human_DropTarget from Brain_OnKnockedOut
    (0x00226FD8, 0x0030678C): "order",  # Human_DropTarget from Tactic_Start
    (0x00226FD8, 0x002ADE80): "order",  # Human_DropTarget from MeleeGoal_End
    (0x00226FD8, 0x002DC884): "order",  # Human_DropTarget from GetItemGoal_Start
    (0x00226FD8, 0x0028C564): "other",  # Human_DropTarget from Brain_TearDown
    (0x002FAD04, None): "hit_back",  # AttackAction_Start: mostly the answer to the man who attacked him
    (0x002DCE08, None): "order",  # GetItemGoal_Process: the item fight
    (0x002DCB08, None): "order",  # GetItemGoal_Process: the item's holder
    (0x002B437C, None): "pick",  # SpectateGoal_Process: Melee found no target
    (0x002B4310, None): "pick",  # SpectateGoal_End
    (0x00166370, None): "other",  # Gang_AddMember
    (0x00226CBC, 0x00226F5C): "other",  # Human_SetTarget from Human_SyncTargetFromBrain: never a change
    (0x00292DFC, None): "order",  # Brain_DefaultOnEvent, event 2 (the reset)
    (0x002B6160, None): "reaction",  # GrabbingGoal_Process
    (0x002B6974, None): "reaction",  # GrabbedGoal_Process
    (0x002B69E8, None): "reaction",  # GrabbedGoal_Process
    # Rows not seen in the five recordings (the review of the page):
    (0x002AEC88, None): "pick",  # MeleeGoal_Process clears: no attack slot, threat response 0
    (0x002BC278, None): "pick",  # FollowAndAttackGoal_Process clears: the pick beyond 60 m of the leader
    (0x002B4AF4, None): "reaction",  # GroundedGoal_Process clears: the target hidden in shadow
    (0x0030797C, None): "help",  # TacticAttack's event 20: the side picked
    (0x002B66CC, None): "reaction",  # MountingGoal_Process: the mount partner
    (0x002B6CB8, None): "reaction",  # MountedGoal_Process: the mount partner
    (0x002BB908, None): "reaction",  # GrabTargetGoal_Process
}
#: For a caller whose `ra2` was not logged, its cause by the force flag: `Brain_Fight` forced is a hit back, unforced a
#: help response (docs/research/gang-fights.md#set-target-causes).
SET_TARGET_CAUSES_BY_FORCE: dict[int, tuple[str, str]] = {0x0028D330: ("help", "hit_back")}


def set_target_cause(ra: int, ra2: int | None, force: int | None) -> str:
    """The cause of a `set-target` hook call by SET_TARGET_CAUSES: the row of (`ra`, `ra2`), else of (`ra`, any);
    with no `ra2` logged (None), SET_TARGET_CAUSES_BY_FORCE by `force`; `unknown` for a caller no row names."""
    if ra2 is not None and (ra, ra2) in SET_TARGET_CAUSES:
        return SET_TARGET_CAUSES[(ra, ra2)]
    if (ra, None) in SET_TARGET_CAUSES:
        return SET_TARGET_CAUSES[(ra, None)]
    if ra2 is None and force is not None and ra in SET_TARGET_CAUSES_BY_FORCE:
        return SET_TARGET_CAUSES_BY_FORCE[ra][1 if force != 0 else 0]
    return "unknown"


# Where the original keeps its humans and brains, to name a hook log's pointer by its human's index
# (research/traces/patches/gang-battle.toml).
_HUMANS_BASE, _HUMAN_SIZE = 0x00640C80, 0x6D0
_BRAINS_BASE, _BRAIN_SIZE = 0x006D53F0, 0x2F0


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


def tactic_name(value: int) -> str:
    """A `g_tactic` value (the tactic's vtable) as the report shows it."""
    return TACTIC_NAMES.get(value, f"tactic_{value:#010x}")


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
    btype: int | None = None
    health: float | None = None
    clip: int | None = None
    phase: int | None = None
    down: int | None = None
    react: int | None = None
    active: int | None = None
    fight_updates: int | None = None
    fight_active: int | None = None
    brain_off: int | None = None
    g_tactic: int | None = None
    g_teamups: int | None = None
    g_order: int | None = None
    g_chosen: int | None = None

    @property
    def top(self) -> int | None:
        """The top goal's id (-1 for none): `goal`, or the stack's top."""
        if self.goal is not None:
            return self.goal
        if self.stack is not None:
            return self.stack[-1] if self.stack else -1
        return None


@dataclass
class FightTrace:
    """A trace's rows by human (in step order) and by step, and its help calls and helpers sent as (step, human)."""

    name: str
    columns: frozenset[str]
    by_human: dict[int, list[Sample]]
    by_step: dict[int, list[Sample]]
    help_calls: list[tuple[int, int]] | None = None
    helpers_sent: list[tuple[int, int]] | None = None
    #: Its target changes as (step, human, cause), from Coney's `target_causes` or the `set-target` hook log.
    target_causes: list[tuple[int, int, str]] | None = None
    #: The steps in order.
    steps: list[int] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.steps = sorted(self.by_step)

    @property
    def last_step(self) -> int:
        """The last step with a row."""
        return self.steps[-1]


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


def _out(row: dict[str, str], columns: frozenset[str], health: float | None) -> bool | None:
    """Whether a row's human is out of the fight (rule 3 of docs/research/rumble.md#gang-battle-metrics): in the
    gang-battle field set by its clip or state, else by `ko` or its health."""
    if "clip" in columns or "state" in columns:
        clip, state, high = _whole(row.get("clip")), _whole(row.get("state")), _whole(row.get("state_hi"))
        return (
            clip == DEAD_IDLE_CLIP
            or (state is not None and state & OUT_STATES != 0)
            or (high is not None and high & OUT_STATES_HIGH != 0)
        )
    if "ko" in columns:
        flag = _whole(row.get("ko"))
        return None if flag is None else flag != 0
    return None if health is None else health <= 0


def _counted(by_human: dict[int, list[Sample]], counts: dict[tuple[int, int], int]) -> list[tuple[int, int]]:
    """The events a cumulative count column (Coney's `help_calls`, `helpers_sent`) shows, one per unit it rises."""
    events: list[tuple[int, int]] = []
    for human, samples in by_human.items():
        before = 0
        for sample in samples:
            now = counts.get((sample.step, human), before)
            events.extend((sample.step, human) for _ in range(max(0, now - before)))
            before = now
    return events


def _causes_cell(cell: str | None) -> dict[str, int]:
    """A `target_causes` cell (`pick=3;front=1`; empty for none) as each cause's count."""
    counts: dict[str, int] = {}
    for part in (cell or "").split(";"):
        name, _, count = part.strip().partition("=")
        value = _whole(count)
        if name and value is not None:
            counts[name] = value
    return counts


def _counted_causes(
    by_human: dict[int, list[Sample]], counts: dict[tuple[int, int], dict[str, int]]
) -> list[tuple[int, int, str]]:
    """The target changes Coney's cumulative `target_causes` column shows, one (step, human, cause) per unit a cause's
    count rises."""
    events: list[tuple[int, int, str]] = []
    for human, samples in by_human.items():
        before: dict[str, int] = {}
        for sample in samples:
            now = counts.get((sample.step, human), before)
            for cause, count in now.items():
                events.extend((sample.step, human, cause) for _ in range(max(0, count - before.get(cause, 0))))
            before = now
    return events


def parse_fight_trace(text: str, name: str = "trace") -> FightTrace:
    """Read an all-humans trace from CSV `text`. Raises FightTraceError when it lacks `step`, `human` or `gang`."""
    reader = csv.DictReader(text.splitlines())
    columns = frozenset(reader.fieldnames or [])
    missing = [column for column in ("step", "human", "gang") if column not in columns]
    if missing:
        raise FightTraceError(f"{name}: no {', '.join(missing)} column: not an all-humans trace")
    by_human: dict[int, list[Sample]] = defaultdict(list)
    by_step: dict[int, list[Sample]] = defaultdict(list)
    calls: dict[tuple[int, int], int] = {}
    helpers: dict[tuple[int, int], int] = {}
    causes: dict[tuple[int, int], dict[str, int]] = {}
    for line, row in enumerate(reader, start=2):
        step, human, gang = _whole(row["step"]), _whole(row["human"]), _whole(row["gang"])
        if step is None or human is None or gang is None:
            raise FightTraceError(f"{name}: line {line}: step, human and gang must be whole numbers")
        if (step, human) in calls:  # `calls` has a key for every row read so far
            raise FightTraceError(f"{name}: line {line}: human {human} twice at step {step}")
        health = _number(row.get("health"))
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
            ko=_out(row, columns, health),
            command=_whole(row.get("command")),
            block=block,
            goal=_whole(row.get("goal")),
            stack=_stack(row.get("stack")) if "stack" in columns else _stack_columns(row, columns),
            target=_whole(row.get("target")),
            btype=_whole(row.get("btype")),
            health=health,
            clip=_whole(row.get("clip")),
            phase=_whole(row.get("phase")),
            down=_whole(row.get("down")),
            react=_whole(row.get("react")),
            active=_whole(row.get("active")),
            fight_updates=_whole(row.get("fight_updates")),
            fight_active=_whole(row.get("fight_active")),
            brain_off=_whole(row.get("brain_off")),
            g_tactic=_whole(row.get("g_tactic")),
            g_teamups=_whole(row.get("g_teamups")),
            g_order=_whole(row.get("g_order")),
            g_chosen=_whole(row.get("g_chosen")),
        )
        by_human[human].append(sample)
        by_step[step].append(sample)
        calls[(step, human)] = _whole(row.get("help_calls")) or 0
        helpers[(step, human)] = _whole(row.get("helpers_sent")) or 0
        causes[(step, human)] = _causes_cell(row.get("target_causes"))
    if not by_step:
        raise FightTraceError(f"{name}: no rows")
    for samples in by_human.values():
        samples.sort(key=lambda sample: sample.step)
    trace = FightTrace(name, columns, dict(by_human), dict(by_step))
    if "help_calls" in columns:
        trace.help_calls = _counted(trace.by_human, calls)
    if "helpers_sent" in columns:
        trace.helpers_sent = _counted(trace.by_human, helpers)
    if "target_causes" in columns:
        trace.target_causes = _counted_causes(trace.by_human, causes)
    return trace


def _hook_events(
    rows: list[dict[str, str]] | None, name: str, column: str, base: int, size: int
) -> list[tuple[int, int]] | None:
    """The (step, human) of each call in the hook log `rows` (named `name` in errors), its `column` a pointer into the
    array at `base` of records of `size` bytes; None when there is no such log. Raises FightTraceError on a bad row."""
    if rows is None:
        return None
    events = []
    for row in rows:
        step, pointer = _whole(row.get("step")), _whole(row.get(column))
        if step is None or pointer is None:
            raise FightTraceError(f"{name}: a row without a step or {column}")
        if pointer >= base and (pointer - base) % size == 0:
            events.append((step, (pointer - base) // size))
    return events


def _set_target_events(rows: list[dict[str, str]] | None, name: str) -> list[tuple[int, int, str]] | None:
    """The target changes in the `set-target` hook log `rows` (columns `step`, `ra`, `brain`, `human`, `force` and,
    when logged, `ra2`; named `name` in errors) as (step, human, cause): only the calls that change the brain's target
    (a brain's target is none until the log sets it), each named by set_target_cause(); None when there is no such
    log. Raises FightTraceError on a bad row."""
    if rows is None:
        return None
    events = []
    current: dict[int, int] = {}
    for row in rows:
        step, ra, brain = _whole(row.get("step")), _whole(row.get("ra")), _whole(row.get("brain"))
        target = _whole(row.get("human"))
        if step is None or ra is None or brain is None or target is None:
            raise FightTraceError(f"{name}: a row without a step, ra, brain or human")
        if current.get(brain, 0) == target:
            continue
        current[brain] = target
        if brain >= _BRAINS_BASE and (brain - _BRAINS_BASE) % _BRAIN_SIZE == 0:
            cause = set_target_cause(ra, _whole(row.get("ra2")), _whole(row.get("force")))
            events.append((step, (brain - _BRAINS_BASE) // _BRAIN_SIZE, cause))
    return events


def load_fight_trace(path: Path) -> FightTrace:
    """Read an all-humans trace file and, for the original's, its hook logs beside it (events.read_hook_logs()):
    `<stem>.help-call.csv`, `<stem>.send-helper.csv` (the caller a human pointer, the helper a brain pointer) and
    `<stem>.set-target.csv` (each target change and its caller). Raises FightTraceError when it cannot be read."""
    try:
        text = path.read_text(encoding="utf-8")
        logs = read_hook_logs(path, ("help-call", "send-helper", "set-target"))
    except OSError as error:
        raise FightTraceError(f"{path}: {error}") from error
    trace = parse_fight_trace(text, str(path))

    def log(hook: str) -> tuple[list[dict[str, str]] | None, str]:
        # A hook's rows (None when it has no file) and its file name for errors.
        return logs.get(hook), str(path.with_name(f"{path.stem}.{hook}.csv"))

    if trace.help_calls is None:
        trace.help_calls = _hook_events(*log("help-call"), "human", _HUMANS_BASE, _HUMAN_SIZE)
    if trace.helpers_sent is None:
        trace.helpers_sent = _hook_events(*log("send-helper"), "a0", _BRAINS_BASE, _BRAIN_SIZE)
    if trace.target_causes is None:
        trace.target_causes = _set_target_events(*log("set-target"))
    return trace


def from_step(trace: FightTrace, start: int) -> FightTrace:
    """`trace` without the steps before `start` (an intro, a countdown); times are then counted from `start`."""
    if start <= trace.steps[0]:
        return trace
    by_step = {step: rows for step, rows in trace.by_step.items() if step >= start}
    if not by_step:
        raise FightTraceError(f"{trace.name}: no rows from step {start}")
    by_human: dict[int, list[Sample]] = defaultdict(list)
    for step in sorted(by_step):
        for sample in by_step[step]:
            by_human[sample.human].append(sample)
    return FightTrace(
        trace.name, trace.columns, dict(by_human), by_step, trace.help_calls, trace.helpers_sent, trace.target_causes
    )
