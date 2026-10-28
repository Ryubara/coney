# SPDX-License-Identifier: GPL-3.0-or-later
"""The old command and option names of coney-tools, kept working until the workflow cutover.

The renaming pass (docs/guides/coney-tools.md#renamed) gave many commands a name that says what they do, ends a clash
(`check changed` beside `task check`) or puts a pair in one group (`trace original` beside `trace coney`). A script, a
hook message or a person's habit that still types an old name must not break before the cutover, so `main` rewrites
the old words of a command line into the new ones before parsing, and says so on stderr in one line. The parsers, the
generated command reference and every guide know only the new names; this table is the one place the old ones live.

At the cutover every row whose `until` is `CUTOVER` is deleted (and this module with them once none is left); the rows
of the review commands that moved to `legacy` stay until that group goes.
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass

#: When the old name stops working: at the workflow cutover.
CUTOVER = "cutover"


@dataclass(frozen=True)
class Rename:
    """One command whose leading words `old` are now `new` (which may add an option, as `bindings scan-usage --story`
    does for `natives missions`)."""

    old: tuple[str, ...]
    new: tuple[str, ...]
    until: str = CUTOVER


@dataclass(frozen=True)
class OptionRename:
    """An option `old` of the command `command` (its new words) that is now `new`; `new` None means the option is gone
    because what it asked for is the default now."""

    command: tuple[str, ...]
    old: str
    new: str | None
    until: str = CUTOVER


def _r(old: str, new: str) -> Rename:
    """A rename given as two space-separated command lines."""
    return Rename(tuple(old.split()), tuple(new.split()))


#: Every renamed command, old words to new. A whole group renamed (`natives`) is a row of one word; a longer row for a
#: command in it wins over it (the longest match is used).
COMMANDS: tuple[Rename, ...] = (
    _r("workspace disk", "workspace check-space"),
    _r("repo check", "repo check-conventions"),
    _r("repo hygiene", "repo post-findings"),
    _r("repo labels", "repo sync-labels"),
    _r("repo check-title", "pr check-title"),
    _r("repo check-docs", "pr check-docs"),
    _r("repo check-body", "pr check-body"),
    _r("repo pr-health", "pr list-stuck"),
    _r("repo review", "legacy pr-review"),
    _r("repo check-review", "legacy check-pr-review"),
    _r("issue draft", "issue template"),
    _r("issue link", "issue set-parent"),
    _r("check", "ci"),
    _r("check changed", "ci run"),
    _r("check result", "ci result"),
    _r("wad names", "wad recover-names"),
    _r("wad scenes", "wad scene-info"),
    _r("xbox files", "xbox disc-files"),
    _r("xbox names", "xbox match-names"),
    _r("xbox resources", "xbox resource-info"),
    _r("xbox index", "xbox write-resource-index"),
    _r("xbox texture-map", "xbox write-texture-map"),
    _r("xbox textures", "xbox texture-info"),
    _r("progress show", "progress summary"),
    _r("progress sizes", "progress check-sizes"),
    _r("progress ghidra", "ghidra export-names"),
    _r("orig check", "orig check-tags"),
    _r("orig copy-check", "orig check-copy"),
    _r("orig queue", "orig legacy-queue"),
    _r("orig record", "orig record-findings"),
    _r("original", "orig"),
    _r("natives", "bindings"),
    _r("natives coney", "bindings sync-coney"),
    _r("natives cpp", "bindings render-cpp"),
    _r("natives mission1", "bindings scan-usage --first-mission"),
    _r("natives missions", "bindings scan-usage --story"),
    _r("refs extract", "refs refresh"),
    _r("pcsx2 release", "pcsx2 unclaim"),
    _r("pcsx2 check-ports", "pcsx2 check-claim"),
    _r("pcsx2 keys", "pcsx2 send-keys"),
    _r("pcsx2 prepare-state", "pcsx2 states patch"),
    _r("pcsx2 repack-state", "pcsx2 states repack"),
    _r("pcsx2 record", "trace original"),
    _r("pcsx2 clip", "clips record"),
    _r("pcsx2 recipe", "pcsx2 recipes"),
    _r("pcsx2 states use", "pcsx2 states hand-out"),
    _r("build release", "build unclaim"),
    _r("trace mission", "trace events"),
)

#: Every renamed option, under the new words of its command.
OPTIONS: tuple[OptionRename, ...] = (
    OptionRename(("workspace", "clean"), "--shell-hours", "--stale-wait-hours"),
    OptionRename(("extract",), "--xbox", "--xbox-disc"),
    OptionRename(("build",), "--slots-wait-timeout", "--max-wait"),
    OptionRename(("clips", "clean"), "--dry-run", None),
)


def _command_words(argv: Sequence[str]) -> list[str]:
    """The leading words of `argv` that name a command (up to the first option or the end)."""
    words: list[str] = []
    for word in argv:
        if word.startswith("-"):
            break
        words.append(word)
    return words


def _rename_command(argv: list[str]) -> tuple[list[str], str | None]:
    """`argv` with its old command words replaced, and the notice to print (None when nothing was old)."""
    words = _command_words(argv)
    matches = [rename for rename in COMMANDS if tuple(words[: len(rename.old)]) == rename.old]
    if not matches:
        return argv, None
    rename = max(matches, key=lambda found: len(found.old))
    count = len(rename.old)
    renamed = [*rename.new, *argv[count:]]
    old, new = " ".join(rename.old), " ".join(rename.new)
    return renamed, f"`{old}` is renamed to `{new}`; the old name stops working at the {rename.until}"


def _rename_options(argv: list[str]) -> tuple[list[str], list[str]]:
    """`argv` with the old options of its command replaced (or dropped), and one notice per option replaced."""
    notices: list[str] = []
    for option in OPTIONS:
        if tuple(argv[: len(option.command)]) != option.command:
            continue
        renamed: list[str] = []
        seen = False
        for index, word in enumerate(argv):
            if word == "--":
                renamed.extend(argv[index:])
                break
            name, equals, value = word.partition("=")
            if name != option.old or index < len(option.command):
                renamed.append(word)
                continue
            seen = True
            if option.new is not None:
                renamed.append(f"{option.new}{equals}{value}")
        if seen:
            command = " ".join(option.command)
            if option.new is None:
                notices.append(f"`{command} {option.old}` is the default now; the option goes at the {option.until}")
            else:
                notices.append(
                    f"`{command} {option.old}` is renamed to `{option.new}`; the old name stops working at the "
                    f"{option.until}"
                )
            argv = renamed
    return argv, notices


def rewrite(argv: Sequence[str]) -> tuple[list[str], list[str]]:
    """The command line `argv` (without the program name) in the new names, and the notices to print, one per old
    name it held. A command line in the new names comes back unchanged with no notice."""
    renamed, notice = _rename_command(list(argv))
    renamed, notices = _rename_options(renamed)
    return renamed, ([notice] if notice else []) + notices
