# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: refuse a raw `gh` command that files an issue or changes an issue's body, and point to
`coney-tools issue`.

An issue follows the issue form's headings (docs/guides/issues.md), and `coney-tools issue draft`, `check` and
`create` write and check the body against the form before anything is filed. A raw `gh issue create` skips that
check, so this hook, registered in `.claude/settings.json` for the Bash and PowerShell tools, refuses a command that
*starts* one of:

* `gh issue create`;
* `gh issue edit` with `--body`, `--body-file`, `-b` or `-F` (labels, titles and `--add-*` options pass);
* `gh api` against an `issues` path with `-X POST`, `-X PATCH`, `--method POST|PATCH` or a `title=` or `body=` field.

Comments, labels, closing and reading stay allowed. A command that names another repository (`-R owner/name`,
`--repo owner/name`, `GH_REPO=owner/name` or a `repos/owner/name` API path) and never names this one passes, so a
scratch or sibling repository can be worked on from here. This repository is the one the working directory's
`origin` remote points at, so no repository name is written down here.

A program counts only where a command starts (the start of a line, or after `;`, `&`, `|`, `(` or `$(`), so text that
merely mentions one passes: an `echo`, a commit message, a `grep`, the body of a here-document or here-string.
`coney-tools`' own `gh` calls are subprocesses, not tool calls, so they are never seen here.

It runs before every shell command, so it is standard-library only and imports nothing it does not use. Exit code 2
blocks the call and shows stderr to the agent; every other outcome (also input it cannot read) exits 0.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

#: Where a command starts: the line start or after `;`, `&`, `|`, `(` or `$(`, then optional `NAME=value` assignments.
_START = r"(?:^|[;&|(]|\$\()[ \t]*(?:[A-Za-z_]\w*=\S*[ \t]+)*"
_CREATE = re.compile(_START + r"gh\s+issue\s+create\b", re.MULTILINE)
_EDIT_BODY = re.compile(
    _START + r"gh\s+issue\s+edit\b[^\n;&|]*\s(?:--body|--body-file|-b|-F)(?=[\s=]|$)", re.MULTILINE
)
# An issue's comments, labels, reactions and assignees are not its body: those paths pass.
_API = re.compile(
    _START + r"gh\s+api\b[^\n;&|]*/issues(?!/\d+/(?:comments|labels|reactions|assignees))\b", re.MULTILINE
)
#: What makes a `gh api` call write: a POST or PATCH, or a title or body field.
_API_WRITES = re.compile(r"(?:-X\s*|--method\s+)(?:POST|PATCH)|\s-[fF]\s+(?:body|title)=", re.IGNORECASE)

#: A here-document (`<<EOF`, `<<-'EOF'`): its head line is kept, its body is text, not commands.
_HEREDOC = re.compile(
    r"(?P<head><<-?[ \t]*(?P<quote>['\"]?)(?P<tag>\w+)(?P=quote)[^\n]*)\n.*?^[ \t]*(?P=tag)[ \t]*$",
    re.DOTALL | re.MULTILINE,
)
#: A PowerShell here-string (`@'` ... `'@`).
_HERESTRING = re.compile(r"@(?P<quote>['\"])[ \t]*\n.*?\n(?P=quote)@", re.DOTALL)
#: A line continuation (a backslash, or a backtick in PowerShell, at the end of a line).
_CONTINUATION = re.compile(r"(?:\\|`)[ \t]*\n")

#: A repository a command names: `-R x/y`, `--repo x/y`, `GH_REPO=x/y` or an API path `repos/x/y`.
_NAMED = re.compile(
    r"""(?:[ \t](?:-R|--repo)[ \t=]+|\bGH_REPO=|\brepos/)["']?(?P<repo>[\w.-]+/[\w.-]+)""", re.IGNORECASE
)
#: A GitHub remote address, in the `https://host/owner/name(.git)` or `git@host:owner/name(.git)` form.
_REMOTE = re.compile(r"[/:](?P<repo>[\w.-]+/[\w.-]+?)(?:\.git)?/?$")

MESSAGE = (
    "Blocked: issues are filed and edited only through `coney-tools issue`, which checks the body against the issue "
    "form first (docs/guides/issues.md). Draft: `uv run --project python coney-tools issue draft task > <file>`, "
    "fill it in, then `coney-tools issue create --title ... --body <file>` (or `issue edit N --body <file>`). If "
    "those commands do not exist yet, do not file: write the drafted body (task form ### headings) to your scratch "
    "folder and list it under \"Needs an issue\" for the coordinator. Labels, titles, comments and closing stay "
    "allowed.\n"
)


def writes_issue(command: str) -> bool:
    """Whether the shell command starts a raw `gh` call that files or rewrites an issue."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    text = _CONTINUATION.sub(" ", text)
    if _CREATE.search(text) or _EDIT_BODY.search(text):
        return True
    return _API.search(text) is not None and _API_WRITES.search(text) is not None


def named_repositories(command: str) -> set[str]:
    """The `owner/name` repositories the command names, lower-cased."""
    return {match.group("repo").lower() for match in _NAMED.finditer(command)}


def this_repository(cwd: str) -> str | None:
    """The `owner/name` of the working directory's `origin` remote, lower-cased; None when it cannot be read."""
    try:
        done = subprocess.run(
            ["git", "-C", cwd or ".", "remote", "get-url", "origin"],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    match = _REMOTE.search(done.stdout.strip()) if done.returncode == 0 else None
    return match.group("repo").lower() if match else None


def blocked(command: str, cwd: str = "") -> bool:
    """Whether the command is refused: a raw issue write that does not name only some other repository."""
    if not writes_issue(command):
        return False
    named = named_repositories(command)
    if not named:
        return True
    # The command names repositories: it passes only when none is this one, and a repository that cannot be told
    # counts as this one.
    here = this_repository(cwd)
    return here is None or here in named


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
        command = data["tool_input"]["command"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    cwd = data.get("cwd") if isinstance(data.get("cwd"), str) else str(Path.cwd())
    if isinstance(command, str) and blocked(command, cwd):
        sys.stderr.write(MESSAGE)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
