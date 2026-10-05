# SPDX-License-Identifier: GPL-3.0-or-later
"""The script-binding masterlist: loading and checking `research/bindings/<category>.yaml`.

The file lists every function the game registers with its Lua state (the role FiveM calls "natives"), one entry per
name. The schema is documented in docs/guides/research-workflow.md#bindings; the rendered pages are in
docs/references/bindings/ (see `natives_render`).
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from coney_tools.config import ConfigError

DATA_DIR = Path("research/bindings")  # one file per category: research/bindings/<category>.yaml
PAGES_DIR = Path("docs/references/bindings")
CONEY_TABLE = Path("src/scripting/script_bindings.cpp")  # Coney's binding table, the source of the `coney` key

# The categories, in page order: id -> (title, one-line scope). A binding's `category` must be one of these ids, and
# each id is also the name of its page (docs/references/bindings/<id>.md).
CATEGORIES: dict[str, tuple[str, str]] = {
    "character": ("Characters", "one human: creation, state, health, animation, commands, the player's crew"),
    "ai": ("AI", "goals, actions, brains and gang tactics that drive non-player characters"),
    "gang": ("Gangs", "gangs: creation, membership, spawners, relations"),
    "camera": ("Cameras", "creating, switching and moving cameras"),
    "world": ("World and objects", "objects, cars, doors, flags, paths, volume boxes and triggers"),
    "effects": ("Effects and lighting", "particles, weather, fog, lights, shadows, gamma and screen effects"),
    "hud": ("HUD and menus", "the in-game HUD, radar, objectives, messages and front-end menus"),
    "sound": ("Sound and music", "sound effects, ambient emitters, music tracks and sound configuration"),
    "scene": ("Scenes and movies", "in-engine cutscenes and full-motion movies"),
    "level": ("Levels and game state", "level flow, checkpoints, difficulty, unlockables, stats, money and police"),
    "script": ("Script flow", "running scripts, scheduled calls, callbacks and message handlers"),
    "input": ("Pad input", "the gamepad: button handlers"),
    "config": ("Configuration (Cfg)", "the Cfg* tables the config scripts fill: characters, objects, levels, sounds"),
    "util": ("Utilities", "numbers, platform queries and generic object queries"),
    "debug": ("Debug", "developer leftovers: network debugging, sample capture, detail flags"),
}

ORIGINS = ("game", "coney")
EVIDENCE = {
    "confirmed-code": "confirmed (code)",
    "confirmed-runtime": "confirmed (runtime)",
    "inferred": "inferred",
    "speculative": "speculative",
}
DEPTHS = ("thorough", "brief", "mechanical")
REGISTRARS = ("RegisterBindings", "ScriptSystem")
ARG_LUA_TYPES = ("number", "boolean", "string", "table", "userdata")
ARG_CTYPES = ("int", "unsigned", "float", "double")
RESULT_LUA_TYPES = ("number", "boolean", "string", "usertype")
CONEY_STATUSES = ("not implemented", "partial", "implemented")
USAGE_KEYS = ("chunks", "calls", "boot", "mission1", "result_used")

_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


@dataclass(frozen=True)
class Arg:
    """One argument, by its position: the Lua type the wrapper reads it as and what it means."""

    name: str
    lua: str
    desc: str
    ctype: str | None = None
    elem: str | None = None
    count: int | None = None
    default: Any = None
    written_back: bool = False


@dataclass(frozen=True)
class Result:
    """One value the binding pushes back to Lua."""

    lua: str
    desc: str
    type: str | None = None


@dataclass(frozen=True)
class Call:
    """A function of the original the wrapper calls: its address and, when we have one, our name for it."""

    addr: int
    name: str | None = None


@dataclass(frozen=True)
class Variant:
    """One registration of a name: the wrapper, what it calls, its arguments and results."""

    wrapper: int | None
    calls: tuple[Call, ...]
    args: tuple[Arg, ...]
    results: tuple[Result, ...]
    virtual_calls: int = 0
    note: str = ""


@dataclass(frozen=True)
class Usage:
    """How the game's compiled scripts use a binding (counted from the disc)."""

    chunks: int
    calls: int
    boot: bool
    mission1: bool
    result_used: bool


@dataclass(frozen=True)
class Binding:
    """One binding name, with the registration Lua keeps (`main`) and any earlier overloads it falls back to."""

    name: str
    category: str
    origin: str
    registered_by: str
    main: Variant
    overloads: tuple[Variant, ...]
    description: str
    evidence: str
    depth: str
    notes: str
    usage: Usage | None
    coney: str

    @property
    def anchor(self) -> str:
        """The binding's anchor on its category page: the name in lower case (names never differ only by case)."""
        return self.name.lower()


@dataclass
class Masterlist:
    """Every binding in file order, plus the problems found while loading."""

    bindings: list[Binding] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)

    def by_category(self, category: str) -> list[Binding]:
        """The bindings of `category`, sorted by name (case-insensitively, as a reader scans for them)."""
        return sorted((b for b in self.bindings if b.category == category), key=lambda b: (b.name.lower(), b.name))


class _Checker:
    """Collects problems for one entry, each prefixed with where it was found."""

    def __init__(self, problems: list[str], where: str) -> None:
        self.problems = problems
        self.where = where

    def fail(self, message: str) -> None:
        """Record a problem."""
        self.problems.append(f"{self.where}: {message}")

    def text(self, raw: dict[str, Any], key: str, *, required: bool = True) -> str:
        """`raw[key]` as a string ('' when absent and optional)."""
        value = raw.get(key)
        if value is None:
            if required:
                self.fail(f"missing `{key}`")
            return ""
        if not isinstance(value, str):
            self.fail(f"`{key}` must be a string")
            return ""
        return value.strip()

    def choice(self, raw: dict[str, Any], key: str, allowed: tuple[str, ...] | list[str], default: str | None) -> str:
        """`raw[key]`, which must be one of `allowed`; `default` when absent (None makes it required)."""
        value = raw.get(key, default)
        if value is None:
            self.fail(f"missing `{key}`")
            return allowed[0]
        if value not in allowed:
            self.fail(f"`{key}` is {value!r}; expected one of {', '.join(allowed)}")
            return allowed[0]
        return str(value)

    def address(self, raw: dict[str, Any], key: str, *, required: bool) -> int | None:
        """`raw[key]` as an address (a YAML integer such as 0x0037d420)."""
        value = raw.get(key)
        if value is None:
            if required:
                self.fail(f"missing `{key}`")
            return None
        if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value < 1 << 32:
            self.fail(f"`{key}` must be an address like 0x0037d420")
            return None
        return value


def _check_keys(check: _Checker, raw: dict[str, Any], allowed: set[str], what: str) -> None:
    """Report keys `raw` has beyond `allowed` (a typo would otherwise be silently ignored)."""
    for key in sorted(set(raw) - allowed):
        check.fail(f"unknown {what} key `{key}`")


def _load_arg(check: _Checker, raw: Any, position: int) -> Arg | None:
    """One argument from its mapping."""
    if not isinstance(raw, dict):
        check.fail(f"argument {position} must be a mapping")
        return None
    sub = _Checker(check.problems, f"{check.where}: argument {position}")
    _check_keys(sub, raw, {"name", "lua", "ctype", "elem", "count", "default", "written_back", "desc"}, "argument")
    name = sub.text(raw, "name")
    if name and not _IDENTIFIER.match(name):
        sub.fail(f"name {name!r} is not an identifier")
    lua = sub.choice(raw, "lua", ARG_LUA_TYPES, None)
    ctype = raw.get("ctype")
    if ctype is not None and ctype not in ARG_CTYPES:
        sub.fail(f"`ctype` is {ctype!r}; expected one of {', '.join(ARG_CTYPES)}")
    if ctype is not None and lua != "number":
        sub.fail("`ctype` is only for number arguments")
    elem = raw.get("elem")
    if lua == "table" and elem not in ("number", "string"):
        sub.fail("a table argument needs `elem: number` or `elem: string`")
    if lua != "table" and (elem is not None or raw.get("count") is not None):
        sub.fail("`elem` and `count` are only for table arguments")
    count = raw.get("count")
    if count is not None and (not isinstance(count, int) or isinstance(count, bool) or count < 1):
        sub.fail("`count` must be a positive integer")
    written_back = raw.get("written_back", False)
    if not isinstance(written_back, bool):
        sub.fail("`written_back` must be true or false")
    return Arg(
        name=name,
        lua=lua,
        desc=sub.text(raw, "desc", required=False),
        ctype=ctype,
        elem=elem,
        count=count,
        default=raw.get("default"),
        written_back=bool(written_back),
    )


def _load_result(check: _Checker, raw: Any, position: int) -> Result | None:
    """One result from its mapping."""
    if not isinstance(raw, dict):
        check.fail(f"result {position} must be a mapping")
        return None
    sub = _Checker(check.problems, f"{check.where}: result {position}")
    _check_keys(sub, raw, {"lua", "type", "desc"}, "result")
    return Result(
        lua=sub.choice(raw, "lua", RESULT_LUA_TYPES, None),
        desc=sub.text(raw, "desc", required=False),
        type=sub.text(raw, "type", required=False) or None,
    )


def _load_variant(check: _Checker, raw: dict[str, Any], *, game: bool) -> Variant:
    """The registration fields of an entry or of one of its overloads."""
    calls: list[Call] = []
    for i, call in enumerate(raw.get("calls") or [], start=1):
        if not isinstance(call, dict):
            check.fail(f"call {i} must be a mapping")
            continue
        sub = _Checker(check.problems, f"{check.where}: call {i}")
        _check_keys(sub, call, {"addr", "name"}, "call")
        addr = sub.address(call, "addr", required=True)
        if addr is not None:
            calls.append(Call(addr, sub.text(call, "name", required=False) or None))
    args = [_load_arg(check, a, i) for i, a in enumerate(raw.get("args") or [], start=1)]
    results = [_load_result(check, r, i) for i, r in enumerate(raw.get("results") or [], start=1)]
    names = [a.name for a in args if a]
    for name in sorted({n for n in names if names.count(n) > 1}):
        check.fail(f"argument name `{name}` is used twice")
    virtual_calls = raw.get("virtual_calls", 0)
    if not isinstance(virtual_calls, int) or isinstance(virtual_calls, bool) or virtual_calls < 0:
        check.fail("`virtual_calls` must be a count")
        virtual_calls = 0
    return Variant(
        wrapper=check.address(raw, "wrapper", required=game),
        calls=tuple(calls),
        args=tuple(a for a in args if a),
        results=tuple(r for r in results if r),
        virtual_calls=virtual_calls,
        note=check.text(raw, "note", required=False),
    )


def _load_usage(check: _Checker, raw: Any) -> Usage | None:
    """The usage counts of a game binding."""
    if not isinstance(raw, dict):
        check.fail("missing `usage` (chunks, calls, boot, mission1, result_used)")
        return None
    _check_keys(check, raw, set(USAGE_KEYS), "usage")
    values: dict[str, Any] = {}
    for key in USAGE_KEYS:
        value = raw.get(key)
        want_bool = key in ("boot", "mission1", "result_used")
        ok = isinstance(value, bool) if want_bool else isinstance(value, int) and not isinstance(value, bool)
        if not ok:
            check.fail(f"usage `{key}` must be {'true or false' if want_bool else 'a count'}")
            return None
        values[key] = value
    if values["chunks"] > values["calls"]:
        check.fail("usage: more chunks than calls")
    if values["calls"] == 0 and (values["boot"] or values["mission1"] or values["result_used"]):
        check.fail("usage: never called, yet marked as used")
    return Usage(**values)


_ENTRY_KEYS = {
    "name", "category", "origin", "registered_by", "wrapper", "calls", "virtual_calls", "args", "results",
    "overloads", "description", "evidence", "depth", "notes", "usage", "coney",
}  # fmt: skip


def _load_entry(problems: list[str], raw: Any, index: int) -> Binding | None:
    """One binding from its mapping; problems go to `problems`."""
    if not isinstance(raw, dict):
        problems.append(f"entry {index}: must be a mapping")
        return None
    name = raw.get("name")
    check = _Checker(problems, f"entry {index} ({name})" if isinstance(name, str) else f"entry {index}")
    _check_keys(check, raw, _ENTRY_KEYS, "entry")
    if not isinstance(name, str) or not _IDENTIFIER.match(name):
        check.fail("`name` must be a Lua identifier")
        return None
    origin = check.choice(raw, "origin", ORIGINS, "game")
    game = origin == "game"
    main = _load_variant(check, raw, game=game)
    if main.note:
        check.fail("`note` belongs to an overload; use `notes` for the entry")
    overloads = []
    for i, over in enumerate(raw.get("overloads") or [], start=1):
        if not isinstance(over, dict):
            check.fail(f"overload {i} must be a mapping")
            continue
        sub = _Checker(problems, f"{check.where}: overload {i}")
        _check_keys(sub, over, {"wrapper", "calls", "virtual_calls", "args", "results", "note"}, "overload")
        overloads.append(_load_variant(sub, over, game=game))
    depth = check.choice(raw, "depth", DEPTHS, None)
    description = check.text(raw, "description", required=False)
    if not description and depth != "mechanical":
        check.fail("a described entry (depth thorough or brief) needs a `description`")
    if depth == "thorough":
        for variant in (main, *overloads):
            for i, arg in enumerate(variant.args, start=1):
                if not arg.desc:
                    check.fail(f"thorough entry: argument {i} (`{arg.name}`) has no `desc`")
    usage = _load_usage(check, raw.get("usage")) if game else None
    if not game and "usage" in raw:
        check.fail("a Coney-added binding has no game `usage`")
    return Binding(
        name=name,
        category=check.choice(raw, "category", list(CATEGORIES), None),
        origin=origin,
        registered_by=check.choice(raw, "registered_by", REGISTRARS, "RegisterBindings"),
        main=main,
        overloads=tuple(overloads),
        description=description,
        evidence=check.choice(raw, "evidence", list(EVIDENCE), None),
        depth=depth,
        notes=check.text(raw, "notes", required=False),
        usage=usage,
        coney=check.choice(raw, "coney", CONEY_STATUSES, "not implemented"),
    )


def _check_duplicates(masterlist: Masterlist) -> None:
    """Report names listed twice, or differing only by case (their anchors would collide)."""
    seen: dict[str, str] = {}
    for binding in masterlist.bindings:
        key = binding.name.lower()
        if key in seen:
            masterlist.problems.append(f"{binding.name}: listed twice (or differs from {seen[key]} only by case)")
        seen[key] = binding.name


def parse(text: str, source: str = "bindings.yaml", category: str | None = None) -> Masterlist:
    """Parse the YAML text of one data file; structural problems are collected, not raised.

    With `category` (the file's own, from its name), an entry without a `category` key gets it and an entry with a
    different one is a problem. Raises ConfigError only when the text is not YAML or not a list at all.
    """
    try:
        raw = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ConfigError(f"{source}: not valid YAML ({error})") from error
    if raw is None:
        raw = []
    if not isinstance(raw, list):
        raise ConfigError(f"{source}: expected a list of bindings")
    result = Masterlist()
    for index, entry in enumerate(raw, start=1):
        if category and isinstance(entry, dict):
            given = entry.setdefault("category", category)
            if given != category:
                result.problems.append(f"{source}: entry {index} has category {given!r}; move it to {given}.yaml")
        binding = _load_entry(result.problems, entry, index)
        if binding:
            result.bindings.append(binding)
    if category:
        result.problems[:] = [p if p.startswith(source) else f"{source}: {p}" for p in result.problems]
    _check_duplicates(result)
    return result


def load(root: Path) -> Masterlist:
    """Load every `research/bindings/<category>.yaml` from the checkout at `root`, in category order."""
    folder = root / DATA_DIR
    if not folder.is_dir():
        raise ConfigError(f"{folder}: no such folder")
    result = Masterlist()
    for path in sorted(folder.glob("*.yaml")):
        source = (DATA_DIR / path.name).as_posix()
        if path.stem not in CATEGORIES:
            result.problems.append(f"{source}: not a category ({', '.join(CATEGORIES)})")
            continue
        try:
            text = path.read_bytes().decode("utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{path}: cannot be read ({error})") from error
        part = parse(text, source, path.stem)
        result.bindings += part.bindings
        result.problems += [p for p in part.problems if "listed twice" not in p]
    _check_duplicates(result)
    return result


# The kinds of Coney's binding table, and the `coney` status each one means.
_KIND_STATUS = {"real": "implemented", "routed": "partial", "stub": "not implemented", "recording": "not implemented"}
_TABLE_START = re.compile(r"^constexpr std::array kBindings\{$", re.M)
_TABLE_ENTRY = re.compile(r"^\s*(real|routed|stub|recording)\(\"([A-Za-z_][A-Za-z_0-9]*)\"")


def parse_coney_table(text: str, source: str = CONEY_TABLE.as_posix()) -> dict[str, str]:
    """Read the `kBindings` table of script_bindings.cpp: each name with the `coney` status of its kind.

    Real bindings are implemented, routed ones (handed to a stand-in for a subsystem Coney lacks) partial, stubs not
    implemented. Every non-blank, non-comment line of the table must be one `kind("Name"...)` entry, so a change of
    the table's shape fails loudly instead of being misread.
    """
    start = _TABLE_START.search(text)
    if not start:
        raise ConfigError(f"{source}: no `constexpr std::array kBindings{{` table")
    result: dict[str, str] = {}
    for line in text[start.end() :].splitlines()[1:]:
        stripped = line.strip()
        if stripped == "};":
            return result
        if not stripped or stripped.startswith("//"):
            continue
        match = _TABLE_ENTRY.match(line)
        if not match:
            raise ConfigError(f"{source}: cannot read the binding table line {stripped!r}")
        if match.group(2) in result:
            raise ConfigError(f"{source}: {match.group(2)} is in the binding table twice")
        result[match.group(2)] = _KIND_STATUS[match.group(1)]
    raise ConfigError(f"{source}: the binding table does not end")


def load_coney_table(root: Path) -> dict[str, str]:
    """`parse_coney_table` on the checkout at `root`."""
    path = root / CONEY_TABLE
    try:
        text = path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    return parse_coney_table(text.replace("\r\n", "\n"))


def set_coney_statuses(text: str, statuses: dict[str, str]) -> str:
    """The YAML text of one data file with each entry's `coney` key set from `statuses` (by name).

    The default, "not implemented", is written as no key; other statuses go on a `coney:` line at the end of the
    entry. Only `coney:` lines change, so comments and layout are kept.
    """
    out: list[str] = []
    name: str | None = None

    def close() -> None:
        # Put the current entry's status line after its last non-blank line.
        status = statuses.get(name, "not implemented") if name else "not implemented"
        if status == "not implemented":
            return
        at = len(out)
        while at and not out[at - 1].strip():
            at -= 1
        out.insert(at, f"  coney: {status}")

    for line in text.splitlines():
        if line.startswith("- name: "):
            close()
            name = line[len("- name: ") :].split("#")[0].strip()
        elif line.startswith("  coney:"):
            continue
        out.append(line)
    close()
    return "\n".join(out) + "\n"
