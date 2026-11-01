# SPDX-License-Identifier: GPL-3.0-or-later
"""The Coney console's command catalogs: `src/console/commands.toml` (the player set, every build) and, once it
exists, `src/debug/console_commands.toml` (the developer set, builds with the debug tools).

Each `[[command]]` is one built-in command: its name, group, access level, scope, summary, arguments with their types,
what it needs, the Coney feature it waits for and at least one example line with its output. `console render-cpp`
turns the player file into the game library's table (console_cpp.py) and `console render` into the console commands
reference (console_render.py); this module reads and checks them, so both refuse the same mistakes: a developer entry
in the player file (or the other way round), a name in both files, an unknown type or key, an argument out of order,
a command without an example. docs/guides/console.md#catalog.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

PLAYER_FILE = Path("src/console/commands.toml")
DEVELOPER_FILE = Path("src/debug/console_commands.toml")

ACCESS = ("player", "developer")
SCOPES = ("local", "session")
NEEDS = ("game", "profile", "level", "disc")
ARG_TYPES = (
    "int",
    "number",
    "bool",
    "text",
    "choice",
    "mission",
    "checkpoint",
    "level",
    "mod",
    "setting",
    "command",
)
#: A command's name, as the registry takes it (src/console/command_spec.h, isCommandName).
NAME = re.compile(r"^[a-z][a-z0-9_-]*$")
#: A Coney feature's name in `available`, and an argument's name.
SLUG = re.compile(r"^[a-z][a-z0-9_-]*$")
#: The longest built-in name (kBuiltinNameMax).
NAME_MAX = 64

_COMMAND_KEYS = {
    "name",
    "group",
    "access",
    "scope",
    "changes",
    "summary",
    "doubles",
    "needs",
    "available",
    "args",
    "examples",
}
_REQUIRED_KEYS = ("name", "group", "access", "scope", "summary")
_ARG_KEYS = {"name", "type", "help", "optional", "default", "choices"}


@dataclass(frozen=True)
class Arg:
    """One argument of a command."""

    name: str
    type: str
    help: str
    optional: bool = False
    default: str = ""
    choices: tuple[str, ...] = ()


@dataclass(frozen=True)
class Example:
    """A line typed at the console and the lines it prints."""

    line: str
    output: tuple[str, ...]


@dataclass(frozen=True)
class Command:
    """One built-in command of a catalog."""

    name: str
    group: str
    access: str
    scope: str
    summary: str
    examples: tuple[Example, ...]
    changes: bool = False
    doubles: str = ""
    needs: tuple[str, ...] = ()
    available: str = ""
    args: tuple[Arg, ...] = ()

    @property
    def usage(self) -> str:
        """The usage line: `mission <mission> [checkpoint]`."""
        words = [self.name] + [f"[{a.name}]" if a.optional else f"<{a.name}>" for a in self.args]
        return " ".join(words)

    @property
    def queued(self) -> bool:
        """Whether it waits for the step boundary (runsAtStepBoundary in command_spec.h)."""
        return self.scope == "session" or self.changes


@dataclass
class Catalog:
    """One catalog file's commands, in the file's order, and what is wrong with it."""

    path: Path
    access: str
    commands: list[Command] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)


def _text(value: Any) -> bool:
    """Whether `value` is a non-empty string."""
    return isinstance(value, str) and bool(value.strip())


def _arg(where: str, raw: Any, problems: list[str]) -> Arg | None:
    """One argument table, or None after noting what is wrong with it."""
    if not isinstance(raw, dict):
        problems.append(f"{where}: an argument is a table")
        return None
    unknown = sorted(set(raw) - _ARG_KEYS)
    if unknown:
        problems.append(f"{where}: unknown argument key(s) {', '.join(unknown)}")
    name, kind = raw.get("name"), raw.get("type")
    if not isinstance(name, str) or not SLUG.match(name):
        problems.append(f"{where}: an argument needs a name of lower-case letters, digits, - and _")
        return None
    if kind not in ARG_TYPES:
        problems.append(f"{where}: argument {name}: type is one of {', '.join(ARG_TYPES)}")
        return None
    if not _text(raw.get("help")):
        problems.append(f"{where}: argument {name}: needs a help line")
    default = raw.get("default", "")
    optional = raw.get("optional", False)
    if not isinstance(default, str) or not isinstance(optional, bool):
        problems.append(f"{where}: argument {name}: default is a string and optional true or false")
        return None
    choices = raw.get("choices", [])
    if kind == "choice" and not (
        isinstance(choices, list) and choices and all(isinstance(c, str) and c for c in choices)
    ):
        problems.append(f"{where}: argument {name}: a choice argument lists its choices")
        return None
    if kind != "choice" and choices:
        problems.append(f"{where}: argument {name}: only a choice argument has choices")
    return Arg(
        name=name,
        type=kind,
        help=str(raw.get("help", "")),
        optional=optional or bool(default),
        default=default,
        choices=tuple(choices) if kind == "choice" else (),
    )


def _examples(where: str, name: str, raw: Any, problems: list[str]) -> tuple[Example, ...]:
    """The example lines, noting a command with none or an example that is not of this command."""
    examples: list[Example] = []
    if not isinstance(raw, list) or not raw:
        problems.append(f"{where}: needs at least one example: {{ line = ..., output = [...] }}")
        return ()
    for item in raw:
        line = item.get("line") if isinstance(item, dict) else None
        output = item.get("output") if isinstance(item, dict) else None
        if not _text(line) or not isinstance(output, list) or not all(isinstance(o, str) for o in output):
            problems.append(f'{where}: an example is {{ line = "...", output = ["...", ...] }}')
            continue
        assert isinstance(line, str)
        if line.split()[0] != name:
            problems.append(f"{where}: the example {line!r} does not start with the command's name")
        examples.append(Example(line=line, output=tuple(output)))
    return tuple(examples)


def _command(index: int, raw: Any, access: str, problems: list[str]) -> Command | None:
    """One `[[command]]` table, or None after noting what is wrong with it."""
    where = f"command {index + 1}"
    if not isinstance(raw, dict):
        problems.append(f"{where}: is not a table")
        return None
    name = raw.get("name")
    if isinstance(name, str):
        where = f"command {name}"
    unknown = sorted(set(raw) - _COMMAND_KEYS)
    if unknown:
        problems.append(f"{where}: unknown key(s) {', '.join(unknown)}")
    missing = [key for key in _REQUIRED_KEYS if key not in raw]
    if missing:
        problems.append(f"{where}: missing {', '.join(missing)}")
        return None
    if not isinstance(name, str) or not NAME.match(name) or len(name) > NAME_MAX:
        problems.append(f"{where}: a name is lower-case letters, digits, - and _, starting with a letter")
        return None
    if raw["access"] != access:
        problems.append(
            f"{where}: access is {raw['access']!r}, but this file holds the {access} set"
            " (a developer command goes in src/debug/console_commands.toml, a player one in src/console/commands.toml)"
        )
    if raw["scope"] not in SCOPES:
        problems.append(f"{where}: scope is local or session")
    if not _text(raw["group"]) or not _text(raw["summary"]):
        problems.append(f"{where}: group and summary are non-empty text")
    changes = raw.get("changes", False)
    if not isinstance(changes, bool):
        problems.append(f"{where}: changes is true or false")
        changes = False
    needs = raw.get("needs", [])
    if not isinstance(needs, list) or any(need not in NEEDS for need in needs):
        problems.append(f"{where}: needs lists some of {', '.join(NEEDS)}")
        needs = []
    available = raw.get("available", "")
    if not isinstance(available, str) or (available and not SLUG.match(available)):
        problems.append(f"{where}: available names a Coney feature in lower case (`mission-select`)")
        available = ""
    doubles = raw.get("doubles", "")
    if not isinstance(doubles, str):
        problems.append(f"{where}: doubles is text")
        doubles = ""
    args: list[Arg] = []
    raw_args = raw.get("args", [])
    if not isinstance(raw_args, list):
        problems.append(f"{where}: args is a list of tables")
        raw_args = []
    for raw_arg in raw_args:
        arg = _arg(where, raw_arg, problems)
        if arg is None:
            continue
        if args and args[-1].optional and not arg.optional:
            problems.append(f"{where}: the required argument {arg.name} follows an optional one")
        if any(a.name == arg.name for a in args):
            problems.append(f"{where}: two arguments are named {arg.name}")
        args.append(arg)
    return Command(
        name=name,
        group=str(raw["group"]),
        access=str(raw["access"]),
        scope=str(raw["scope"]),
        summary=str(raw["summary"]),
        examples=_examples(where, name, raw.get("examples"), problems),
        changes=changes,
        doubles=doubles,
        needs=tuple(needs),
        available=available,
        args=tuple(args),
    )


def parse(text: str, path: Path, access: str) -> Catalog:
    """The catalog `text` (the file at `path`, holding the `access` set), with its problems."""
    catalog = Catalog(path=path, access=access)
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        catalog.problems.append(f"{path.as_posix()}: not valid TOML ({error})")
        return catalog
    extra = sorted(set(data) - {"command"})
    if extra:
        catalog.problems.append(f"{path.as_posix()}: unknown top-level key(s) {', '.join(extra)}")
    raw_commands = data.get("command", [])
    if not isinstance(raw_commands, list):
        catalog.problems.append(f"{path.as_posix()}: commands are [[command]] tables")
        return catalog
    seen: set[str] = set()
    for index, raw in enumerate(raw_commands):
        problems: list[str] = []
        command = _command(index, raw, access, problems)
        catalog.problems += [f"{path.as_posix()}: {problem}" for problem in problems]
        if command is None:
            continue
        if command.name in seen:
            catalog.problems.append(f"{path.as_posix()}: the name {command.name} is used twice")
        seen.add(command.name)
        catalog.commands.append(command)
    return catalog


def _read(root: Path, path: Path, access: str) -> Catalog | None:
    """The catalog file at `root / path`, or None when it does not exist."""
    full = root / path
    if not full.is_file():
        return None
    try:
        text = full.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        catalog = Catalog(path=path, access=access)
        catalog.problems.append(f"{path.as_posix()}: cannot be read ({error})")
        return catalog
    return parse(text, path, access)


def load(root: Path) -> list[Catalog]:
    """Both catalogs that exist (the player file always, as a problem when missing), each with its problems, and a
    name in both files noted as a problem of the later one: one name, one command."""
    player = _read(root, PLAYER_FILE, "player")
    if player is None:
        player = Catalog(path=PLAYER_FILE, access="player", problems=[f"{PLAYER_FILE.as_posix()}: missing"])
    catalogs = [player]
    developer = _read(root, DEVELOPER_FILE, "developer")
    if developer is not None:
        player_names = {command.name for command in player.commands}
        for command in developer.commands:
            if command.name in player_names:
                developer.problems.append(
                    f"{DEVELOPER_FILE.as_posix()}: {command.name} is in both catalogs; a developer variant takes a "
                    "name of its own"
                )
        catalogs.append(developer)
    return catalogs
