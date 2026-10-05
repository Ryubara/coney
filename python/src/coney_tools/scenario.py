# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace scenarios: one scripted run, played on the original in PCSX2 and on Coney, and how to compare the two.

A scenario is a TOML file in `research/traces/scenarios/` (format: docs/guides/research-workflow.md#recording-a-trace):

```toml
description = "level99 street: the walk start, stick 60 % up"
research = "docs/research/feel.md"
input = "walk60.txt"                 # a Coney input script, beside the scenario
updates = 160                        # character updates to record (Coney: --frames)
fields = ["player", "camera"]        # field sets of research/traces/fields.toml

[original]
slot = 1                             # the quick-save slot to copy (read only)
patches = ["scripted-pad", "right-stick"]
let = { puppet = 'human("PoizoCiv")' }   # names resolved once, before the first update
setup = [ { address = "prec(puppet) + 0x1e", type = "u8", value = "0", frame = 0, until = 0 } ]

[coney]
level = "level99"                    # --play-level
args = []                            # more of Coney's options

[diff]
start_frame = true                   # compare in the player's frame at the first update of input
columns = ["speed", "gait", "clip"]
tolerance = { speed = 0.05 }
```

A field set is an array of tables in `research/traces/fields.toml`, each field one of:

- `{ name, address, type }`: a value read every update; `address` an expression (coney_tools.game_memory), `type`
  one of u8 s8 u16 s16 u32 s32 f32;
- `{ name, formula }`: a value worked out from the fields before it (`deg(2 * atan2(qz, qw))`).

`hidden = true` keeps a field out of the CSV (a quaternion part only a formula needs); `digits` sets a float's
decimals (4 by default). A column the original and Coney's `--trace` both have uses Coney's name and unit.
"""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools.game_memory import MATH, TYPES, Expression, ExpressionError, parse
from coney_tools.input_script import InputEvent, load_input_script

TRACES_DIR = Path("research/traces")
FIELDS_FILE = TRACES_DIR / "fields.toml"
PATCHES_FILE = TRACES_DIR / "patches.toml"


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


@dataclass(frozen=True)
class SetupWrite:
    """A write made after each sample of frames `frame` to `until`: `value` (an expression) as `type`."""

    address: Expression
    type: str
    value: Expression
    frame: int
    until: int


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
    patches: tuple[str, ...]
    let: tuple[tuple[str, Expression], ...]
    setup: tuple[SetupWrite, ...]
    coney_level: str
    coney_args: tuple[str, ...]
    diff: DiffSettings

    def first_input_step(self) -> int:
        """The step the first line of input takes effect on (frame N is step N + 1), or 1 for an empty script."""
        return self.events[0].frame + 1 if self.events else 1


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
    unknown = set(raw) - {"name", "address", "type", "formula", "hidden", "digits"}
    if unknown:
        raise ScenarioError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    if ("address" in raw) == ("formula" in raw):
        raise ScenarioError(f"{where}: give either address and type, or formula")
    digits = raw.get("digits", 4)
    if not isinstance(digits, int) or not 0 <= digits <= 9:
        raise ScenarioError(f"{where}: digits must be a whole number from 0 to 9")
    hidden = bool(raw.get("hidden", False))
    if "address" in raw:
        return Field(
            raw["name"], _expression(raw["address"], where), _type(raw.get("type"), where), None, hidden, digits
        )
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
    known = {"description", "research", "input", "updates", "fields", "field", "original", "coney", "diff"}
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
    let = tuple((str(k), _expression(v, f"{where} let {k}")) for k, v in original.get("let", {}).items())
    setup = tuple(_setup(item, f"{where} setup {i + 1}") for i, item in enumerate(original.get("setup", [])))
    coney = data.get("coney", {})
    return Scenario(
        path=path,
        description=str(data.get("description", "")),
        input_path=input_path,
        events=events,
        updates=data["updates"],
        fields=tuple(fields),
        slot=slot,
        patches=tuple(str(p) for p in original.get("patches", [])),
        let=let,
        setup=setup,
        coney_level=str(coney.get("level", "")),
        coney_args=tuple(str(a) for a in coney.get("args", [])),
        diff=_diff(data.get("diff", {}), where),
    )
