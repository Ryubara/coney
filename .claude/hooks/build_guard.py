# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: refuse a raw `cmake --build`, `cmake --preset`, `ctest` or `ninja` command and point to
`coney-tools build`.

Several agents share one machine, and a full engine build uses every core. `coney-tools build` takes a build slot (at
most two at once, a fast lane for small jobs, a fair queue; docs/guides/coney-tools.md#build) and does configure,
build and ctest in one command, so a raw build is refused here. This hook, registered in `.claude/settings.json` for
the Bash and PowerShell tools, refuses a command that *starts* one of:

* `cmake` with `--build`, `--preset`, `-S` or `-B` (a configure or a build; `cmake -E` and `cmake --version` pass);
* `ctest` (not `ctest -N`, `--show-only`, `--version` or `--help`, which only list);
* `ninja` (not `ninja --version`, `-h` or `--help`).

A program counts only where a command starts (the start of a line, or after `;`, `&`, `|`, `(` or `$(`), so text that
merely mentions one passes: an `echo`, a commit message, a `grep`, the body of a here-document or here-string, and
`coney-tools build ...` itself. The build tool's own subprocesses are not tool calls, so they are never seen here.

It runs before every shell command, so it is standard-library only and imports nothing it does not use. Exit code 2
blocks the call and shows stderr to the agent; every other outcome (also input it cannot read) exits 0.
"""

from __future__ import annotations

import json
import re
import sys

#: Where a command starts: the line start or after `;`, `&`, `|`, `(` or `$(`, then optional `NAME=value` assignments.
_START = r"(?:^|[;&|(]|\$\()[ \t]*(?:[A-Za-z_]\w*=\S*[ \t]+)*"
#: A program by name, possibly with a path in front and `.exe` after, possibly quoted (`& "C:\...\cmake.exe" --build`).
_PATH = r"(?:\"[^\"\n]*[\\/]|'[^'\n]*[\\/]|[\"']?[^\s;&|()\"']*[\\/])?"
_PROGRAM = (
    _PATH + r"(?P<name>cmake|ctest|ninja)(?:\.exe)?[\"']?(?=[ \t]|$)(?P<args>[^\n;&|]*)"
)

_COMMAND = re.compile(_START + _PROGRAM, re.MULTILINE)
#: What a `cmake` command does that builds: configure by preset or by -S/-B, or build.
_CMAKE_BUILDS = re.compile(r"(?:^|[ \t])(?:--build|--preset|-S|-B)")
#: Options that only list or print for `ctest` and `ninja`.
_CTEST_LISTS = re.compile(
    r"(?:^|[ \t])(?:-N|--show-only(?:=\S+)?|--version|--help|-h)(?=[ \t]|$)"
)
_NINJA_PRINTS = re.compile(r"(?:^|[ \t])(?:--version|--help|-h)(?=[ \t]|$)")

#: A here-document (`<<EOF`, `<<-'EOF'`): its head line is kept, its body is text, not commands.
_HEREDOC = re.compile(
    r"(?P<head><<-?[ \t]*(?P<quote>['\"]?)(?P<tag>\w+)(?P=quote)[^\n]*)\n.*?^[ \t]*(?P=tag)[ \t]*$",
    re.DOTALL | re.MULTILINE,
)
#: A PowerShell here-string (`@'` ... `'@`).
_HERESTRING = re.compile(r"@(?P<quote>['\"])[ \t]*\n.*?\n(?P=quote)@", re.DOTALL)
#: A line continuation (a backslash, or a backtick in PowerShell, at the end of a line).
_CONTINUATION = re.compile(r"(?:\\|`)[ \t]*\n")

MESSAGE = (
    "Blocked: do not run cmake, ctest or ninja directly. Several agents share this machine, and "
    "`coney-tools build` takes a build slot (at most two builds at once, a fast lane for small jobs, a fair queue), "
    "and does configure, build and ctest in one command with a short summary and the full log in your scratch "
    "folder (docs/guides/coney-tools.md, 'build'). Use, from your worktree:\n"
    "  uv run --project python coney-tools build --agent <your id> --preset dev --test [-R <regex>] [-L <label>]\n"
    "(`--configure` forces a configure, `--priority` goes ahead in the queue, `--dir <checkout>` builds another "
    "worktree, `--build-dir <folder> --env CC=... --env CXX=...` a second configuration). It runs in the background "
    "well: add `run_in_background` and check the log. Exit code 75 means it waited its cap for a slot or for the "
    "build folder: do other work and run it again.\n"
)


def blocked(command: str) -> bool:
    """Whether the shell command starts a raw configure, build or test run."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    text = _CONTINUATION.sub(" ", text)
    for call in _COMMAND.finditer(text):
        name = call.group("name").lower()
        args = call.group("args")
        if name == "cmake" and _CMAKE_BUILDS.search(args):
            return True
        if name == "ctest" and not _CTEST_LISTS.search(args):
            return True
        if name == "ninja" and not _NINJA_PRINTS.search(args):
            return True
    return False


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
        command = data["tool_input"]["command"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    if isinstance(command, str) and blocked(command):
        sys.stderr.write(MESSAGE)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
