# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers of the Claude Code hooks: the settings file, the guard modules and reading a shell command.

`dispatch.py` is the one entry point of the hooks (docs/guides/agents.md#hooks); the rule modules next to it
(`roles.py`, `identity.py`) use this file. Standard library only, because the hooks run under
`uv run --no-project python`, which has no packages.
"""

from __future__ import annotations

import importlib.util
import ntpath
import os
import posixpath
import re
import sys
import tempfile
import time
import tomllib
from pathlib import Path
from types import ModuleType
from typing import Any

HOOKS = Path(__file__).resolve().parent
#: The checkout these hooks belong to.
CHECKOUT = HOOKS.parent.parent
CONFIG_FILE = HOOKS / "hooks.toml"
LOG_FILE = "coney-hooks.log"

#: A here-document (`<<EOF`, `<<-'EOF'`): its head line is kept, its body is text, not commands.
_HEREDOC = re.compile(
    r"(?P<head><<-?[ \t]*(?P<quote>['\"]?)(?P<tag>\w+)(?P=quote)[^\n]*)\n.*?^[ \t]*(?P=tag)[ \t]*$",
    re.DOTALL | re.MULTILINE,
)
#: A PowerShell here-string (`@'` ... `'@`).
_HERESTRING = re.compile(r"@(?P<quote>['\"])[ \t]*\n.*?\n(?P=quote)@", re.DOTALL)
#: A line continuation (a backslash, or a backtick in PowerShell, at the end of a line).
_CONTINUATION = re.compile(r"(?:\\|`)[ \t]*\n")
#: A quoted string, to blank out the text inside (the quotes and the length stay, so offsets still line up).
QUOTED = re.compile(r"'[^'\n]*'|\"(?:[^\"\\\n]|\\.)*\"")
#: Where a command starts: the line start or after `;`, `&`, `|`, `(` or `$(`, then `NAME=value` assignments.
_START = r"(?:^|[;&|(]|\$\()[ \t]*(?:[A-Za-z_]\w*=\S*[ \t]+)*"
#: A `git commit`, with the options git takes before the subcommand (`opts`).
GIT_COMMIT = re.compile(
    _START + r"(?:\S*[/\\])?git(?:\.exe)?(?P<opts>(?:[ \t]+(?:(?:-C|-c|--git-dir|--work-tree)[ \t]+\S+|-\S+))*)"
    r"[ \t]+commit(?![\w-])",
    re.MULTILINE,
)
#: `cd`, `pushd` or `Set-Location` and the folder they move to.
_CD = re.compile(
    r"(?:^|[;&|(]|\$\()[ \t]*(?:cd|pushd|Set-Location)[ \t]+(?:/d[ \t]+)?(?P<dir>\"[^\"\n]+\"|'[^'\n]+'|[^\s;&|]+)",
    re.MULTILINE | re.IGNORECASE,
)
_MSYS = re.compile(r"^/(?P<drive>[A-Za-z])/(?P<rest>.*)$")
#: A Windows drive path (`C:/x`, or `C:` and a backslash), which is absolute whatever system the hook runs on.
_DRIVE = re.compile(r"^[A-Za-z]:[\\/]")

_guards: dict[str, ModuleType] = {}


def load_config_checked(path: Path = CONFIG_FILE) -> tuple[dict[str, Any], str | None]:
    """The hooks' settings and the problem reading them (None when there is none).

    Anything that goes wrong (a missing file, text that is not UTF-8, invalid TOML, a settings table of the wrong
    shape) gives `{}` and the reason, so no rule is ever on because of a half-read file and no guard is ever off
    because of one: the guards do not depend on this file.
    """
    try:
        data = tomllib.loads(path.read_bytes().decode("utf-8"))
    except Exception as error:
        return {}, f"{path.name}: {type(error).__name__}: {error}"
    return data, None


def load_config(path: Path = CONFIG_FILE) -> dict[str, Any]:
    """The hooks' settings; `{}` when the file cannot be read (every rule then takes its default: off)."""
    return load_config_checked(path)[0]


def strings(value: object) -> list[str]:
    """A settings value that should be a list of strings, as one: anything else (a bare string, numbers) is dropped,
    so a half-wrong entry never turns into a substring test or a crash."""
    return [item for item in value if isinstance(item, str)] if isinstance(value, list) else []


def load_guard(name: str, folder: Path = HOOKS) -> ModuleType:
    """The hook script `<name>.py` of `folder` as a module, loaded once."""
    key = f"{folder}/{name}"
    if key not in _guards:
        spec = importlib.util.spec_from_file_location(f"coney_hook_{name}", folder / f"{name}.py")
        if spec is None or spec.loader is None:
            raise ImportError(name)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        _guards[key] = module
    return _guards[key]


def record(what: str) -> None:
    """Append a line to the hooks' log in the temp folder: a broken guard passes the call but leaves a trace."""
    try:
        with (Path(tempfile.gettempdir()) / LOG_FILE).open("a", encoding="utf-8") as log:
            log.write(f"{time.strftime('%Y-%m-%dT%H:%M:%S')} {what}\n")
    except OSError:
        return


def clean_command(command: str) -> str:
    """The command without the text that is not commands: here-document and here-string bodies, line continuations."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    return _CONTINUATION.sub(" ", text)


def mask_quotes(text: str) -> str:
    """The text with the inside of each quoted string blanked, so a word in a message is not read as a command."""
    return QUOTED.sub(lambda match: match.group(0)[0] + "x" * (len(match.group(0)) - 2) + match.group(0)[-1], text)


def is_git_commit(command: str) -> bool:
    """Whether the shell command runs `git commit` (also `git -C dir commit`, `git -c k=v commit`)."""
    return GIT_COMMIT.search(clean_command(command)) is not None


def is_drive_path(text: str) -> bool:
    """Whether the text starts with a Windows drive (`C:/x`, or `C:` and a backslash): absolute on any system, where
    `Path` on Linux and macOS would read it as a relative name."""
    return _DRIVE.match(text) is not None


def _folder(base: str, text: str, windows: bool | None = None) -> str:
    """The folder `text` names (quotes dropped, Git Bash's `/c/x` as `c:/x` on Windows), from `base` if relative.

    A command can carry a Windows path on any system (an agent on Linux told to look at a Windows worktree), so a drive
    path replaces the base everywhere and a path joined to a drive base follows Windows rules. `windows` says which
    system's rules apply to everything else; it defaults to the running one and is only set by the tests.
    """
    windows = os.name == "nt" if windows is None else windows
    target = text.strip("\"'")
    drive = _MSYS.match(target) if windows else None
    if drive:
        target = f"{drive.group('drive')}:/{drive.group('rest')}"
    style = ntpath if windows or is_drive_path(target) or is_drive_path(base) else posixpath
    return style.normpath(style.join(base, target) if base else target)


def commit_folders(command: str, cwd: str, windows: bool | None = None) -> list[str]:
    """The folder each `git commit` of the command runs in: the working directory moved by the last `cd` before it,
    then by that git's own `-C`. Another folder the command merely names (`git -C x log`) is not one of them."""
    text = clean_command(command)
    found: list[str] = []
    for commit in GIT_COMMIT.finditer(text):
        here = _folder("", cwd, windows) if cwd else ""
        for change in _CD.finditer(text[: commit.start()]):
            here = _folder(here, change.group("dir"), windows)
        for option in re.finditer(r"-C[ \t]+(\"[^\"\n]+\"|'[^'\n]+'|\S+)", commit.group("opts")):
            here = _folder(here, option.group(1), windows)
        found.append(here)
    return found


def checkout_of(path: str) -> Path | None:
    """The checkout (a folder holding `.git`) that a file path lies in; None for a path outside any, such as scratch."""
    here = Path(path).resolve()
    for folder in (here, *here.parents):
        if (folder / ".git").exists():
            return folder
    return None


def relative_path(path: str) -> str | None:
    """The path inside its checkout, lower case with forward slashes; None when it lies in no checkout."""
    root = checkout_of(path)
    if root is None:
        return None
    relative = Path(path).resolve().relative_to(root)
    return relative.as_posix().casefold()
