# SPDX-License-Identifier: GPL-3.0-or-later
"""The index `coney-tools lua decompile --all` writes beside the decompiled scripts: per script, its functions, the
natives each calls and the handlers it registers.

It is where level research starts (docs/guides/research-workflow.md#scripts): find the script and the function that
sets a checkpoint or registers a message handler, then open that function in the decompile. The facts come from
`coney_tools.lua4.walk_chunk`, which walks each function's code with a symbolic stack, so a registration's constant
arguments (a message name, the name of the function to call, a delay) are kept, and from the chunk's prototypes,
which keep the line each function was defined on (the line the decompile puts it on).

* **Functions** are named by the global or global table field that holds them (`Setup`, `Mission.Setup`); one held
  only in a local or passed as an argument has no name and is known by its path (`main/3/0`: the first function
  defined inside the chunk's fourth).
* **Natives** are the callees found in the game's binding list (research/bindings, `natives.load`); every other
  callee is a call of the script's own (or a preloaded script's) function.
* **Registrations** are calls of a native named in HANDLER: a message handler, a callback, an update function or a
  scheduled call, with their arguments as plain data (`lua4.plain`; a function argument is `{"function": path}`).

The index is written outside the repository with the scripts (LEGAL.md#no-game-data: it holds the scripts' names
and constants). It carries no marker line: it is not a decompile.
"""

from __future__ import annotations

import json
import re
from dataclasses import asdict, dataclass, field
from pathlib import Path

import lua4 as decompiler
from coney_tools import lua4

INDEX_JSON = "index.json"
INDEX_MD = "index.md"
#: The natives that register code to run later: message handlers, callbacks, update functions, scheduled calls and
#: trigger spheres (`SetMsgHandler`, `HuSetWorkoutCallbacks`, `RegisterUpdate`, `ScheduleFuncArg`,
#: `TriggerSphereCfg`).
HANDLER = re.compile(r"(?:Handlers?|Callbacks?)$|^RegisterUpdate$|^ScheduleFunc|^TriggerSphereCfg$")
#: The name the main chunk (the code that runs when the script is loaded) is listed under.
MAIN_NAME = "(main chunk)"


@dataclass(frozen=True)
class Registration:
    """One call of a native in HANDLER, with its arguments as plain data."""

    native: str
    args: list[object]


@dataclass
class Function:
    """One function of a script: its path, name (None when nothing global holds it), the line it was defined on, the
    natives and other functions it calls (each once, in first-call order) and the handlers it registers."""

    path: str
    name: str | None
    line: int
    natives: list[str] = field(default_factory=list)
    calls: list[str] = field(default_factory=list)
    handlers: list[Registration] = field(default_factory=list)


@dataclass
class Script:
    """One script's index."""

    name: str
    functions: list[Function]


def _names(facts: lua4.ChunkFacts) -> dict[str, str]:
    """Path -> the global, or global table field, that holds the function there."""
    found = {path: name for name, path in facts.functions().items()}

    def fields(prefix: str, table: lua4.Table, depth: int) -> None:
        # A table's fields, and the fields of the tables in it (a few levels: `Mission.Stage.Setup`).
        for key, value in table.fields.items():
            if isinstance(value, lua4.Function):
                found.setdefault(value.path, f"{prefix}.{key}")
            elif isinstance(value, lua4.Table) and depth < 3:
                fields(f"{prefix}.{key}", value, depth + 1)

    for name, table in facts.tables.items():
        fields(name, table, 0)
    return found


def _arg(value: lua4.Value) -> object:
    """An argument as plain data; a function the chunk defines as `{"function": path}`."""
    return {"function": value.path} if isinstance(value, lua4.Function) else lua4.plain(value)


def index_script(name: str, data: bytes, natives: frozenset[str]) -> Script:
    """The index of the chunk `data` named `name`, with `natives` the binding names."""
    facts = lua4.walk_chunk(lua4.parse_chunk(data))
    names = _names(facts)
    functions = {
        path: Function(path, MAIN_NAME if path == "main" else names.get(path), proto.line_defined)
        for path, proto in decompiler.load(data).walk()
    }
    for call in facts.calls:
        entry = functions.get(call.path)
        if entry is None or call.callee == "?":
            continue
        listed = entry.natives if call.callee in natives else entry.calls
        if call.callee not in listed:
            listed.append(call.callee)
        if call.callee in natives and HANDLER.search(call.callee):
            entry.handlers.append(Registration(call.callee, [_arg(value) for value in call.args]))
    return Script(name, list(functions.values()))


def _call_text(registration: Registration) -> str:
    """A registration as Lua-like text: `SetMsgHandler("MSG", "OnMsg")`."""
    return f"{registration.native}({', '.join(json.dumps(arg) for arg in registration.args)})"


def _markdown(scripts: list[Script], source: str) -> str:
    """The index for reading: a section per script, a line per function."""
    lines = [
        "# Decompiled scripts",
        "",
        f"Written by `coney-tools lua decompile --all` from {source}. Each script's functions, with the line the",
        "decompile puts them on, the natives they call, the script functions they call and the handlers they",
        "register. Research pages describe what a script does in our own words and cite the script and function;",
        "never copy a decompile or this index into the repository (LEGAL.md#no-game-data).",
    ]
    for script in scripts:
        lines += ["", f"## {script.name}", ""]
        for entry in script.functions:
            title = f"`{entry.name}`" if entry.name else "a function with no global name"
            parts = [f"natives {', '.join(f'`{n}`' for n in entry.natives)}"] if entry.natives else []
            parts += [f"calls {', '.join(f'`{n}`' for n in entry.calls)}"] if entry.calls else []
            parts += [f"registers {', '.join(f'`{_call_text(h)}`' for h in entry.handlers)}"] if entry.handlers else []
            where = f"{entry.path}, line {entry.line}" if entry.line else entry.path
            lines.append(f"- {title} ({where})" + (": " + "; ".join(parts) if parts else ""))
    return "\n".join(lines) + "\n"


def write_index(folder: Path, scripts: list[Script], source: str) -> None:
    """Write `index.json` and `index.md` into `folder`; `source` says what the scripts came from ("467 chunks")."""
    data = {
        "source": source,
        "scripts": {script.name: {"functions": [asdict(entry) for entry in script.functions]} for script in scripts},
    }
    folder.mkdir(parents=True, exist_ok=True)
    (folder / INDEX_JSON).write_bytes((json.dumps(data, indent=1, ensure_ascii=False) + "\n").encode("utf-8"))
    (folder / INDEX_MD).write_bytes(_markdown(scripts, source).encode("utf-8"))
