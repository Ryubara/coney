# SPDX-License-Identifier: GPL-3.0-or-later
"""Finding the `coney-tools` commands a message names and checking them against the real command line.

A refusal tells the engineer what to run, so the command must exist: a message that names a renamed command
(`original fails-first` for `orig fails-first`) sends the engineer to a deprecation notice at best and a dead end after
the cutover. The tests that hold messages to the command line use these helpers (test_command_names.py, and the tests
of `task review`, `pr verify` and `pr open`, whose refusals are built from pieces no scan of the source can join).
"""

import argparse
import functools
import re

from coney_tools import cli, renames

#: A code span of a message (backticks around one line).
_SPAN = re.compile(r"`([^`\n]+)`")
#: A word of a command's name (`orig`, `fails-first`).
_WORD = re.compile(r"[a-z][a-z0-9-]*$")
#: An option as a message writes it (`--record`), with a value after an `=` cut off.
_OPTION = re.compile(r"(--[a-z][a-z0-9-]*)(?:=.*)?$")


@functools.cache
def command_line() -> argparse.ArgumentParser:
    """The real command line, in the new names only (the old ones are rewritten before parsing)."""
    return cli._build_parser()


def _children(parser: argparse.ArgumentParser) -> dict[str, argparse.ArgumentParser]:
    """The commands directly under `parser` (empty for a command that takes only arguments)."""
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return dict(action.choices)
    return {}


def groups() -> set[str]:
    """The groups (first words) of the command line."""
    return set(_children(command_line()))


def _words(span: str) -> list[str]:
    """The span's leading run of command-name words: `orig fails-first --none "<reason>"` gives two."""
    words: list[str] = []
    for word in span.split():
        if not _WORD.match(word):
            break
        words.append(word)
    return words


def commands_in(text: str) -> list[str]:
    """Every command the code spans of `text` name, as the span writes it (`orig fails-first --none "<reason>"`).

    A span names a command when it starts with two name words (or one and an option) and the first is a group of the
    command line or the old name of a renamed command. Anything else in backticks (a file, a word, `xbox 0x000bd3c0`)
    is not one, and neither is a span that leaves words out (`pcsx2 ... --agent`).
    """
    found: list[str] = []
    for span in _SPAN.findall(text):
        words = _words(span)
        # Two words (`orig compare`), or one with an option (`build --agent`).
        named = len(words) >= 2 or (len(words) == 1 and any(token.startswith("--") for token in span.split()[1:]))
        elided = "..." in span or "…" in span  # `pcsx2 ... --agent`: words left out, so nothing to look up
        if named and not elided and (words[0] in _children(command_line()) or renames.rewrite(words)[1]):
            found.append(span)
    return found


def command_problem(command: str) -> str | None:
    """Why `command` (a span from `commands_in`) is not a command or option that exists, or None when it is."""
    words = _words(command)
    if renames.rewrite(words)[1]:
        return f"`{command}`: an old name, renamed (renames.py)"
    parser = command_line()
    for word in words:
        children = _children(parser)
        if not children:
            break  # the rest are arguments of the command
        if word not in children:
            return f"`{command}`: no command `{word}` here"
        parser = children[word]
    known = {option for action in parser._actions for option in action.option_strings}
    for token in command.split()[len(words) :]:
        option = _OPTION.match(token)
        if option and option.group(1) not in known:
            return f"`{command}`: the command has no option {option.group(1)}"
    return None


def unknown_commands(text: str) -> list[str]:
    """The commands `text` names that do not exist (`command_problem` says why, one entry each)."""
    return [problem for command in commands_in(text) if (problem := command_problem(command)) is not None]
