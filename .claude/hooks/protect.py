# SPDX-License-Identifier: GPL-3.0-or-later
"""What no session may touch, whatever its role: the Claude Code hooks' `protect` rules.

* **`~/.coney`**: it holds the identity files the hooks write for `coney-tools` (`identity.py`). The files are named by
  a hash of the token, so a listing does not give a token away, but nobody has a reason to look.
* **`~/.ssh`**: the person's private keys. A public key (`.pub`), `config` and `known_hosts` may be read.
* **Stamp logs and build records**: Edit and Write may not change a file under a `stamps`, `stamps.legacy-*` (an old
  log moved aside) or `build-runs` folder, and a shell write into one is refused (written by the stage commands only,
  docs/guides/stamps.md).
* **The SSH agent's keys**: `ssh-add -d` and `-D` delete the person's keys; listing them (`-l`) is fine.
* **Other processes**: the readers of other processes' command lines and environments (`/proc/<pid>/environ`,
  `wmic process`, `Win32_Process`), which would show another agent's token.

The rules are built in, not read from `hooks.toml`: a settings file that cannot be read must not take them away, and
no setting may weaken them. Only the fields of a call that name a path are read (`file_path`, `path`, `notebook_path`,
a Glob's `pattern`, a shell command's words, every string of an MCP tool's input), never a search pattern or the text a
file is given: `grep ".coney/identity" docs`, `git log -S`, a Grep pattern and an Edit's new text mention a path
without touching it. Neither does a shell command's text count, in the paths or in the refused commands: the pattern of
`grep` / `rg` / `Select-String` / `git log -S` / `git grep`, the `-k` expression of `pytest`, the message of
`git commit -m` and the title and body of `gh` only mention words (text that the shell would run, `$(...)` or a
backtick, still counts). Paths are normalised before they are matched (`./`, `..`, case, both slashes, `~`, `$HOME`,
`%USERPROFILE%`, a leading-dot wildcard such as `~/.con*`, and the aliases Windows gives a folder: trailing dots and
spaces, `SSH~1` for `.ssh`, and the real path where the system resolves a short name).

This is spelling: a path built at run time (`$(printf '.co%s' ney)`) gets through, which is why the identity files are
hashed and expire, and why a forgery is visible and deliberate, not impossible. Standard library only.
"""

from __future__ import annotations

import fnmatch
import os
import posixpath
import re
from typing import Any

import hooklib

#: A folder no session names.
PRIVATE_FOLDERS = (".coney",)
#: A folder of private keys, and the files in it that may be read.
KEY_FOLDERS = (".ssh",)
KEY_ALLOWED_NAMES = ("config", "known_hosts", "known_hosts.old")
KEY_ALLOWED_SUFFIXES = (".pub",)
#: A folder with one of these names holds a stamp log or the build records a stamp covers.
STAMP_FOLDERS = ("stamps", "build-runs")
#: A folder named so is an old stamp log that `task start --adopt-branch` moved aside; it is kept as it was.
STAMP_ARCHIVE_PREFIX = "stamps.legacy-"
#: Shell commands that are not for agents: (pattern over the command, what to do instead).
COMMANDS = (
    (
        # Any short-flag group that holds `d` or `D`: `-d`, `-D`, `-qD`, `-Dq`.
        r"\bssh-add\b[^\n;&|]*\s-[A-Za-z]*[dD][A-Za-z]*(?![\w-])",
        "Deleting keys from the person's SSH agent is not for agents; listing them (`ssh-add -l`) is fine.",
    ),
    (
        r"(?:>>?|\btee\b|\bcp\b|\bmv\b|\brm\b|\bsed\s+-i|\btruncate\b|\bdd\b|\bSet-Content\b|\bAdd-Content\b|\bOut-File\b)"
        r"[^\n;&|]*[/\\](?:stamps(?:\.legacy-[^\s/\\;&|]*)?|build-runs)[/\\]",
        "Only the stage commands write a stamp log or a build record; there is deliberately no way to do it by hand.",
    ),
    (
        r"/proc/[^\s/]*/(?:environ|cmdline)|\bwmic\b[^\n;&|]*\bprocess\b|\bWin32_Process\b",
        "Another process's command line or environment is not for agents to read.",
    ),
)
#: The fields of a built-in tool's input that name a path (a Glob's `pattern` is one too).
PATH_FIELDS = ("file_path", "path", "notebook_path")
WRITE_TOOLS = {"Edit": "file_path", "Write": "file_path", "MultiEdit": "file_path", "NotebookEdit": "notebook_path"}
SHELLS = ("Bash", "PowerShell")
#: Programs whose first plain argument, and whose pattern options, are text to search for and not paths.
_SEARCHERS = {"grep", "egrep", "fgrep", "rg", "ag", "ack", "select-string", "sls"}
_PATTERN_OPTIONS = {"-e", "--regexp", "-g"}
#: The options of the searchers that take a value which is not the pattern (so the pattern is the plain word after it):
#: grep's and rg's are case-sensitive (`-A` is not `-a`), PowerShell's are not.
_SEARCH_VALUE_OPTIONS = {
    "-A", "-B", "-C", "-d", "-D", "-f", "-m", "-t", "-T", "-j", "-M", "--include", "--exclude", "--exclude-dir",
    "--max-count", "--context", "--after-context", "--before-context", "--file", "--glob", "--type", "--type-not",
    "--threads", "--max-depth", "--max-columns", "--path-separator",
}  # fmt: skip
_POWERSHELL_VALUE_OPTIONS = {"-context", "-path", "-literalpath", "-include", "-exclude"}
_GIT_SEARCHES = {"log", "grep", "diff", "show", "blame"}
#: The git commands that take a message with `-m` / `--message`.
_GIT_MESSAGES = {"commit", "tag", "merge", "notes", "revert", "cherry-pick", "stash", "am"}
_GIT_VALUE_OPTIONS = {"-C", "-c", "--git-dir", "--work-tree", "--namespace", "--exec-path"}
#: The options of `gh` that take prose.
_GH_TEXT_OPTIONS = {"--title", "-t", "--body", "-b", "--message", "-m", "--notes", "-n"}
#: Where the home folder is spelled, replaced by `~` before matching.
_HOME = re.compile(r"(?:\$\{?home\}?|\$env:(?:home|userprofile)|\$userprofile|%userprofile%|%homepath%)", re.IGNORECASE)
_WORD = re.compile(r"""[^\s;&|()<>"'`=,]+""")
#: A part of a path that Windows made up for a long name: `SSH~1` for `.ssh`, `CONEY~1` for `.coney` (the stem is the
#: name without its dots, upper case, and the digit tells apart names with the same stem).
_SHORT_NAME = re.compile(r"^(?P<stem>[^~.]+)~\d+(?:\.[^.]*)?$")
_HAS_SHORT_NAME = re.compile(r"~\d")
#: Text a shell runs when it sits inside double quotes: a command substitution.
_RUNS = ("$(", "`")


def normalise(word: str) -> str:
    """A path as it is matched: `~` for the home folder, forward slashes, lower case, `./` and `..` folded away."""
    text = _HOME.sub("~", word).replace("\\", "/").casefold()
    folded = posixpath.normpath(text) if text else text
    return folded


def _segments(path: str) -> list[str]:
    """The parts of a normalised path. Windows ignores trailing dots and spaces in a part (`.ssh.` is `.ssh`)."""
    parts = (part.rstrip(". ") for part in normalise(path).split("/"))
    return [part for part in parts if part]


def _is_wildcard(part: str) -> bool:
    """Whether a path part has wildcard characters."""
    return any(char in part for char in "*?[")


def _is_folder(part: str, name: str) -> bool:
    """Whether a path part is the folder `name`: written out, as a wildcard that starts with a literal dot, or as the
    short name Windows gives it."""
    if part == name:
        return True
    if part.startswith(".") and _is_wildcard(part) and fnmatch.fnmatchcase(name, part):
        return True
    short = _SHORT_NAME.match(part)
    return short is not None and short.group("stem") == name.lstrip(".")


def folder_use(path: str, folders: tuple[str, ...]) -> tuple[str, str | None] | None:
    """If the path goes into one of the folders: (the folder, the first part after it or None). Else None.

    A wildcard part (`~/.con*`) counts only after another part, so `find . -name ".*"` is not a use of any folder.
    """
    parts = _segments(path)
    for index, part in enumerate(parts):
        for folder in folders:
            if _is_folder(part, folder) and (index > 0 or not _is_wildcard(part)):
                return folder, parts[index + 1] if index + 1 < len(parts) else None
    return None


def resolved(path: str) -> str | None:
    """The real path of a path with a short-name part (`SSH~1`), where the system resolves it, else None.

    A short name is the system's, so its stem need not be the folder's name; only a path that looks like one is looked
    up, because the lookup touches the disk.
    """
    if not _HAS_SHORT_NAME.search(path):
        return None
    try:
        real = os.path.realpath(path)
    except (OSError, ValueError):
        return None
    return real if real != path else None


def _key_file_allowed(name: str | None) -> bool:
    """Whether a file in a key folder may be read: a public key, `config`, `known_hosts`."""
    return name is not None and (name in KEY_ALLOWED_NAMES or name.endswith(KEY_ALLOWED_SUFFIXES))


def _split(command: str, escape: str) -> tuple[list[list[str]], bool]:
    """The commands of a shell line as lists of raw tokens (quotes kept); a command ends at `;`, `&`, `|` or a newline
    outside quotes. `escape` is the shell's escape character inside double quotes (a backslash for sh, a backtick for
    PowerShell). A quote that is never closed takes the rest of the line into its token (the second value is False)."""
    commands: list[list[str]] = [[]]
    token = ""
    quote = ""
    escaped = False

    def end_token() -> None:
        nonlocal token
        if token:
            commands[-1].append(token)
            token = ""

    for char in hooklib.clean_command(command):
        if quote:
            token += char
            if escaped:
                escaped = False
            elif char == escape and quote == '"':
                escaped = True
            elif char == quote:
                quote = ""
        elif char in "'\"":
            quote = char
            token += char
        elif char in ";&|\n":
            end_token()
            commands.append([])
        elif char in " \t":
            end_token()
        else:
            token += char
    end_token()
    return [tokens for tokens in commands if tokens], not quote


def _base(token: str) -> str:
    """The program a token names: its file name without quotes, folder and `.exe`, in lower case."""
    return posixpath.basename(token.strip("'\"").replace("\\", "/")).casefold().removesuffix(".exe")


def _program_index(tokens: list[str]) -> int | None:
    """Where the program is in a command's tokens (after the `NAME=value` assignments)."""
    for index, token in enumerate(tokens):
        if not ("=" in token and not token.startswith(("-", "'", '"'))):
            return index
    return None


def _option_values(tokens: list[str], options: set[str]) -> set[int]:
    """The tokens that are the value of one of the options (`-k x`, `--title=x`) in a command."""
    found: set[int] = set()
    for index, token in enumerate(tokens):
        if token in options and index + 1 < len(tokens):
            found.add(index + 1)
        elif token.startswith("--") and "=" in token and token.split("=", 1)[0] in options:
            found.add(index)
    return found


def _search_text(tokens: list[str], start: int) -> set[int]:
    """The pattern of a `grep`, `rg` or `Select-String` command: after `-e` / `--regexp` / `-Pattern`, else the first
    plain word that is not an option's value."""
    found = {
        index + 1
        for index, token in enumerate(tokens)
        if (token in _PATTERN_OPTIONS or token.casefold() == "-pattern") and index + 1 < len(tokens)
    }
    found |= {index for index, token in enumerate(tokens) if token.startswith("--regexp=")}
    if found:
        return found
    index = start + 1
    while index < len(tokens):
        token = tokens[index]
        if token.startswith("-"):
            takes_value = token in _SEARCH_VALUE_OPTIONS or token.casefold() in _POWERSHELL_VALUE_OPTIONS
            index += 2 if takes_value else 1
            continue
        return {index}
    return set()


def _git_text(tokens: list[str], start: int) -> set[int]:
    """The text of a git command: the search of `git log -S` and `git grep`, the message of `git commit -m`."""
    index = start + 1
    while index < len(tokens) and tokens[index].startswith("-"):
        index += 2 if tokens[index] in _GIT_VALUE_OPTIONS else 1
    if index >= len(tokens):
        return set()
    subcommand = tokens[index].strip("'\"")
    found: set[int] = set()
    if subcommand in _GIT_SEARCHES:
        for place, token in enumerate(tokens):
            if token in {"-S", "-G", "--grep", "-e"} and place + 1 < len(tokens):
                found.add(place + 1)
            elif token.startswith(("-S", "-G", "--grep=")) and len(token) > 2:
                found.add(place)
        if subcommand == "grep" and not found and index + 1 < len(tokens):
            found.add(next((p for p in range(index + 1, len(tokens)) if not tokens[p].startswith("-")), index + 1))
    elif subcommand in _GIT_MESSAGES:
        for place, token in enumerate(tokens[index + 1 :], index + 1):
            if token == "--message" or re.fullmatch(r"-[A-Za-z]*m", token):
                found.add(place + 1)
            elif token.startswith("--message=") or re.match(r"-m.", token):
                found.add(place)
    return found


def _text_tokens(tokens: list[str]) -> set[int]:
    """The tokens of one command that are text and not commands or paths: a search pattern, a commit message, a
    `pytest -k` expression, the prose of a `gh` command. Text that the shell would run (`$(...)`) is not."""
    start = _program_index(tokens)
    if start is None:
        return set()
    program = _base(tokens[start])
    found: set[int] = set()
    if program in _SEARCHERS:
        found |= _search_text(tokens, start)
    elif program == "git":
        found |= _git_text(tokens, start)
    elif program == "gh":
        found |= _option_values(tokens, _GH_TEXT_OPTIONS)
    if any(_base(token) in {"pytest", "py.test"} for token in tokens):
        found |= _option_values(tokens, {"-k"})
    return {index for index in found if index < len(tokens) and not any(run in tokens[index] for run in _RUNS)}


def _shell_commands(command: str, tool: str) -> list[list[str]]:
    """The commands of a shell line with their text arguments (patterns, messages) emptied."""
    escape = "`" if tool == "PowerShell" else "\\"
    commands = []
    split, closed = _split(command, escape)
    for tokens in split:
        # With a quote left open nothing is known about where the text ends, so all of it is read.
        text = _text_tokens(tokens) if closed else set()
        commands.append(['""' if index in text else token for index, token in enumerate(tokens)])
    return commands


def _shell_paths(command: str, tool: str = "Bash") -> list[str]:
    """The words of a shell command that can be paths, without the text of its commands (patterns, messages)."""
    return [word for tokens in _shell_commands(command, tool) for token in tokens for word in _WORD.findall(token)]


def _leaf_strings(value: Any) -> list[str]:
    """Every string inside a (possibly nested) tool input."""
    if isinstance(value, str):
        return [value]
    if isinstance(value, dict):
        return [text for item in value.values() for text in _leaf_strings(item)]
    if isinstance(value, list):
        return [text for item in value for text in _leaf_strings(item)]
    return []


def named_paths(payload: dict[str, Any]) -> list[str]:
    """The path candidates of a call: the path fields of a built-in tool, the words of a shell command, and for an
    MCP tool (a script's code, an output folder, ...) the words of every string of its input."""
    tool = str(payload.get("tool_name", ""))
    tool_input = payload.get("tool_input")
    if not isinstance(tool_input, dict):
        return []
    if tool in SHELLS:
        command = tool_input.get("command")
        return _shell_paths(command, tool) if isinstance(command, str) else []
    if tool.startswith("mcp__"):
        return [word for text in _leaf_strings(tool_input) for word in _WORD.findall(text)]
    fields = [*PATH_FIELDS, *(["pattern"] if tool == "Glob" else [])]
    return [value for field in fields for value in [tool_input.get(field)] if isinstance(value, str)]


def refusal(payload: dict[str, Any]) -> str | None:
    """The refusal of a call that touches what no agent may, or None."""
    tool = str(payload.get("tool_name", ""))
    for named in named_paths(payload):
        # A path with a Windows short-name part is also tried as the real path the system resolves it to.
        for path in (named, resolved(named) or named):
            reason = _path_refusal(path)
            if reason:
                return reason
    tool_input = payload.get("tool_input")
    if tool in WRITE_TOOLS and isinstance(tool_input, dict):
        target = tool_input.get(WRITE_TOOLS[tool])
        folders = _segments(target)[:-1] if isinstance(target, str) else []
        if any(name in STAMP_FOLDERS or name.startswith(STAMP_ARCHIVE_PREFIX) for name in folders):
            return (
                "Blocked: a stamp log or build record is written by the stage commands only, never by hand "
                "(docs/guides/stamps.md).\n"
            )
    if tool in SHELLS and isinstance(tool_input, dict) and isinstance(tool_input.get("command"), str):
        # The text of the commands (a grep pattern, a commit message) only mentions words, so it is left out.
        commands = _shell_commands(tool_input["command"], tool)
        command = "; ".join(" ".join(tokens) for tokens in commands)
        for pattern, hint in COMMANDS:
            if re.search(pattern, command, re.IGNORECASE):
                return f"Blocked: that command is not for agents. {hint}\n"
    return None


def _path_refusal(path: str) -> str | None:
    """The refusal of a path that goes into a private folder, or None."""
    if folder_use(path, PRIVATE_FOLDERS):
        return (
            "Blocked: ~/.coney holds the identity files the hooks write for coney-tools; nobody names it "
            "(docs/guides/agents.md#identity).\n"
        )
    used = folder_use(path, KEY_FOLDERS)
    if used and not _key_file_allowed(used[1]):
        return f"Blocked: ~/{used[0]}/ holds private keys, which an agent never reads.\n"
    return None
