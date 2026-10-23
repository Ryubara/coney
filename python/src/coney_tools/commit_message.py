# SPDX-License-Identifier: GPL-3.0-or-later
"""The check on a commit message's trailers, run by the git commit-msg hook (`coney-tools pr check-commit`).

The rules are `commits.trailers` of `.github/conventions.yml` (AGENTS.md, Commits and GitHub): the only trailer is the
AI co-author line (`Co-Authored-By: <model name> <noreply address>`), and no other e-mail address appears anywhere in
the message. When the commit is made by an agent whose model is known, the line must also name that model: the
Claude Code hooks write it to the identity file the agent's token names (`coney_tools.identity`,
docs/guides/agents.md#identity), read from the agent's own transcript, because the system reminder an agent copies
its trailer from names the coordinator's model, not the agent's (it credited Opus on Sonnet's commits twice on
2026-10-13).

This is the same check as `.claude/hooks/trailer_guard.py`, which runs after the commit inside Claude Code; this one
runs before it for everyone, through pre-commit. The model-name functions are copies of the hook's (a test holds them
equal) until the hook is retired.
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from coney_tools.conventions import Trailers

#: A version of a model id that is a build date (`claude-opus-4-1-20250805`), not part of the version.
_DATE = re.compile(r"\d{8}")
#: A trailer line: a token, a colon and a value (git's shape), or a continuation line that starts with blanks.
_TRAILER_LINE = re.compile(r"^(?:[A-Za-z][A-Za-z0-9-]*[ \t]*:[ \t]*\S.*|[ \t]+\S.*)$")
_TRAILER_KEY = re.compile(r"^(?P<key>[A-Za-z][A-Za-z0-9-]*)[ \t]*:[ \t]*(?P<value>.*)$")
#: The name and the address of a co-author value: `Claude Sonnet 5.5 <noreply@anthropic.com>`.
_CO_AUTHOR = re.compile(r"^(?P<name>[^<]*?)[ \t]*<(?P<address>[^>]*)>[ \t]*$")
#: The vendor's own noreply address, the one whose trailers name the Claude model the agent runs on.
_ANTHROPIC = "noreply@anthropic.com"
#: The line git puts between the message and the diff it shows with `git commit -v` (or commit.verbose), in the comment
#: character (`#` unless core.commentChar says another).
_SCISSORS = re.compile(r"^(?P<char>\S) -{20,} >8 -{20,}$")
#: The characters `core.commentChar=auto` picks from (git's `auto_comment_line_char`): the first that no line of the
#: message starts with.
_AUTO_CHARS = "#;@!$%^&|:"
#: What git's editor template starts with (after the comment character): when one is there, git strips comment lines.
_TEMPLATE = (
    "Please enter the commit message",
    "Please enter a commit message",
    "Changes to be committed:",
    "On branch ",
)
#: Messages git writes about other people's commits: not judged at all.
_NOT_JUDGED = ("Merge ", "Squashed commit of the following")
#: Messages git writes about an earlier commit of the same author (`git revert`, `--fixup`): no trailer is required.
_GENERATED = ('Revert "', "fixup! ", "squash! ", "amend! ")


def display_name(model: str) -> str | None:
    """The name a trailer uses for a model id (`claude-sonnet-5-5` gives `Claude Sonnet 5.5`); None if not a Claude id.

    The family is the one alphabetic part and the version its numeric parts joined with dots, whichever side of the
    family they sit (`claude-3-5-sonnet-20241022` and `claude-sonnet-4-5` both work); a build date and a context
    suffix (`[1m]`) are dropped. An id of any other shape has no name, so it is not judged.
    """
    text = re.sub(r"\[[^\]]*\]|@.*$", "", model.strip().lower())
    if not text.startswith("claude-"):
        return None
    parts = [p for p in text[len("claude-") :].split("-") if p and p != "latest" and not _DATE.fullmatch(p)]
    families = [p for p in parts if p.isalpha()]
    versions = [p for p in parts if p.isdigit()]
    if len(families) != 1 or not versions or len(families) + len(versions) != len(parts):
        return None
    return f"Claude {families[0].capitalize()} {'.'.join(versions)}"


def same_model(trailer_name: str, expected: str) -> bool:
    """Whether a trailer's name is the expected one, ignoring case, spacing and a parenthetical like `(1M context)`."""

    def normal(name: str) -> str:
        return " ".join(re.sub(r"\([^)]*\)", " ", name).casefold().split())

    return normal(trailer_name) == normal(expected)


def configured_comment_char(folder: Path | None = None) -> str | None:
    """The comment character `core.commentChar` sets, or None for the default and for `auto` (then the scissors line
    names it) and for a value git itself refuses (more than one character)."""
    try:
        done = subprocess.run(
            ["git", "config", "--get", "core.commentChar"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            cwd=folder,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    value = done.stdout.strip() if done.returncode == 0 else ""
    return value if len(value) == 1 else None


def _is_template(line: str, char: str) -> bool:
    """Whether a line is the first line of git's editor template in the comment character `char`."""
    return line.startswith(char + " ") and line[2:].startswith(_TEMPLATE)


def _message_lines(message: str, comment_char: str | None = None) -> list[str]:
    """The lines of the message git keeps.

    Nothing from the scissors line on (the diff `git commit -v` shows). Comment lines are dropped only when git will
    strip them, which is when the editor template is in the file: `git commit -m` and `-F` keep every line, so an
    address in a `#` line there lands in history and is checked. The comment character is the scissors line's, else
    `core.commentChar`'s, else the one the editor template is written in (`core.commentChar=auto` picks it per
    message), else `#`.
    """
    text = message.replace("\r\n", "\n").split("\n")
    char = comment_char or "#"
    for line in text:
        found = _SCISSORS.match(line)
        if found:
            char = found.group("char")
            break
    kept: list[str] = []
    for line in text:
        found = _SCISSORS.match(line)
        if found and found.group("char") == char:
            break
        kept.append(line)
    if comment_char is None:
        # `core.commentChar=auto` (or not set): git writes its template in the first of its characters that no line of
        # the message starts with, so the template names the character.
        char = next((c for c in _AUTO_CHARS if any(_is_template(line, c) for line in kept)), char)
    strips = any(_is_template(line, char) for line in kept)
    lines = [line.rstrip() for line in kept if not (strips and line.startswith(char))]
    while lines and not lines[-1]:
        lines.pop()
    return lines


def _trailer_block(lines: list[str]) -> list[str]:
    """The lines of the trailer block: the last paragraph when every line of it is trailer-shaped and it is not the
    first paragraph (the title `tools: Add it` is shaped like a trailer)."""
    start = len(lines)
    while start > 0 and lines[start - 1]:
        start -= 1
    block = lines[start:]
    if start == 0 or not block or not all(_TRAILER_LINE.match(line) for line in block):
        return []
    return block


def check_commit_message(
    message: str,
    rules: Trailers,
    model: str | None = None,
    comment_char: str | None = None,
    *,
    emails: tuple[str, ...],
) -> list[str]:
    """What is wrong with a commit message's trailers and addresses: one reason each; empty when it passes.

    `model` is the id of the model the committing agent runs on (`claude-sonnet-5-5`), or None/empty when the author
    is not a known agent; then the trailer names are not judged, only their shape. `comment_char` is git's
    `core.commentChar` (`configured_comment_char`). `emails` are the regular expressions of an e-mail address, the
    project's (`hygiene.attachments.patterns.email` of `.github/conventions.yml`, the pattern the attachment scan uses
    too): none is kept here, so a package pin (`markdownlint-cli2@0.18.1`) is judged the way everywhere else.
    """
    if message.startswith(_NOT_JUDGED):
        return []
    lines = _message_lines(message, comment_char)
    co_author = rules.ai_co_author
    noreply = {address.casefold() for address in co_author.noreply}
    problems: list[str] = []
    names: list[tuple[str, str]] = []
    for line in _trailer_block(lines):
        found = _TRAILER_KEY.match(line)
        if found is None:
            continue
        if found.group("key").casefold() != co_author.key.casefold():
            if rules.only_ai_co_author:
                problems.append(f"the trailer {line!r} is not allowed: the only trailer is {co_author.key}")
            continue
        who = _CO_AUTHOR.match(found.group("value"))
        if who is not None:
            names.append((who.group("name"), who.group("address").strip().casefold()))
    if not rules.other_emails:
        text = "\n".join(lines)
        addresses = {match.group(0).casefold() for pattern in emails for match in re.finditer(pattern, text)}
        for address in sorted(addresses - noreply):
            problems.append(f"the e-mail address {address} is not allowed in a message (only the model noreply ones)")
    expected = display_name(model) if model else None
    if expected is not None:
        trailer = f"{co_author.key}: {expected} <{_ANTHROPIC}>"
        ours = [name for name, address in names if address == _ANTHROPIC]
        if not ours and not message.startswith(_GENERATED):
            problems.append(f"an agent ends its commit message with `{trailer}` and this has none")
        elif any(not same_model(name, expected) for name in ours):
            problems.append(f"a {co_author.key} trailer names another model than the one you run on; use `{trailer}`")
    return problems
