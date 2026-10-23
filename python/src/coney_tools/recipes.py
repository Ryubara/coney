# SPDX-License-Identifier: GPL-3.0-or-later
"""PCSX2 recipes: the shared, verified ways to do basic things in the running original (find a human, move one, write
a field, call one of the game's functions), so no engineer has to work an address out again.

A recipe lives in `research/pcsx2-recipes.toml` as a table `[recipe.NAME]`: what it does, its parameters, the steps a
runner makes over PINE (and the PCSX2-MCP fork's debug server), its preconditions, the evidence for it and the Ghidra
address that justifies it, and when it was last confirmed at runtime. `coney-tools pcsx2 recipe run` makes the steps;
`recipe render` writes the recipe pages (docs/guides/pcsx2-recipes.md, the index, and one page per group in
docs/guides/pcsx2-recipes/) from the same file.

```toml
[recipe.teleport]
summary = "Move a human to x, y, z and turn it to a heading."
how = "Prose for the recipe's section: the technique and why it works."
research = ["docs/research/characters.md#the-human-object"]
ghidra = ["0x00390180 Handle_Resolve"]
evidence = "confirmed-runtime"
patches = []                    # patches or hooks of research/traces/patches/ the running state must hold
emulation = "either"            # "running", "paused" or "either"
state = "pedestrian-grab"       # optional: the PCSX2 state library state it starts from (`pcsx2 states use`)
confirmed_runtime = { date = "2026-10-21", copy = "pcsx2-d", pcsx2 = "2.9.129", state = "level99 street" }
params = [ { name = "who", kind = "text", default = "", help = "a human's name; empty for the player" } ]
steps = [
  { let = { h = "player" } },
  { write = "tf(h) + 0x0", type = "f32", value = "x" },
  { show = "f32(tf(h) + 0x0)", label = "x" },
]
```

Steps, each a table with one of these keys (the expressions are coney_tools.game_memory's, over the parameters and
every name bound before):

- `let = { NAME = "expr", ... }`: bind names, worked out in order.
- `require = "expr"`: stop with `message` when the value is 0.
- `show = "expr"`: print the value as `label = value` (`format` `hex`, `int` or `float`; default by type).
- `text = "expr"`: print the C string at that address (`size`, default 32).
- `write = "address expr"`, `type`, `value = "expr"`: write once, or again at each of the next `updates` updates.
- `humans = true`: print every human in use: index, name, player number, position and heading.
- `members = "address expr"`: print the humans of a handle list there (`count` words, default 16; a gang's members at
  gang `+0x48`): index, name, player number, the brain's goal stack (type ids, bottom first) and target.
- `globals = "expr"`: print the script's global variables whose names hold that text (`"''"` for all).
- `call = "function expr"`, `args = [...]`: make the game call its function through the call hook (a hook with
  `call = true` among `patches`), wait for it, and bind the result to `result` (default `result`).
- `lua = "Lua code"`: run a Lua chunk in the game's script state, as a level script would (its `{name}` fields are
  filled in from the names bound so far, Python format syntax): the text goes into LUA_BUFFER and the call hook makes
  the game call `ScriptSystem_DoString` (`0x00356a78`), which runs it with `lua_dobuffer`. Every script binding
  (`HuCreate`, `GiveMoney`, `MenuLoadLevel`, ...) is reachable this way.
- `wait = N` (or an expression): wait until N character updates have run.
- `emulation = "pause" | "resume"`, `advance = N`: the debug server's emulation pause and frame advance.

The runner is coney_tools.recipes_run.

Research: docs/guides/pcsx2-recipes.md
"""

from __future__ import annotations

import re
import tomllib
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, cast

from coney_tools import hooks as hook_tools
from coney_tools.game_memory import TYPES, ExpressionError, GameMemory, parse
from coney_tools.pcsx2_state import StateError, load_patches
from coney_tools.pine import Memory

RECIPES_FILE = Path("research/pcsx2-recipes.toml")
PATCHES_DIR = Path("research/traces/patches")
#: The evidence levels of docs/guides/research-evidence.md#evidence-levels, as the YAML values spell them.
EVIDENCE = ("confirmed-code", "confirmed-runtime", "inferred", "speculative")
EMULATION = ("running", "paused", "either")
#: A PCSX2 state library id (`coney-tools pcsx2 states`, docs/guides/pcsx2-states.md): a short slug, also the state's
#: file name. A recipe's `state` names one.
STATE_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,47}$")
PARAM_KINDS = ("number", "text", "expr", "human")
FORMATS = ("hex", "int", "float")
#: The step keys: exactly one per step, with the extra keys each allows.
STEP_KINDS: dict[str, set[str]] = {
    "let": set(),
    "require": {"message"},
    "show": {"label", "format"},
    "text": {"label", "size"},
    "write": {"type", "value", "updates"},
    "humans": set(),
    "members": {"count"},
    "globals": {"limit"},
    "call": {"args", "result", "timeout"},
    "lua": {"result", "timeout"},
    "wait": set(),
    "emulation": set(),
    "advance": set(),
}


#: The names every expression has (coney_tools.game_memory): a parameter may not hide one. GameMemory reads nothing
#: until a name is used, so one over no memory lists them.
RESERVED = frozenset(GameMemory(cast(Memory, None)).functions) | {"player", "camera", "game_time", "stick", "buttons"}


class RecipeError(Exception):
    """A recipe file entry that breaks the schema, or a run that cannot go on; the message names the recipe."""


@dataclass(frozen=True)
class Param:
    """One parameter: given on the command line as `--arg NAME=VALUE`, or its default."""

    name: str
    kind: str
    help: str
    default: str | None


@dataclass(frozen=True)
class Confirmed:
    """When and where a recipe was last seen working in PCSX2."""

    date: str
    copy: str
    pcsx2: str
    state: str
    note: str = ""


@dataclass(frozen=True)
class Recipe:
    """One entry of the recipe file."""

    name: str
    summary: str
    how: str
    research: tuple[str, ...]
    ghidra: tuple[str, ...]
    evidence: str
    patches: tuple[str, ...]
    emulation: str
    confirmed: Confirmed
    params: tuple[Param, ...]
    steps: tuple[dict[str, Any], ...]
    group: str = "Other"
    example: str = ""
    #: The id of a library state the recipe starts from (a precondition), or "".
    state: str = ""


@dataclass
class RecipeBook:
    """Every recipe, in file order, and the groups the pages show them under."""

    recipes: dict[str, Recipe] = field(default_factory=dict)
    groups: tuple[str, ...] = ()
    intro: str = ""


def _text(raw: Mapping[str, Any], key: str, where: str, required: bool = True) -> str:
    """A string field; RecipeError when it is required and missing or empty, or not a string."""
    value = raw.get(key, "")
    if not isinstance(value, str) or (required and not value.strip()):
        raise RecipeError(f"{where}: `{key}` must be a non-empty string")
    return value.strip()


def _strings(raw: Mapping[str, Any], key: str, where: str) -> tuple[str, ...]:
    """A list of strings (may be empty)."""
    value = raw.get(key, [])
    if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
        raise RecipeError(f"{where}: `{key}` must be a list of strings")
    return tuple(value)


def _check_expression(text: object, where: str) -> None:
    """RecipeError when `text` is not an expression of the address grammar."""
    if not isinstance(text, str):
        raise RecipeError(f"{where}: must be an expression string")
    try:
        parse(text)
    except ExpressionError as error:
        raise RecipeError(f"{where}: {error}") from error


def _step(raw: object, where: str) -> dict[str, Any]:
    """One step, checked: one kind key, only the extra keys it allows, expressions that parse."""
    if not isinstance(raw, dict):
        raise RecipeError(f"{where}: a step is a table")
    kinds = [key for key in raw if key in STEP_KINDS]
    if len(kinds) != 1:
        raise RecipeError(f"{where}: a step needs exactly one of {', '.join(STEP_KINDS)}")
    kind = kinds[0]
    unknown = set(raw) - {kind} - STEP_KINDS[kind]
    if unknown:
        raise RecipeError(f"{where}: unknown key(s) {', '.join(sorted(unknown))} for a {kind} step")
    value = raw[kind]
    if kind == "let":
        if not isinstance(value, dict) or not value:
            raise RecipeError(f"{where}: let binds a table of names to expressions")
        for name, text in value.items():
            _check_expression(text, f"{where}: let {name}")
    elif kind == "lua":
        if not isinstance(value, str) or not value.strip():
            raise RecipeError(f"{where}: lua takes the code as a string")
    elif kind in ("require", "show", "text", "write", "call", "members"):
        _check_expression(value, f"{where}: {kind}")
    elif kind in ("wait", "advance"):
        if isinstance(value, str):
            _check_expression(value, f"{where}: {kind}")
        elif not isinstance(value, int) or not 1 <= value <= 600:
            raise RecipeError(f"{where}: {kind} takes a count of 1 to 600, or an expression")
    elif kind == "emulation" and value not in ("pause", "resume"):
        raise RecipeError(f"{where}: emulation is pause or resume")
    elif kind == "humans" and value is not True:
        raise RecipeError(f"{where}: humans = true")
    elif kind == "globals":
        _check_expression(value, f"{where}: globals")
    if kind == "write":
        if raw.get("type") not in (*TYPES, "text"):
            raise RecipeError(f"{where}: a write needs a type, one of {', '.join(TYPES)} or text")
        _check_expression(raw.get("value"), f"{where}: write value")
        updates = raw.get("updates", 0)
        if not isinstance(updates, int) or not 0 <= updates <= 600:
            raise RecipeError(f"{where}: updates is 0 to 600")
    if kind == "show" and raw.get("format", "hex") not in FORMATS:
        raise RecipeError(f"{where}: format is one of {', '.join(FORMATS)}")
    if kind == "call":
        args = raw.get("args", [])
        if not isinstance(args, list) or len(args) > hook_tools.CALL_ARGS:
            raise RecipeError(f"{where}: args is a list of at most {hook_tools.CALL_ARGS} expressions")
        for k, arg in enumerate(args):
            _check_expression(arg, f"{where}: args[{k}]")
    return dict(raw)


def _param(raw: object, where: str) -> Param:
    """One parameter, checked."""
    if not isinstance(raw, dict):
        raise RecipeError(f"{where}: a parameter is a table")
    unknown = set(raw) - {"name", "kind", "help", "default"}
    if unknown:
        raise RecipeError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    name = _text(raw, "name", where)
    if not name.isidentifier():
        raise RecipeError(f"{where}: {name!r} is not a name expressions can use")
    if name in RESERVED:
        raise RecipeError(f"{where}: {name!r} would hide the expression name of that name; pick another")
    kind = raw.get("kind", "number")
    if kind not in PARAM_KINDS:
        raise RecipeError(f"{where}: kind is one of {', '.join(PARAM_KINDS)}")
    default = raw.get("default")
    if default is not None and not isinstance(default, str):
        raise RecipeError(f"{where}: a default is written as a string")
    return Param(name, kind, _text(raw, "help", where), default)


def _confirmed(raw: object, where: str) -> Confirmed:
    """The confirmed_runtime table: every recipe must have been seen working in PCSX2."""
    if not isinstance(raw, dict):
        raise RecipeError(f"{where}: needs confirmed_runtime = {{ date, copy, pcsx2, state }}")
    unknown = set(raw) - {"date", "copy", "pcsx2", "state", "note"}
    if unknown:
        raise RecipeError(f"{where}: confirmed_runtime has unknown key(s) {', '.join(sorted(unknown))}")
    date = _text(raw, "date", f"{where}: confirmed_runtime")
    if len(date) != 10 or date[4] != "-" or date[7] != "-" or not date.replace("-", "").isdigit():
        raise RecipeError(f"{where}: confirmed_runtime date must be YYYY-MM-DD")
    return Confirmed(
        date,
        _text(raw, "copy", f"{where}: confirmed_runtime"),
        _text(raw, "pcsx2", f"{where}: confirmed_runtime"),
        _text(raw, "state", f"{where}: confirmed_runtime"),
        _text(raw, "note", f"{where}: confirmed_runtime", required=False),
    )


def parse_recipe(name: str, raw: object, groups: Sequence[str], known_patches: set[str]) -> Recipe:
    """One `[recipe.NAME]` table, checked against the schema of the module docstring. Raises RecipeError."""
    where = f"recipe {name}"
    if not isinstance(raw, dict):
        raise RecipeError(f"{where}: must be a table")
    allowed = {
        "summary", "how", "research", "ghidra", "evidence", "patches", "emulation", "confirmed_runtime",
        "params", "steps", "group", "example", "state",
    }  # fmt: skip
    unknown = set(raw) - allowed
    if unknown:
        raise RecipeError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    evidence = raw.get("evidence")
    if evidence not in EVIDENCE:
        raise RecipeError(f"{where}: evidence must be one of {', '.join(EVIDENCE)}")
    ghidra = _strings(raw, "ghidra", where)
    if not ghidra or not all(item.startswith("0x00") and len(item.split()[0]) == 10 for item in ghidra):
        raise RecipeError(f"{where}: ghidra lists at least one `0x00xxxxxx Name` that justifies the recipe")
    research = _strings(raw, "research", where)
    if not research or not all(item.startswith("docs/") for item in research):
        raise RecipeError(f"{where}: research lists at least one docs/ page")
    patches = _strings(raw, "patches", where)
    missing = [patch for patch in patches if patch not in known_patches]
    if missing:
        raise RecipeError(f"{where}: unknown patch or hook {', '.join(missing)} (research/traces/patches/)")
    emulation = raw.get("emulation", "either")
    if emulation not in EMULATION:
        raise RecipeError(f"{where}: emulation is one of {', '.join(EMULATION)}")
    group = raw.get("group", "Other")
    if group not in groups:
        raise RecipeError(f"{where}: group {group!r} is not one of {', '.join(groups)}")
    params = tuple(_param(p, f"{where}: params[{k}]") for k, p in enumerate(raw.get("params", [])))
    steps_raw = raw.get("steps", [])
    if not isinstance(steps_raw, list):
        raise RecipeError(f"{where}: steps is a list")
    steps = tuple(_step(s, f"{where}: steps[{k}]") for k, s in enumerate(steps_raw))
    if not steps and "manual" not in raw.get("how", "").lower() and not raw.get("example"):
        raise RecipeError(f"{where}: a recipe with no steps gives an `example` of what to run instead")
    if any("call" in step or "lua" in step for step in steps) and not patches:
        raise RecipeError(f"{where}: a call step needs a call hook among patches")
    state = raw.get("state", "")
    if not isinstance(state, str) or (state and not STATE_ID.match(state)):
        raise RecipeError(f"{where}: state is the id of a library state (lower-case letters, digits and '-')")
    return Recipe(
        name=name,
        summary=_text(raw, "summary", where),
        how=_text(raw, "how", where),
        research=research,
        ghidra=ghidra,
        evidence=evidence,
        patches=patches,
        emulation=emulation,
        confirmed=_confirmed(raw.get("confirmed_runtime"), where),
        params=params,
        steps=steps,
        group=group,
        example=_text(raw, "example", where, required=False),
        state=state,
    )


def known_patch_names(root: Path) -> set[str]:
    """Every patch and hook name of research/traces/patches/."""
    try:
        return set(load_patches(root / PATCHES_DIR)) | set(hook_tools.load_hooks(root / PATCHES_DIR))
    except (StateError, hook_tools.HookError) as error:
        raise RecipeError(str(error)) from error


def load(root: Path, path: Path | None = None) -> RecipeBook:
    """The recipe file (research/pcsx2-recipes.toml under `root`, or `path`), checked. Raises RecipeError."""
    file = path or root / RECIPES_FILE
    try:
        data = tomllib.loads(file.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise RecipeError(f"{file}: {error}") from error
    groups = tuple(data.get("groups", ["Other"]))
    known = known_patch_names(root)
    book = RecipeBook(groups=groups, intro=str(data.get("intro", "")).strip())
    for name, raw in data.get("recipe", {}).items():
        book.recipes[name] = parse_recipe(name, raw, groups, known)
    if not book.recipes:
        raise RecipeError(f"{file}: holds no recipe")
    return book
