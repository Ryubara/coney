# SPDX-License-Identifier: GPL-3.0-or-later
"""The parameters of every `coney-tools` command, read from the argparse parsers and rendered as Markdown.

The command line is defined once, in the parsers. This module reads that tree (`read_groups`) and turns each command
into a synopsis and a table of its parameters (`render_group`), so the docs cannot drift from the code: the guides hold
the result in generated blocks (`command_reference`). A command or parameter with no help text cannot be documented
and is reported (`problems`). The synopsis is built here, not by argparse, so the text does not depend on the terminal's
width or the Python version.

Research: docs/guides/coney-tools.md#parameter-reference
"""

from __future__ import annotations

import argparse
import re
import textwrap
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

#: The docs' line length (.markdownlint.yaml, MD013); the synopsis and the prose are wrapped to it. Tables are not.
WIDTH = 120
#: Stands in for a space inside one part of a synopsis while it is wrapped, so a part is never split.
_HOLD = "\x00"
#: A Markdown code span without a backtick inside.
_CODE_SPAN = re.compile(r"(`[^`\n]*`)")
#: The characters at the end of a sentence; help text that ends otherwise gets a full stop before a note.
_SENTENCE_END = ".!?:"
#: A web address in help text, up to the space, closing bracket or comma that ends it.
_URL = re.compile(r"https?://[^\s),;]+")
#: What each Python type a parser converts to is called for a reader.
_TYPE_WORDS: dict[object, str] = {int: "integer", float: "number", Path: "path"}


@dataclass(frozen=True)
class Param:
    """One parameter of a command, as the table shows it."""

    #: The name used in messages: the option's first string (`--base`) or the positional's metavar (`PATHS`).
    name: str
    #: The table's first cell: every spelling of the option and its value, as a code span.
    signature: str
    #: A word for the kind of value: `flag`, `text`, `integer`, `number`, `path` or `choice`.
    type: str
    #: `required`, `none`, `off`, `on` or the default as a code span.
    default: str
    #: The help text with its notes (the choices, repeatable, exclusive options), escaped for a table cell.
    meaning: str
    #: Whether the parser gave the parameter any help text.
    has_help: bool


@dataclass(frozen=True)
class Entry:
    """One command: the words that name it, its help, its synopsis and its parameters."""

    path: tuple[str, ...]
    help: str
    synopsis: str
    params: list[Param] = field(default_factory=list)
    #: Sentences about the command as a whole (a group of options of which one is required).
    notes: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class Group:
    """A top-level group (`repo`, `build`, ...) with the entries of its commands, in definition order."""

    name: str
    help: str
    entries: list[Entry] = field(default_factory=list)


# --- reading the parsers --------------------------------------------------------------------------------------------


def _subparsers(parser: argparse.ArgumentParser) -> argparse._SubParsersAction[argparse.ArgumentParser] | None:
    """The action that holds the commands under `parser`, or None for a command that has none."""
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action
    return None


def _children(
    action: argparse._SubParsersAction[argparse.ArgumentParser],
) -> Iterator[tuple[str, argparse.ArgumentParser, str]]:
    """The commands of a subparsers action as (name, parser, help); an alias is not a command of its own and a command
    whose help is `argparse.SUPPRESS` is left out."""
    helps = {pseudo.dest: pseudo.help for pseudo in action._choices_actions}
    seen: set[int] = set()
    for name, child in action.choices.items():
        if id(child) in seen:
            continue
        seen.add(id(child))
        given = helps.get(name)
        if given == argparse.SUPPRESS:
            continue
        yield name, child, given or ""


def _shown(parser: argparse.ArgumentParser) -> list[argparse.Action]:
    """The actions of `parser` that are parameters a reader can give: not `-h`, `--version`, the commands or an
    argument whose help is suppressed."""
    skipped = (argparse._HelpAction, argparse._VersionAction, argparse._SubParsersAction)
    return [a for a in parser._actions if not isinstance(a, skipped) and a.help != argparse.SUPPRESS]


def _walk(parser: argparse.ArgumentParser, path: tuple[str, ...], help_text: str, program: str) -> Iterator[Entry]:
    """The entry of `parser` (when it takes parameters or is a command to run) and of every command under it;
    `program` is the name the user types before the path."""
    action = _subparsers(parser)
    params = _shown(parser)
    if action is None or params:
        yield _entry(parser, path, help_text, params, program)
    if action is not None:
        for name, child, child_help in _children(action):
            yield from _walk(child, (*path, name), child_help, program)


def read_groups(parser: argparse.ArgumentParser) -> dict[str, Group]:
    """The top-level groups of the command line `parser`, by name, each with the entries of its commands."""
    action = _subparsers(parser)
    groups: dict[str, Group] = {}
    if action is None:
        return groups
    for name, child, help_text in _children(action):
        groups[name] = Group(name, help_text, list(_walk(child, (name,), help_text, parser.prog)))
    return groups


def read_program(parser: argparse.ArgumentParser) -> Group:
    """A program of its own (`coney-agent`), which has no commands under it, as a group named after it with one entry;
    its help is the parser's description."""
    name = parser.prog
    help_text = " ".join((parser.description or "").split())
    return Group(name, help_text, [_entry(parser, (name,), help_text, _shown(parser), "")])


# --- one command ----------------------------------------------------------------------------------------------------


def _upper(action: argparse.Action) -> str:
    """The name argparse gives an argument's value when no metavar is set."""
    return action.dest.upper()


def _value(action: argparse.Action) -> str:
    """How the value of an argument that takes one is written: `FILE`, `[X]`, `X [X ...]`, `A B`. The choices are not
    part of it (a note lists them), so a long list does not fill the synopsis."""
    if isinstance(action.metavar, tuple):
        return " ".join(action.metavar)
    name = action.metavar if isinstance(action.metavar, str) else _upper(action)
    nargs = action.nargs
    if nargs is None:
        return name
    if nargs == "?":
        return f"[{name}]"
    if nargs == "*":
        return f"[{name} ...]"
    if nargs == "+":
        return f"{name} [{name} ...]"
    if nargs == argparse.REMAINDER:
        return "..."
    if isinstance(nargs, int):
        return " ".join([name] * nargs)
    return name


def _spelling(action: argparse.Action) -> str:
    """The option string a synopsis shows: the first long one, else the first."""
    return next((s for s in action.option_strings if s.startswith("--")), action.option_strings[0])


def _type_word(action: argparse.Action) -> str:
    """A word for the kind of value an argument takes."""
    if action.nargs == 0:
        return "flag"
    if action.choices is not None:
        return "choice"
    return _TYPE_WORDS.get(action.type, "text") if action.type is not None else "text"


def _escape(text: str, *, table: bool = False) -> str:
    """`text` with its white space collapsed and the characters Markdown would take for markup escaped; a code span
    is kept as it is (only a `|` in it is escaped in a table). A web address is put in a code span."""
    # A web address becomes a code span, so no renderer or linter takes it for a link of its own.
    parts = _CODE_SPAN.split(_URL.sub(lambda url: f"`{url.group(0)}`", " ".join(text.split())))
    out: list[str] = []
    for part in parts:
        if part.startswith("`") and part.endswith("`") and len(part) > 1:
            out.append(part.replace("|", "\\|") if table else part)
            continue
        part = part.replace("*", "\\*").replace("<", "\\<").replace(">", "\\>")
        out.append(part.replace("|", "\\|") if table else part)
    return "".join(out)


def _expand(action: argparse.Action, prog: str) -> str:
    """The help text with `%(default)s` and the like filled in, as `--help` shows it; as written when it will not."""
    text = action.help or ""
    fields = {key: value for key, value in vars(action).items() if value is not argparse.SUPPRESS}
    fields["prog"] = prog
    for key, value in list(fields.items()):
        if hasattr(value, "__name__"):
            fields[key] = value.__name__
    if fields.get("choices") is not None:
        fields["choices"] = ", ".join(str(choice) for choice in fields["choices"])
    try:
        return text % fields
    except (KeyError, TypeError, ValueError):
        return text


def _default(action: argparse.Action) -> str:
    """The default column: `required`, `none`, `off`/`on` for a flag, or the default value as a code span."""
    required = action.required if action.option_strings else action.nargs not in ("?", "*")
    if required:
        return "required"
    if action.nargs == 0:
        return "on" if action.default is True else "off"
    value = action.default
    if value is None or value == argparse.SUPPRESS or value in ("", [], ()):
        return "none"
    if isinstance(value, (list, tuple)):
        return "`" + ", ".join(str(item) for item in value) + "`"
    if isinstance(value, Path):
        return f"`{value.as_posix()}`"
    return f"`{value}`"


def _exclusive(parser: argparse.ArgumentParser) -> list[tuple[list[argparse.Action], bool]]:
    """The groups of mutually exclusive options of `parser` as (options, required)."""
    found: list[tuple[list[argparse.Action], bool]] = []
    for group in parser._mutually_exclusive_groups:
        members = [a for a in group._group_actions if a.option_strings and a.help != argparse.SUPPRESS]
        if len(members) > 1:
            found.append((members, group.required))
    return found


def _with_notes(text: str, notes: list[str]) -> str:
    """`text` followed by `notes`, a full stop added to the text when a note follows it."""
    if not notes:
        return text
    if text and text[-1] not in _SENTENCE_END:
        text += "."
    return " ".join([text, *notes] if text else notes)


def _param(parser: argparse.ArgumentParser, action: argparse.Action, others: list[argparse.Action]) -> Param:
    """The table row of one argument; `others` are the options it excludes."""
    if action.option_strings:
        value = "" if action.nargs == 0 else _value(action)
        signature = f"{', '.join(action.option_strings)} {value}".strip()
        name = action.option_strings[0]
    else:
        signature = _value(action)
        name = action.metavar if isinstance(action.metavar, str) else _upper(action)
    text = _expand(action, parser.prog)
    notes: list[str] = []
    if action.choices is not None:
        notes.append("One of " + ", ".join(f"`{choice}`" for choice in action.choices) + ".")
    if isinstance(action, (argparse._AppendAction, argparse._ExtendAction)) and "repeatable" not in text.lower():
        notes.append("Repeatable.")
    if others:
        notes.append("Not with " + ", ".join(f"`{_spelling(other)}`" for other in others) + ".")
    help_text = _escape(text, table=True)
    # The notes are built here from names and values, so only the cell's pipes need escaping.
    meaning = _with_notes(help_text, [note.replace("|", "\\|") for note in notes])
    return Param(name, f"`{signature}`", _type_word(action), _default(action), meaning, bool(action.help))


def _part(action: argparse.Action) -> str:
    """One argument as a synopsis part without brackets: `--x VALUE`, `--flag` or `NAME`."""
    if not action.option_strings:
        return _value(action)
    return f"{_spelling(action)} {_value(action)}".strip() if action.nargs != 0 else _spelling(action)


def synopsis(prog: str, parts: list[str]) -> str:
    """`prog` and the `parts` as lines of at most WIDTH characters, never splitting a part; the lines after the first
    are indented four spaces."""
    held = " ".join(part.replace(" ", _HOLD) for part in parts)
    lines = textwrap.wrap(
        f"{prog.replace(' ', _HOLD)} {held}".strip(),
        WIDTH,
        subsequent_indent="    ",
        break_long_words=False,
        break_on_hyphens=False,
    )
    return "\n".join(line.replace(_HOLD, " ") for line in lines)


def _parts(parser: argparse.ArgumentParser, params: list[argparse.Action]) -> list[str]:
    """The synopsis parts of `params`: positionals first, then the options in definition order, with the options of an
    exclusive group together in one bracket (parentheses when one is required)."""
    exclusive = _exclusive(parser)
    grouped = {id(a): (members, required) for members, required in exclusive for a in members}
    done: set[int] = set()
    parts = [_part(a) for a in params if not a.option_strings]
    for action in (a for a in params if a.option_strings):
        if id(action) in done:
            continue
        if id(action) in grouped:
            members, required = grouped[id(action)]
            done.update(id(member) for member in members)
            joined = " | ".join(_part(member) for member in members)
            parts.append(f"({joined})" if required else f"[{joined}]")
        elif action.required:
            parts.append(_part(action))
        else:
            parts.append(f"[{_part(action)}]")
    return parts


def _entry(
    parser: argparse.ArgumentParser, path: tuple[str, ...], help_text: str, params: list[argparse.Action], program: str
) -> Entry:
    """The entry of the command at `path`; `program` is what the user types before it ("" if the path has it)."""
    exclusive = _exclusive(parser)
    excluded: dict[int, list[argparse.Action]] = {}
    for members, _ in exclusive:
        for member in members:
            excluded[id(member)] = [other for other in members if other is not member]
    ordered = [a for a in params if not a.option_strings] + [a for a in params if a.option_strings]
    rows = [_param(parser, action, excluded.get(id(action), [])) for action in ordered]
    notes = [
        "Exactly one of " + ", ".join(f"`{_spelling(m)}`" for m in members) + " is required."
        for members, required in exclusive
        if required
    ]
    prog = " ".join(word for word in (program, *path) if word)
    return Entry(path, help_text, synopsis(prog, _parts(parser, params)), rows, notes)


# --- rendering ------------------------------------------------------------------------------------------------------


def _wrap(text: str) -> list[str]:
    """`text` as lines of at most WIDTH characters, never breaking inside a code span."""
    held = _CODE_SPAN.sub(lambda span: span.group(0).replace(" ", _HOLD), text)
    lines = textwrap.wrap(held, WIDTH, break_long_words=False, break_on_hyphens=False)
    return [line.replace(_HOLD, " ") for line in lines]


def _sentence(text: str) -> str:
    """`text` as a sentence: the first letter in capitals, a full stop at the end."""
    text = _escape(text)
    if not text:
        return text
    text = text[0].upper() + text[1:]
    return text if text[-1] in _SENTENCE_END else text + "."


def _anchor(path: tuple[str, ...]) -> str:
    """The id of the heading of the command at `path`; a group's own heading ends in `-parameters`, so a command that
    shares the group's name does not share its id."""
    return "-".join(path) + "-usage"


def render_entry(entry: Entry, heading: bool = True) -> list[str]:
    """The lines of one command: its heading (unless `heading` is off), help, notes, synopsis and table, or a line
    saying it takes nothing."""
    name = " ".join(entry.path)
    lines = [f"### `{name}` {{#{_anchor(entry.path)}}}", ""] if heading else []
    for text in (_sentence(entry.help), *entry.notes):
        if text:
            lines += [*_wrap(text), ""]
    lines += ["```text", entry.synopsis, "```", ""]
    if not entry.params:
        return [*lines, "Takes no parameters."]
    lines += ["| Parameter | Type | Default | Meaning |", "| --- | --- | --- | --- |"]
    for param in entry.params:
        lines.append(f"| {param.signature} | {param.type} | {param.default} | {param.meaning or ' '} |")
    return lines


def render_group(group: Group) -> list[str]:
    """The generated section of one group: a heading and a sentence, then every command of the group; no markers and
    no trailing blank line."""
    lines = [
        f"## Parameters of `{group.name}` {{#{group.name}-parameters}}",
        "",
        "Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.",
    ]
    # A group that is one command of its own name needs no second heading.
    alone = len(group.entries) == 1 and group.entries[0].path == (group.name,)
    for entry in group.entries:
        lines += ["", *render_entry(entry, heading=not alone)]
    return lines


# --- what a complete reference needs --------------------------------------------------------------------------------


def missing_help(groups: dict[str, Group]) -> list[str]:
    """The name of each group, command or parameter that has no help text (`g`, `g x`, `g x --bare`): it would have an
    empty row in the reference."""
    found: list[str] = []
    for group in groups.values():
        if not group.help:
            found.append(group.name)
        for entry in group.entries:
            name = " ".join(entry.path)
            if not entry.help and len(entry.path) > 1:
                found.append(name)
            found += [f"{name} {param.name}" for param in entry.params if not param.has_help]
    return found


def problems(groups: dict[str, Group]) -> list[str]:
    """One line per group, command or parameter that has no help text."""
    return [f"{name}: no help text" for name in missing_help(groups)]
