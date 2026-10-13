# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: refuse a raw `gh` command that writes an issue, and point to `coney-tools issue`.

Issues are filed and their bodies changed only through `coney-tools issue create`, `edit` and `link`, which check the
body against the issue form first (docs/guides/issues.md#filing). This hook, registered in `.claude/settings.json` for
the Bash and PowerShell tools, refuses what bypasses that check:

* `gh issue create`;
* `gh issue edit` with `--body`, `--body-file`, `-b` or `-F`;
* `gh api` calls that create an issue, change an issue's body or title, link a sub-issue, or send a GraphQL
  `createIssue`, `updateIssue` or `addSubIssue`.

Labels, titles, comments and closing an issue stay allowed. A command that names another repository (`-R owner/name`,
`--repo owner/name`, `GH_REPO=owner/name` or a `repos/owner/name` API path) and never names this one passes, so a
scratch or sibling repository can be worked on from here. This repository is the one the working directory's
`origin` remote points at, so no repository name is written down here.

A command is refused only where `gh` starts one (the start of a line, or after `;`, `&`, `|`, `(` or `$(`), so text
that merely mentions it passes: an `echo`, a commit message, a `grep`, and the body of a here-document or
here-string. `coney-tools`' own `gh` calls are subprocesses, not tool calls, so they are never seen here.

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
#: The `gh` program (also `gh.exe`), then the space before its subcommand.
_GH = r"gh(?:\.exe)?[ \t]+"

_CREATE = re.compile(_START + _GH + r"issue[ \t]+create\b", re.MULTILINE)
_EDIT_BODY = re.compile(
    _START + _GH + r"issue[ \t]+edit\b[^\n;&|]*?[ \t](?:--body(?:-file)?|-b|-F)(?![\w-])", re.MULTILINE
)
_API = re.compile(_START + _GH + r"api\b(?P<args>[^\n;&|]*)", re.MULTILINE)
#: The mutations that write an issue through GraphQL.
_MUTATION = re.compile(r"\b(?:createIssue|updateIssue|addSubIssue|removeSubIssue|reprioritizeSubIssue)\b")
#: The issue path of a `gh api` call: nothing after `issues` (the collection), or `/N`, or `/N/<what>`.
_ISSUE_PATH = re.compile(
    r"(?:^|[ \t])[\"']?/?[^\s\"']*?/issues(?P<rest>/\d+(?:/(?P<sub>[A-Za-z_]+))?)?[\"']?(?=[ \t]|$)"
)
_METHOD = re.compile(r"(?:^|[ \t])(?:-X|--method)(?:[ \t]+|=)(?P<method>\w+)", re.IGNORECASE)
_FIELD = re.compile(r"(?:^|[ \t])(?:-f|-F|--field|--raw-field)(?:[ \t]+|=)[\"']?(?P<name>[\w\[\]]+)")
_INPUT = re.compile(r"(?:^|[ \t])--input\b")
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
    "Blocked: issues are filed and edited only through coney-tools, which checks the body against the issue form "
    "first (docs/guides/issues.md, 'Filing an issue'). Draft: `uv run --project python coney-tools issue draft task "
    "> <file>`, fill it in, check it with `coney-tools issue check --title ... --body <file>`, then "
    "`coney-tools issue create --title ... --body <file>` (or `coney-tools issue edit N --body <file>`, or "
    "`coney-tools issue link N --parent P`). Labels, titles, comments and closing stay with `gh`.\n"
)


def _api_writes_issue(args: str) -> bool:
    """Whether the arguments of a `gh api` call create an issue, change its body or title, or link a sub-issue."""
    path = _ISSUE_PATH.search(args)
    if path is None:
        return False
    fields = {match.group("name") for match in _FIELD.finditer(args)}
    method = _METHOD.search(args)
    verb = method.group("method").upper() if method else ("POST" if fields or _INPUT.search(args) else "GET")
    sub = path.group("sub") or ""
    if sub.startswith("sub_issue"):
        return verb in ("POST", "PATCH", "PUT", "DELETE")
    if verb not in ("POST", "PATCH", "PUT"):
        return False
    if path.group("rest") is None:
        return True  # the collection: a new issue
    if sub:
        return False  # comments, labels, assignees ...
    return bool(fields & {"body", "title"}) or _INPUT.search(args) is not None


def writes_issue(command: str) -> bool:
    """Whether the shell command starts a raw `gh` call that files or rewrites an issue."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    text = _CONTINUATION.sub(" ", text)
    if _CREATE.search(text) or _EDIT_BODY.search(text):
        return True
    for call in _API.finditer(text):
        args = call.group("args")
        if re.search(r"(?:^|[ \t])graphql\b", args) and _MUTATION.search(text[call.start() :]):
            return True
        if _api_writes_issue(args):
            return True
    return False


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
