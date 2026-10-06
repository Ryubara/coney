# SPDX-License-Identifier: GPL-3.0-or-later
"""Which script bindings the first mission can call, read from the compiled scripts.

The first mission is `level99.lua` and its three chapter scripts. They call bindings directly and through the
helpers `global.lua` defines (`ObjectiveAdd`, `SuperRunScene`, ...), and when the mission ends the engine itself calls
two `global.lua` helpers by name: `UnlockAndLoad` (the mission-complete mode's `Enter`) and `runNextMission` (the
failure and pause menus). A binding counts as used by the mission when any of those functions names it, or when a
`global.lua` function they reach does, following names transitively (a function is reached when a reached function
names it as a global, a `Table.field` or a callback string). Nested functions of a reached function are reached too,
unless they are themselves named helpers. `global.lua`'s main chunk is not followed: it runs before every level and
belongs to the boot path.

The result is an upper bound: a branch the mission never takes still counts. It holds names only; no script text
is kept.

Research: docs/research/scripting.md#level99 (the mission's flow), docs/research/scripting.md#run-next-mission.
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Mapping

from coney_tools import lua4

#: The first mission's compiled scripts, by WAD name.
MISSION1_SCRIPTS = ("level99.lua", "level99_combat.lua", "level99_lesson1.lua", "level99_lesson2.lua")
#: The script whose helpers the mission's scripts call.
HELPERS_SCRIPT = "global.lua"
#: `global.lua` functions the engine calls by name during or at the end of a mission: the mission-complete mode's
#: `Enter` calls `UnlockAndLoad` (`0x0015cf70`, name at `0x0054f528`); the failure and pause menus call
#: `runNextMission` (name at `0x0054e798`).
ENGINE_CALLBACKS = ("UnlockAndLoad", "runNextMission")

_USAGE_LINE = re.compile(r"^(  usage: \{.*\bmission1: )(true|false)(\b.*)$")


def _opcode(word: int) -> str:
    """The name of an instruction's opcode ("END" for an unknown one)."""
    code = word & 0x3F
    return lua4.OPCODES[code] if code < len(lua4.OPCODES) else "END"


def names_in(proto: lua4.Proto) -> set[str]:
    """The names one function mentions, without its nested functions: every global it reads, every `Global.field`
    it reads (a `GETGLOBAL` followed by a `GETDOTTED`), and every string constant (callbacks are passed by name)."""
    found = {text for text in proto.strings if text}
    previous: str | None = None
    for word in proto.code:
        op = _opcode(word)
        operand = word >> 6
        if op == "GETGLOBAL":
            previous = proto.strings[operand] if operand < len(proto.strings) else None
            if previous:
                found.add(previous)
            continue
        if op == "GETDOTTED" and previous and operand < len(proto.strings) and proto.strings[operand]:
            previous = f"{previous}.{proto.strings[operand]}"
            found.add(previous)
            continue
        previous = None
    return found


def functions_by_path(main: lua4.Proto) -> dict[str, lua4.Proto]:
    """Every function of a chunk by its path ("main", "main/3", "main/3/0", ...), as `lua4.walk_chunk` names them."""
    found: dict[str, lua4.Proto] = {}

    def visit(proto: lua4.Proto, path: str) -> None:
        # Record this function, then its children in order.
        found[path] = proto
        for index, child in enumerate(proto.protos):
            visit(child, f"{path}/{index}")

    visit(main, "main")
    return found


def helper_functions(facts: lua4.ChunkFacts) -> dict[str, str]:
    """The named functions a chunk defines, name -> path: globals (`function Foo()`) and fields of global tables
    (`function Menu.onStart()`, `Menu = {onStart = function ...}`), up to three tables deep."""
    helpers = dict(facts.functions())

    def fields(prefix: str, table: lua4.Table, depth: int) -> None:
        # Name every function stored in this table, and look one level further into nested tables.
        for key, value in table.fields.items():
            if not isinstance(key, str):
                continue
            if isinstance(value, lua4.Function):
                helpers.setdefault(f"{prefix}.{key}", value.path)
            elif isinstance(value, lua4.Table) and depth < 3:
                fields(f"{prefix}.{key}", value, depth + 1)

    for name, table in facts.tables.items():
        fields(name, table, 1)
    return helpers


def reached_names(
    scripts: Iterable[lua4.Proto], helpers_main: lua4.Proto, roots: Iterable[str] = ENGINE_CALLBACKS
) -> set[str]:
    """Every name that the mission's scripts (all their functions) and the helpers they reach mention.

    `roots` are helper names reached from outside the scripts (the engine's callbacks).
    """
    helpers = helper_functions(lua4.walk_chunk(helpers_main))
    paths = functions_by_path(helpers_main)
    named = set(helpers.values())
    found: set[str] = set(roots)
    for main in scripts:
        for proto in functions_by_path(main).values():
            found |= names_in(proto)
    visited: set[str] = set()
    pending = [helpers[name] for name in found if name in helpers]
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        # A reached helper brings its unnamed nested functions (local functions, closures) with it.
        subtree = [p for p in paths if p == path or (p.startswith(path + "/") and p not in named)]
        for item in subtree:
            visited.add(item)
            new = names_in(paths[item]) - found
            found |= new
            pending += [helpers[name] for name in new if name in helpers]
    return found


def mission_bindings(names: Iterable[str], bindings: Iterable[str]) -> set[str]:
    """The binding names among `names`."""
    return set(names) & set(bindings)


def set_mission1(text: str, used: set[str]) -> str:
    """The YAML text of one data file with each entry's `usage.mission1` set from `used` (by name).

    Only the `mission1:` value of `usage:` lines changes, so comments and layout are kept; an entry without a usage
    line is left alone.
    """
    out: list[str] = []
    name: str | None = None
    for line in text.splitlines():
        if line.startswith("- name: "):
            name = line[len("- name: ") :].split("#")[0].strip()
        match = _USAGE_LINE.match(line)
        if match and name is not None:
            line = f"{match.group(1)}{'true' if name in used else 'false'}{match.group(3)}"
        out.append(line)
    return "\n".join(out) + "\n"


def scripts_from(chunks: Mapping[str, bytes]) -> tuple[list[lua4.Proto], lua4.Proto]:
    """Parse the mission's chunks and `global.lua` from raw bytes by WAD name; raises KeyError for a missing one."""
    return [lua4.parse_chunk(chunks[name]) for name in MISSION1_SCRIPTS], lua4.parse_chunk(chunks[HELPERS_SCRIPT])
