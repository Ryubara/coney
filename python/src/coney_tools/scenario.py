# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace scenarios: one scripted run, played on the original in PCSX2 and on Coney, and how to compare the two.

A scenario is a TOML file in `research/traces/scenarios/` (format: docs/guides/research-traces.md#recording-a-trace):

```toml
description = "level99 street: the walk start, stick 60 % up"
research = "docs/research/feel.md"
input = "walk60.txt"                 # a Coney input script, beside the scenario
updates = 160                        # character updates to record (Coney: --frames)
fields = ["player", "camera"]        # field sets of research/traces/fields.toml

[original]
slot = 1                             # the quick-save slot to copy (read only), or:
# state = "states/my-save.p2s"       # a state file of your own, under scratch_dir (never committed)
patches = ["scripted-pad", "right-stick"]
let = { puppet = 'human("PoizoCiv")' }   # names resolved once, before the first update
setup = [ { address = "prec(puppet) + 0x1e", type = "u8", value = "0", frame = 0, until = 0 } ]
calls = [ { function = "0x002b2b90", args = ["u32(puppet + 0x90)", "u32(player + 0x90)"], frame = 2 } ]
speed = 1.0                          # PCSX2's emulation speed while recording (0.5: half; the game sees no change)

[coney]
level = "level99"                    # --play-level
args = []                            # more of Coney's options
settle = 0                           # updates Coney plays first with no input, dropped from its trace

[diff]
start_frame = true                   # compare in the player's frame at the first update of input
columns = ["speed", "gait", "clip"]
tolerance = { speed = 0.05 }
```

A field set is an array of tables in `research/traces/fields.toml`, each field one of:

- `{ name, address, type }`: a value read every update; `address` an expression (coney_tools.game_memory), `type`
  one of u8 s8 u16 s16 u32 s32 f32;
- `{ name, formula }`: a value worked out from the fields before it (`deg(2 * atan2(qz, qw))`);
- `{ name, value }`: an expression over memory worked out at every sample, whose result is the column
  (`handles(brain(h) + 0x164, 16)`, `hidx(u32(brain(h) + 0x124))`).

`rows = "humans"` makes one row per human in use per update instead of one per update: the columns are `step`,
`human` (its handle index) and the fields, with the name `h` standing for the row's human
(research/traces/fields.toml's `humans` set). The goal type ids of `goal_type` and `goal_at` come from
research/references/goal-types.yaml.

`hidden = true` keeps a field out of the CSV (a quaternion part only a formula needs); `digits` sets a float's
decimals (4 by default). An address is worked out once, before the first update, unless `follow = true`: then it is
worked out again at every sample, for a chain of pointers that changes as the game plays (the top of an animation
stack); such a field costs a read per pointer in the chain, outside the sample's batch.
A column the original and Coney's `--trace` both have uses Coney's name and unit.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

import yaml

from coney_tools.game_memory import MATH, TYPES, Expression, ExpressionError, parse
from coney_tools.input_script import InputEvent, load_input_script

TRACES_DIR = Path("research/traces")
FIELDS_FILE = TRACES_DIR / "fields.toml"
PATCHES_DIR = TRACES_DIR / "patches"  # one TOML file per research pass: state patches and hooks
GOAL_TYPES_FILE = Path("research/references/goal-types.yaml")


class ScenarioError(Exception):
    """A scenario or field file that cannot be used; the message names the file and the problem."""


@dataclass(frozen=True)
class Field:
    """One column of the original's trace: read at `address` as `type`, or worked out by `formula`."""

    name: str
    address: Expression | None = None
    type: str = ""
    formula: Expression | None = None
    hidden: bool = False
    digits: int = 4
    follow: bool = False
    #: An expression worked out from memory at every sample, the column's value (no address or formula).
    value: Expression | None = None


@dataclass(frozen=True)
class SetupWrite:
    """A write made after each sample of frames `frame` to `until`: `value` (an expression) as `type`."""

    address: Expression
    type: str
    value: Expression
    frame: int
    until: int


@dataclass(frozen=True)
class CallSpec:
    """A call of one of the game's functions, made after the sample of frame `frame` through a call hook
    (coney_tools.hooks): `function` and each of `args` are expressions worked out then."""

    function: Expression
    args: tuple[Expression, ...]
    frame: int


@dataclass(frozen=True)
class DiffSettings:
    """How a scenario's two traces are compared (see coney_tools.trace_diff)."""

    columns: tuple[str, ...] = ()
    tolerance: dict[str, float] = field(default_factory=dict)
    start_frame: bool = False
    shift: int = 0


@dataclass(frozen=True)
class Scenario:
    """A parsed scenario with its input script and fields."""

    path: Path
    description: str
    input_path: Path
    events: list[InputEvent]
    updates: int
    fields: tuple[Field, ...]
    slot: int | None
    state: str | None
    patches: tuple[str, ...]
    let: tuple[tuple[str, Expression], ...]
    setup: tuple[SetupWrite, ...]
    calls: tuple[CallSpec, ...]
    coney_level: str
    coney_args: tuple[str, ...]
    diff: DiffSettings
    #: Updates Coney plays before the script starts, where a level's opening holds the pad (they are dropped from the
    #: trace and the script is shifted by them), so a scenario can start where the original's slot does.
    coney_settle: int = 0
    #: "updates" (one row per update) or "humans" (one row per human in use per update, `h` the row's human).
    rows: str = "updates"
    #: Goal vtables to type ids, for goal_type() and goal_at() (research/references/goal-types.yaml).
    goal_types: dict[int, int] = field(default_factory=dict)
    #: PCSX2's emulation speed while `trace original` runs it (its NominalScalar; 1.0 full speed). Below 1 a sample
    #: that takes longer than the 16 ms between two ticks still fits in one update; the game's own time is the same.
    speed: float = 1.0

    def first_input_step(self) -> int:
        """The step the first line of input takes effect on (frame N is step N + 1), or 1 for an empty script."""
        return self.events[0].frame + 1 if self.events else 1


def _settle(value: object, where: str) -> int:
    """The `[coney] settle` count: a whole number of updates, not negative."""
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ScenarioError(f"{where}: [coney] settle must be a whole number of updates, 0 or more")
    return value


def _load_toml(path: Path) -> dict[str, Any]:
    """A TOML file as a dict. Raises ScenarioError naming the file."""
    try:
        with path.open("rb") as handle:
            return tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError, UnicodeDecodeError) as error:
        raise ScenarioError(f"{path}: {error}") from error


def _expression(text: object, where: str) -> Expression:
    """Parse an expression from a TOML value. Raises ScenarioError naming where it is."""
    if not isinstance(text, str):
        raise ScenarioError(f"{where}: must be a string expression")
    try:
        return parse(text)
    except ExpressionError as error:
        raise ScenarioError(f"{where}: {error}") from error


def _type(value: object, where: str) -> str:
    """A memory type name. Raises ScenarioError for an unknown one."""
    if value not in TYPES:
        raise ScenarioError(f"{where}: type must be one of {', '.join(TYPES)}")
    return str(value)


def parse_field(raw: object, where: str) -> Field:
    """One field from its TOML table."""
    if not isinstance(raw, dict) or not isinstance(raw.get("name"), str) or not raw["name"]:
        raise ScenarioError(f"{where}: a field needs a name")
    where = f"{where} ({raw['name']})"
    unknown = set(raw) - {"name", "address", "type", "formula", "value", "hidden", "digits", "follow"}
    if unknown:
        raise ScenarioError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    if sum(key in raw for key in ("address", "formula", "value")) != 1:
        raise ScenarioError(f"{where}: give either address and type, or formula, or value")
    digits = raw.get("digits", 4)
    if not isinstance(digits, int) or not 0 <= digits <= 9:
        raise ScenarioError(f"{where}: digits must be a whole number from 0 to 9")
    hidden = bool(raw.get("hidden", False))
    follow = bool(raw.get("follow", False))
    if "value" in raw:
        if follow or "type" in raw:
            raise ScenarioError(f"{where}: a value is worked out at every sample and has no type")
        return Field(raw["name"], None, "", None, hidden, digits, value=_expression(raw["value"], where))
    if "address" in raw:
        address = _expression(raw["address"], where)
        return Field(raw["name"], address, _type(raw.get("type"), where), None, hidden, digits, follow)
    if follow:
        raise ScenarioError(f"{where}: follow is for an address, not a formula")
    return Field(raw["name"], None, "", _expression(raw["formula"], where), hidden, digits)


def check_fields(fields: list[Field], where: str) -> None:
    """Refuse a name used twice, or a formula that uses a name no earlier field or math function gives."""
    known: set[str] = set()
    for item in fields:
        if item.name in known:
            raise ScenarioError(f"{where}: field {item.name} is given twice")
        if item.formula is not None:
            missing = item.formula.names() - known - set(MATH)
            if missing:
                raise ScenarioError(
                    f"{where}: field {item.name} uses {', '.join(sorted(missing))}, not a field before it"
                )
        known.add(item.name)


def load_field_sets(path: Path) -> dict[str, list[Field]]:
    """Every field set of a fields file, by name."""
    data = _load_toml(path)
    sets: dict[str, list[Field]] = {}
    for name, raw in data.items():
        if not isinstance(raw, list):
            raise ScenarioError(f"{path}: {name} must be an array of tables ([[{name}]])")
        sets[name] = [parse_field(item, f"{path}: {name} field {i + 1}") for i, item in enumerate(raw)]
    return sets


def _setup(raw: object, where: str) -> SetupWrite:
    """One setup write from its TOML table."""
    if not isinstance(raw, dict):
        raise ScenarioError(f"{where}: must be a table")
    unknown = set(raw) - {"address", "type", "value", "frame", "until"}
    if unknown:
        raise ScenarioError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    frame = raw.get("frame", 0)
    until = raw.get("until", frame)
    if not isinstance(frame, int) or not isinstance(until, int) or not 0 <= frame <= until:
        raise ScenarioError(f"{where}: frame and until must be whole numbers with 0 <= frame <= until")
    return SetupWrite(
        _expression(raw.get("address"), f"{where} address"),
        _type(raw.get("type"), where),
        _expression(str(raw.get("value", "")), f"{where} value"),
        frame,
        until,
    )


def _call(raw: object, where: str) -> CallSpec:
    """One call from its TOML table."""
    if not isinstance(raw, dict):
        raise ScenarioError(f"{where}: must be a table")
    unknown = set(raw) - {"function", "args", "frame"}
    if unknown:
        raise ScenarioError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    frame = raw.get("frame", 0)
    args = raw.get("args", [])
    if not isinstance(frame, int) or frame < 0:
        raise ScenarioError(f"{where}: frame must be a whole number from 0")
    if not isinstance(args, list) or len(args) > 6:
        raise ScenarioError(f"{where}: args must be a list of at most six expressions")
    return CallSpec(
        _expression(str(raw.get("function", "")), f"{where} function"),
        tuple(_expression(str(arg), f"{where} argument {k + 1}") for k, arg in enumerate(args)),
        frame,
    )


def _diff(raw: object, where: str) -> DiffSettings:
    """The [diff] table."""
    if not isinstance(raw, dict):
        raise ScenarioError(f"{where}: [diff] must be a table")
    unknown = set(raw) - {"columns", "tolerance", "start_frame", "shift"}
    if unknown:
        raise ScenarioError(f"{where}: [diff] has unknown key(s) {', '.join(sorted(unknown))}")
    tolerance = raw.get("tolerance", {})
    if not isinstance(tolerance, dict) or not all(isinstance(v, int | float) for v in tolerance.values()):
        raise ScenarioError(f"{where}: [diff] tolerance must map columns to numbers")
    return DiffSettings(
        tuple(str(c) for c in raw.get("columns", [])),
        {str(k): float(v) for k, v in tolerance.items()},
        bool(raw.get("start_frame", False)),
        int(raw.get("shift", 0)),
    )


def load_scenario(path: Path, root: Path) -> Scenario:
    """Read the scenario at `path`, with the field sets of `root`'s research/traces/fields.toml.

    Raises ScenarioError for a missing key, an unknown field set, a bad expression or a bad input script.
    """
    data = _load_toml(path)
    where = str(path)
    known = {"description", "research", "input", "updates", "rows", "fields", "field", "original", "coney", "diff"}
    unknown = set(data) - known
    if unknown:
        raise ScenarioError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    if not isinstance(data.get("input"), str) or not isinstance(data.get("updates"), int) or data["updates"] < 1:
        raise ScenarioError(f"{where}: needs input (a script beside it) and updates (a whole number above 0)")
    input_path = path.parent / data["input"]
    try:
        events = load_input_script(input_path)
    except Exception as error:
        raise ScenarioError(f"{where}: {error}") from error

    # The columns: the named sets, then the scenario's own fields.
    sets = load_field_sets(root / FIELDS_FILE)
    fields: list[Field] = []
    for name in data.get("fields", []):
        if name not in sets:
            raise ScenarioError(f"{where}: unknown field set {name!r}; known: {', '.join(sorted(sets))}")
        fields += sets[name]
    fields += [parse_field(item, f"{where} field {i + 1}") for i, item in enumerate(data.get("field", []))]
    check_fields(fields, where)

    original = data.get("original", {})
    slot = original.get("slot")
    if slot is not None and (not isinstance(slot, int) or slot < 0):
        raise ScenarioError(f"{where}: [original] slot must be a whole number")
    state = original.get("state")
    if state is not None and (not isinstance(state, str) or not state or Path(state).anchor):
        raise ScenarioError(f"{where}: [original] state must be a path relative to scratch_dir")
    if state is not None and slot is not None:
        raise ScenarioError(f"{where}: [original] names both a slot and a state; give one")
    let = tuple((str(k), _expression(v, f"{where} let {k}")) for k, v in original.get("let", {}).items())
    setup = tuple(_setup(item, f"{where} setup {i + 1}") for i, item in enumerate(original.get("setup", [])))
    calls = tuple(_call(item, f"{where} call {i + 1}") for i, item in enumerate(original.get("calls", [])))
    if len({call.frame for call in calls}) != len(calls):
        raise ScenarioError(f"{where}: one call per frame (the call block holds one)")
    speed = original.get("speed", 1.0)
    if isinstance(speed, bool) or not isinstance(speed, int | float) or not 0.05 <= speed <= 1.0:
        raise ScenarioError(f"{where}: [original] speed must be a number from 0.05 to 1.0")
    rows = data.get("rows", "updates")
    if rows not in ("updates", "humans"):
        raise ScenarioError(f'{where}: rows must be "updates" or "humans"')
    coney = data.get("coney", {})
    return Scenario(
        path=path,
        description=str(data.get("description", "")),
        input_path=input_path,
        events=events,
        updates=data["updates"],
        fields=tuple(fields),
        slot=slot,
        state=state,
        patches=tuple(str(p) for p in original.get("patches", [])),
        let=let,
        setup=setup,
        calls=calls,
        coney_level=str(coney.get("level", "")),
        coney_args=tuple(str(a) for a in coney.get("args", [])),
        diff=_diff(data.get("diff", {}), where),
        coney_settle=_settle(coney.get("settle", 0), where),
        rows=rows,
        goal_types=load_goal_types(root / GOAL_TYPES_FILE),
        speed=float(speed),
    )


def with_let(scenario: Scenario, overrides: list[str]) -> Scenario:
    """`scenario` with `let` names given or replaced by `NAME=EXPRESSION` texts (`trace original --let`), so one
    scenario can be recorded with different values (a random stream's offset per match). Raises ScenarioError for a
    text that is not NAME=EXPRESSION."""
    let = dict(scenario.let)
    for text in overrides:
        name, sep, expression = text.partition("=")
        name = name.strip()
        if not sep or not name.isidentifier():
            raise ScenarioError(f"--let {text!r}: expected NAME=EXPRESSION")
        let[name] = _expression(expression.strip(), f"--let {name}")
    return replace(scenario, let=tuple(let.items()))


def load_goal_types(path: Path) -> dict[int, int]:
    """Goal vtables to type ids from the goal types reference list; empty when the file is not there."""
    if not path.is_file():
        return {}
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ScenarioError(f"{path}: {error}") from error
    return {int(entry["vtable"]): int(entry["id"]) for entry in data.get("entries", []) if "vtable" in entry}
