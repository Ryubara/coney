# SPDX-License-Identifier: GPL-3.0-or-later
"""The reference of `global.lua`'s helpers: the research data file, and the survey of the disc that fills its counts.

`global.lua` runs before every level's script in the game state's Lua 4.0 (docs/research/scripting.md), and the level
scripts build on the functions and globals it defines: `ActivateWarriors`, `runNextMission`, `ObjectiveSetup`,
`true` and `false`. A mission mod's patched script calls them as bare globals, as the original does, so they are the
API a modder needs beside the bindings. `research/script-helpers.yaml` holds an entry for each: our own name for each
parameter, a group, our own description and its evidence level. Its `scan` lines are written by
`coney-tools bindings scan-helpers` from the player's disc and hold only names and counts (LEGAL.md#no-game-data):

* **A helper** is a function `global.lua` stores in a global or in a field of a global table (`TagInfo.SetupTags`),
  as `natives_mission.helper_functions` finds them. Its scan line gives the number of its prototype in the chunk
  (the `global.lua:fN` script locator its evidence cites), its parameter count, the natives (bindings) and other
  helpers it calls (a callback name it passes counts as a call), how many of the disc's other scripts call it (by name,
  or by passing its name as a callback) and how many calls they make, whether `global.lua`'s main chunk runs it while
  loading or passes its name to a binding (a callback the engine calls), and which functions delete it (set it to nil).
* **A global** is a variable the main chunk, or a helper the main chunk runs, sets to something other than a function
  and that another script reads (`GETGLOBAL`). Its scan line gives how many scripts read it, the kind of value it ends
  with, and whether the main chunk clears it at the end.

`coney-tools bindings render` writes the page (`script_helpers_render`) beside the binding pages. The schema is in
docs/guides/research-data.md#script-helpers.
"""

from __future__ import annotations

import hashlib
import re
from collections.abc import Iterable
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

import yaml

from coney_tools import lua4, natives_mission
from coney_tools.config import ConfigError
from coney_tools.natives import EVIDENCE, Checker, check_keys

DATA_FILE = Path("research/script-helpers.yaml")
PAGE = "helpers.md"  # in natives.PAGES_DIR, beside the binding pages
HELPERS_SCRIPT = natives_mission.HELPERS_SCRIPT

# The groups, in page order: id -> (title, what the group holds). An entry's `group` must be one of these ids.
GROUPS: dict[str, tuple[str, str]] = {
    "squad": ("Squad", "the player's crew: switching brains off and on, following"),
    "gangs": ("Gangs", "setting up gangs and their relations, the `Super` gang manager, world-flag callbacks"),
    "cameras": ("Cameras", "the follow camera, the interior cameras and skippable camera moves"),
    "scenes": ("Scenes", "playing cutscenes and keeping bystanders back while they play"),
    "objectives": ("Objectives", "mission objectives, their HUD counters and the good-to-go check"),
    "missions": ("Missions and saves", "the story's chain of missions, unlocks and the scripts' save data"),
    "tags": ("Tags", "the tagging side objective"),
    "inventory": ("Inventory", "the starting inventory and the second player's"),
    "rage": ("Rage", "the rage handlers"),
    "lighting": ("Lighting", "the character lights, light effects and thunderstorms"),
    "sound": ("Sound", "reverb presets and sound paths"),
    "cheats": ("Cheats", "the cheat codes"),
    "strings": ("Strings", "string helpers and the shared text tables"),
    "tables": ("Tables", "small table utilities"),
    "values": ("Values and constants", "values every script relies on"),
    "setup": (
        "One-time setup",
        "the setup functions that run once, most while `global.lua` loads, and then delete themselves",
    ),
    "debug": ("Debug", "developer leftovers"),
}

_HELPER_KEYS = {"name", "group", "params", "description", "evidence", "notes", "scan"}
_GLOBAL_KEYS = {"name", "group", "description", "evidence", "notes", "scan"}
_HELPER_SCAN_KEYS = {
    "function", "params", "natives", "helpers", "scripts", "calls", "at_load", "registered", "deleted_by"
}  # fmt: skip
_GLOBAL_SCAN_KEYS = {"scripts", "value", "cleared"}
_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$")
_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
#: The name the main chunk goes by in `deleted_by`.
MAIN_NAME = "(main chunk)"


@dataclass(frozen=True)
class HelperScan:
    """What the survey read about one helper (names and counts only)."""

    function: int  # the prototype's number among the main chunk's: the `global.lua:fN` locator
    params: int
    natives: tuple[str, ...] = ()
    helpers: tuple[str, ...] = ()
    scripts: int = 0  # other scripts that call it
    calls: int = 0  # their calls of it (callback names included)
    at_load: bool = False  # the main chunk calls it
    registered: bool = False  # the main chunk passes its name to a binding
    deleted_by: tuple[str, ...] = ()


@dataclass(frozen=True)
class GlobalScan:
    """What the survey read about one global (names and counts only)."""

    scripts: int
    value: str  # number, string, table, nil or computed: the kind of the last value `global.lua` gives it
    cleared: bool = False  # the main chunk sets it to nil at the end, after giving it a value


@dataclass
class Helper:
    """One entry of the data file's `helpers` list."""

    name: str
    group: str
    params: list[str]
    description: str
    evidence: str
    notes: str = ""
    scan: HelperScan | None = None

    @property
    def anchor(self) -> str:
        """The section's id on the page: the name in lower case without its dots (`taginfo-setuptags` would clash
        with a heading id, so `taginfosetuptags`)."""
        return self.name.lower().replace(".", "")


@dataclass
class GlobalVar:
    """One entry of the data file's `globals` list."""

    name: str
    group: str
    description: str
    evidence: str
    notes: str = ""
    scan: GlobalScan | None = None


@dataclass
class HelperList:
    """The data file: the hash of the chunk it was surveyed from, the entries and the problems found reading them."""

    chunk_sha1: str | None = None
    helpers: list[Helper] = field(default_factory=list)
    globals: list[GlobalVar] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)


@dataclass
class Survey:
    """The survey of one disc: the chunk's hash, each helper and each global read by another script, in chunk
    order."""

    chunk_sha1: str
    helpers: dict[str, HelperScan]
    globals: dict[str, GlobalScan]


# --- reading the data file --------------------------------------------------------------------------------------------


def _strings(check: Checker, raw: Any, key: str) -> tuple[str, ...]:
    """A scan field that lists names."""
    if raw is None:
        return ()
    if not isinstance(raw, list) or not all(isinstance(item, str) for item in raw):
        check.fail(f"`scan.{key}` must be a list of names")
        return ()
    return tuple(raw)


def _count(check: Checker, raw: dict[str, Any], key: str, *, required: bool = True) -> int:
    """A scan field that is a count (or a prototype number)."""
    value = raw.get(key, None if required else 0)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        check.fail(f"`scan.{key}` must be a whole number")
        return 0
    return value


def _helper_scan(check: Checker, raw: Any) -> HelperScan | None:
    """A helper's `scan` mapping; None (and a problem) when it is missing or broken."""
    if raw is None:
        check.fail("no `scan` line: run `coney-tools bindings scan-helpers`")
        return None
    if not isinstance(raw, dict):
        check.fail("`scan` must be a mapping")
        return None
    check_keys(check, raw, _HELPER_SCAN_KEYS, "scan")
    return HelperScan(
        function=_count(check, raw, "function"),
        params=_count(check, raw, "params"),
        natives=_strings(check, raw.get("natives"), "natives"),
        helpers=_strings(check, raw.get("helpers"), "helpers"),
        scripts=_count(check, raw, "scripts"),
        calls=_count(check, raw, "calls"),
        at_load=raw.get("at_load") is True,
        registered=raw.get("registered") is True,
        deleted_by=_strings(check, raw.get("deleted_by"), "deleted_by"),
    )


def _global_scan(check: Checker, raw: Any) -> GlobalScan | None:
    """A global's `scan` mapping; None (and a problem) when it is missing or broken."""
    if raw is None:
        check.fail("no `scan` line: run `coney-tools bindings scan-helpers`")
        return None
    if not isinstance(raw, dict):
        check.fail("`scan` must be a mapping")
        return None
    check_keys(check, raw, _GLOBAL_SCAN_KEYS, "scan")
    value = raw.get("value")
    if not isinstance(value, str):
        check.fail("`scan.value` must be a kind of value")
        value = ""
    return GlobalScan(scripts=_count(check, raw, "scripts"), value=value, cleared=raw.get("cleared") is True)


def _common(check: Checker, raw: dict[str, Any]) -> tuple[str, str, str, str]:
    """The keys helpers and globals share: group, description, evidence and notes."""
    group = check.choice(raw, "group", list(GROUPS), None)
    description = raw.get("description")
    if not isinstance(description, str) or not description.strip():
        check.fail("missing `description`")
        description = ""
    evidence = check.choice(raw, "evidence", list(EVIDENCE), "confirmed-code")
    return group, " ".join(description.split()), evidence, check.text(raw, "notes", required=False)


def _load_helper(problems: list[str], raw: Any, source: str, index: int) -> Helper | None:
    """One `helpers` entry; problems are collected."""
    if not isinstance(raw, dict) or not isinstance(raw.get("name"), str) or not _NAME.match(raw["name"]):
        problems.append(f"{source}: helper {index}: needs a `name` (a global or `Table.field` name)")
        return None
    check = Checker(problems, f"{source}: {raw['name']}")
    check_keys(check, raw, _HELPER_KEYS, "helper")
    group, description, evidence, notes = _common(check, raw)
    params = raw.get("params")
    if not isinstance(params, list) or not all(isinstance(p, str) and _IDENTIFIER.match(p) for p in params):
        check.fail("`params` must be a list of names")
        params = []
    for name in sorted({p for p in params if params.count(p) > 1}):
        check.fail(f"parameter `{name}` named twice")
    scan = _helper_scan(check, raw.get("scan"))
    if scan is not None and len(params) != scan.params:
        check.fail(f"{len(params)} parameter(s) named, the chunk has {scan.params}")
    return Helper(raw["name"], group, list(params), description, evidence, notes, scan)


def _load_global(problems: list[str], raw: Any, source: str, index: int) -> GlobalVar | None:
    """One `globals` entry; problems are collected."""
    if not isinstance(raw, dict) or not isinstance(raw.get("name"), str) or not _IDENTIFIER.match(raw["name"]):
        problems.append(f"{source}: global {index}: needs a `name`")
        return None
    check = Checker(problems, f"{source}: {raw['name']}")
    check_keys(check, raw, _GLOBAL_KEYS, "global")
    group, description, evidence, notes = _common(check, raw)
    return GlobalVar(raw["name"], group, description, evidence, notes, _global_scan(check, raw.get("scan")))


def parse(text: str, source: str = DATA_FILE.as_posix()) -> HelperList:
    """Parse the data file's text; problems are collected, not raised (ConfigError only when it is not YAML or not
    a mapping)."""
    try:
        raw = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ConfigError(f"{source}: not valid YAML ({error})") from error
    if not isinstance(raw, dict):
        raise ConfigError(f"{source}: expected a mapping with `helpers` and `globals`")
    result = HelperList()
    for key in sorted(set(raw) - {"chunk_sha1", "helpers", "globals"}):
        result.problems.append(f"{source}: unknown key `{key}`")
    sha1 = raw.get("chunk_sha1")
    result.chunk_sha1 = sha1 if isinstance(sha1, str) else None
    for index, entry in enumerate(raw.get("helpers") or [], start=1):
        helper = _load_helper(result.problems, entry, source, index)
        if helper:
            result.helpers.append(helper)
    for index, entry in enumerate(raw.get("globals") or [], start=1):
        variable = _load_global(result.problems, entry, source, index)
        if variable:
            result.globals.append(variable)
    names = [h.name for h in result.helpers] + [g.name for g in result.globals]
    for name in sorted({n for n in names if names.count(n) > 1}):
        result.problems.append(f"{source}: {name} listed twice")
    return result


def load(root: Path) -> HelperList | None:
    """The data file of the checkout at `root`, or None when it has none."""
    path = root / DATA_FILE
    if not path.is_file():
        return None
    try:
        text = path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    return parse(text)


# --- surveying the chunks ---------------------------------------------------------------------------------------------


def _top(path: str) -> int:
    """The number of the main chunk's prototype a path lies in: `main/3/0` -> 3."""
    return int(path.split("/")[1])


def _value_kind(value: lua4.Value) -> str:
    """The kind of value a global is given, as the page names it."""
    if value is None:
        return "nil"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    return "table" if isinstance(value, lua4.Table) else "computed"


def _in(path: str, root: str, named: set[str]) -> bool:
    """Whether `path` is `root` or a function nested in it that is not a helper of its own."""
    return path == root or (path.startswith(root + "/") and path not in named)


def _helper_scans(facts: lua4.ChunkFacts, main: lua4.Proto, natives: frozenset[str]) -> dict[str, HelperScan]:
    """Every helper of `global.lua` and what its own code calls, in chunk order (counts of other scripts left 0)."""
    found = natives_mission.helper_functions(facts)
    helpers = {name: path for name, path in found.items() if path != "main"}
    named = set(helpers.values())
    by_path = natives_mission.functions_by_path(main)
    name_of = {path: name for name, path in helpers.items()}
    scans: dict[str, HelperScan] = {}
    for name, path in sorted(helpers.items(), key=lambda item: [int(p) for p in item[1].split("/")[1:]]):
        own_natives: list[str] = []
        own_helpers: list[str] = []
        for call in facts.calls:
            if not _in(call.path, path, named):
                continue
            referenced = [call.callee] + [arg for arg in call.args if isinstance(arg, str)]
            for item in referenced:
                if item in natives and item == call.callee and item not in own_natives:
                    own_natives.append(item)
                elif item in helpers and item != name and item not in own_helpers:
                    own_helpers.append(item)
        main_refs = [c for c in facts.calls if c.path == "main"]
        deleted = [
            MAIN_NAME if a.path == "main" else name_of.get(a.path, a.path)
            for a in facts.assignments
            if a.name == name and a.value is None
        ]
        scans[name] = HelperScan(
            function=_top(path),
            params=by_path[path].params,
            natives=tuple(own_natives),
            helpers=tuple(own_helpers),
            at_load=any(c.callee == name for c in main_refs),
            registered=any(name in c.args for c in main_refs),
            deleted_by=tuple(dict.fromkeys(deleted)),
        )
    return scans


def _global_values(facts: lua4.ChunkFacts, scans: dict[str, HelperScan]) -> dict[str, tuple[str, bool]]:
    """Each global the main chunk (or a helper it runs while loading) sets to a value other than a function: the
    kind of its last value and whether the main chunk ends by clearing it, in the order they are first set."""
    paths = natives_mission.helper_functions(facts)
    loading = {"main"} | {paths[name] for name, scan in scans.items() if scan.at_load}
    assigned: dict[str, list[lua4.Assignment]] = {}
    for assignment in facts.assignments:
        if assignment.path in loading and assignment.name not in scans:
            assigned.setdefault(assignment.name, []).append(assignment)
    values: dict[str, tuple[str, bool]] = {}
    for name, assignments in assigned.items():
        from_main = [a for a in assignments if a.path == "main"] or assignments
        last = from_main[-1]
        cleared = last.value is None and any(a.value is not None for a in assignments)
        values[name] = (_value_kind(last.value), cleared)
    return values


def survey(global_chunk: bytes, scripts: Iterable[tuple[str, bytes]], natives: frozenset[str]) -> Survey:
    """Survey `global.lua` (`global_chunk`) against the other compiled scripts (name, bytes); `natives` are the
    binding names. Only names and counts are kept."""
    main = lua4.parse_chunk(global_chunk)
    facts = lua4.walk_chunk(main)
    scans = _helper_scans(facts, main, natives)
    values = _global_values(facts, scans)
    callers: dict[str, set[str]] = {name: set() for name in scans}
    calls: dict[str, int] = dict.fromkeys(scans, 0)
    readers: dict[str, int] = dict.fromkeys(values, 0)
    for script, data in scripts:
        if script == HELPERS_SCRIPT:
            continue
        chunk = lua4.parse_chunk(data)
        for call in lua4.walk_chunk(chunk).calls:
            for item in [call.callee] + [arg for arg in call.args if isinstance(arg, str)]:
                if item in calls:
                    calls[item] += 1
                    callers[item].add(script)
        read: set[str] = set()
        for proto in natives_mission.functions_by_path(chunk).values():
            read |= natives_mission.names_in(proto, constants=False)
        for name in read & set(readers):
            readers[name] += 1
    helpers = {name: replace(scan, scripts=len(callers[name]), calls=calls[name]) for name, scan in scans.items()}
    found = {
        name: GlobalScan(scripts=readers[name], value=kind, cleared=cleared)
        for name, (kind, cleared) in values.items()
        if readers[name]
    }
    return Survey(hashlib.sha1(global_chunk).hexdigest(), helpers, found)


# --- writing the survey into the data file ----------------------------------------------------------------------------

_YAML_WORDS = {"true", "false", "yes", "no", "on", "off", "null", "y", "n", "~"}
_ENTRY = re.compile(r"^  - name: (?P<name>.+?)\s*$")
_SECTION = re.compile(r"^(?P<key>[A-Za-z_][A-Za-z0-9_]*):")


def _scalar(text: str) -> str:
    """A name as a YAML scalar: plain when YAML reads it back as the same string, quoted otherwise."""
    if _NAME.match(text) and text.lower() not in _YAML_WORDS:
        return text
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _names(items: Iterable[str]) -> str:
    """A list of names in flow style."""
    return "[" + ", ".join(_scalar(item) for item in items) + "]"


def helper_scan_line(scan: HelperScan) -> str:
    """A helper's `scan` line: empty lists and false flags are left out."""
    parts = [f"function: {scan.function}", f"params: {scan.params}"]
    parts += [f"natives: {_names(scan.natives)}"] if scan.natives else []
    parts += [f"helpers: {_names(scan.helpers)}"] if scan.helpers else []
    parts += [f"scripts: {scan.scripts}", f"calls: {scan.calls}"]
    parts += ["at_load: true"] if scan.at_load else []
    parts += ["registered: true"] if scan.registered else []
    parts += [f"deleted_by: {_names(scan.deleted_by)}"] if scan.deleted_by else []
    return "    scan: {" + ", ".join(parts) + "}"


def global_scan_line(scan: GlobalScan) -> str:
    """A global's `scan` line."""
    cleared = ", cleared: true" if scan.cleared else ""
    return f"    scan: {{scripts: {scan.scripts}, value: {scan.value}{cleared}}}"


def _entry_name(raw: str) -> str:
    """The name on an entry's first line, unquoted."""
    value = yaml.safe_load(raw)
    return value if isinstance(value, str) else raw


def _rewrite(lines: list[str], scan_lines: dict[str, str]) -> tuple[list[str], set[str]]:
    """`lines` (one section's entries) with each entry's `scan` line replaced by its new one, or dropped when the
    entry has none; returns the names seen."""
    out: list[str] = []
    seen: set[str] = set()
    current: str | None = None

    def close() -> None:
        # Put the finished entry's scan line after its last non-blank line.
        if current is None or current not in scan_lines:
            return
        end = len(out)
        while end and not out[end - 1].strip():
            end -= 1
        out.insert(end, scan_lines[current])

    for line in lines:
        match = _ENTRY.match(line)
        if match:
            close()
            current = _entry_name(match.group("name"))
            seen.add(current)
        if line.startswith("    scan: "):
            continue
        out.append(line)
    close()
    return out, seen


def _skeleton(name: str, scan_line: str, params: int | None) -> list[str]:
    """An entry for a name the file lacks, to be filled in by hand (parse refuses it until it is)."""
    lines = [f"  - name: {_scalar(name)}", '    group: ""']
    if params is not None:
        lines.append(f"    params: [{', '.join(f'p{i}' for i in range(1, params + 1))}]")
    return [*lines, '    description: ""', scan_line]


def apply_survey(text: str, result: Survey) -> tuple[str, list[str], list[str]]:
    """The data file's text with the survey written in: the `chunk_sha1` line, every entry's `scan` line, and a
    skeleton entry for each helper or global the file lacks. Returns the text, the names added and the names the
    file has but the survey does not (whose scan lines are dropped). Hand-written lines are kept as they were."""
    lines = text.splitlines()
    sections: dict[str, tuple[int, int]] = {}
    starts = [(i, m.group("key")) for i, line in enumerate(lines) if (m := _SECTION.match(line))]
    for n, (i, key) in enumerate(starts):
        sections[key] = (i, starts[n + 1][0] if n + 1 < len(starts) else len(lines))
    out: list[str] = []
    added: list[str] = []
    gone: list[str] = []
    # Per section: each surveyed name's scan line, and its parameter count (None for a global).
    wanted: dict[str, tuple[dict[str, str], dict[str, int | None]]] = {
        "helpers": (
            {k: helper_scan_line(v) for k, v in result.helpers.items()},
            {k: v.params for k, v in result.helpers.items()},
        ),
        "globals": ({k: global_scan_line(v) for k, v in result.globals.items()}, dict.fromkeys(result.globals)),
    }
    first = starts[0][0] if starts else len(lines)
    sections.setdefault("chunk_sha1", (first, first))
    for key in ("helpers", "globals"):
        sections.setdefault(key, (len(lines), len(lines)))
    order = {"chunk_sha1": 0, "helpers": 1, "globals": 2}
    position = 0
    for key, (start, end) in sorted(sections.items(), key=lambda item: (item[1][0], order.get(item[0], 3))):
        out += lines[position:start]
        position = end
        if key == "chunk_sha1":
            out.append(f"chunk_sha1: {result.chunk_sha1}")
            continue
        if key not in wanted:
            out += lines[start:end]
            continue
        scan_lines, params = wanted[key]
        body, seen = _rewrite(lines[start + 1 : end] if start < len(lines) else [], scan_lines)
        while body and not body[-1].strip():
            body.pop()
        missing = [name for name in scan_lines if name not in seen]
        for name in missing:
            body += ["", *_skeleton(name, scan_lines[name], params[name])]
        out += [f"{key}:", *body, ""]
        added += missing
        gone += sorted(seen - set(scan_lines))
    while out and not out[-1].strip():
        out.pop()
    return "\n".join(out) + "\n", added, gone
