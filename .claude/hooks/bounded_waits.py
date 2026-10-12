# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: refuse a shell command that waits in a loop with no bound, and name the bounded ways.

A background shell that loops `until grep -q done build.log; do sleep 20; done` waits for ever when the run it watches
dies before writing the line. It never ends, keeps its finished agent listed as running, and piles up: twenty such
shells, hours old, were once found and killed by hand (docs/guides/agents.md#bounded-waits). This hook, registered in
`.claude/settings.json` for the Bash and PowerShell tools, refuses a command holding a loop that

* is an `until ...; do ... done` or `while ...; do ... done` loop (bash), or a `while (...) { ... }`,
  `do { ... } until (...)` or `do { ... } while (...)` loop (PowerShell),
* sleeps in its body (`sleep`, `Start-Sleep`), and
* has no bound: no number compared in the loop (`[ $i -ge 30 ]`, `(( i < 30 ))`, `$SECONDS -lt 600`, PowerShell's
  `-lt`, `-gt` ...) and no `timeout N` in front of it on the command.

`for i in $(seq 1 N)`, `for ((...))` and PowerShell's `for (...)` count by themselves and are never refused. A loop
without a sleep (`while read line`) is not a wait. Text that only mentions a loop passes: a quoted string, a commit
message, the body of a here-document or here-string.

It runs before every shell command, so it is standard-library only. Exit code 2 blocks the call and shows stderr to
the agent; every other outcome (also input it cannot read) exits 0.
"""

from __future__ import annotations

import json
import re
import sys

#: A here-document (`<<EOF`, `<<-'EOF'`): its head line is kept, its body is text, not commands.
_HEREDOC = re.compile(
    r"(?P<head><<-?[ \t]*(?P<quote>['\"]?)(?P<tag>\w+)(?P=quote)[^\n]*)\n.*?^[ \t]*(?P=tag)[ \t]*$",
    re.DOTALL | re.MULTILINE,
)
#: A PowerShell here-string (`@'` ... `'@`).
_HERESTRING = re.compile(r"@(?P<quote>['\"])[ \t]*\n.*?\n(?P=quote)@", re.DOTALL)
#: What runs the string after it as a script (`bash -c '...'`, `pwsh -Command "..."`, `eval '...'`): that string is
#: code.
_RUNS_SCRIPT = re.compile(r"(?:^|[\s;&|(])(?:-c|-Command|eval)[ \t]+$", re.IGNORECASE)
#: A quoted string ('...' or "..."), which is text unless a shell runs it (`_RUNS_SCRIPT`).
_QUOTED = re.compile(r"'[^'\n]*'|\"(?:[^\"\\\n]|\\.)*\"")

#: Where a word starts a command: the line start or after `;`, `&`, `|`, `(`, `{` or `$(`.
_AT = r"(?:^|(?<=[;&|({\n])|(?<=\$\())[ \t]*"
#: bash: `until|while COND; do BODY done` (COND and BODY may span lines).
_BASH_LOOP = re.compile(_AT + r"(?:until|while)\b(?P<cond>.*?)(?:;|\n)[ \t\n]*do\b(?P<body>.*?)\bdone\b", re.DOTALL)
#: PowerShell: `while (COND) { BODY }`.
_PS_WHILE = re.compile(r"\bwhile[ \t]*\((?P<cond>.*?)\)[ \t\n]*\{(?P<body>.*?)\}", re.DOTALL | re.IGNORECASE)
#: PowerShell: `do { BODY } until|while (COND)`.
_PS_DO = re.compile(
    r"\bdo[ \t\n]*\{(?P<body>.*?)\}[ \t\n]*(?:until|while)[ \t]*\((?P<cond>.*?)\)[ \t]*(?:$|[;\n])",
    re.DOTALL | re.IGNORECASE | re.MULTILINE,
)
#: A sleep in a loop's body.
_SLEEP = re.compile(r"(?:^|[\s;&|({])(?:sleep|start-sleep)\b", re.IGNORECASE)
#: A bound: a number compared (bash `test` and PowerShell operators, arithmetic `(( ))`, `$SECONDS`).
_BOUND = re.compile(r"(?i:-(?:lt|le|gt|ge))\b|\(\([^)]*[<>][^)]*\)\)|\bSECONDS\b|\bStopwatch\b|\.Elapsed\b")
#: `timeout N` (coreutils), or `timeout -k 5 10m`, somewhere in front of a loop.
_TIMEOUT = re.compile(r"\btimeout(?:\.exe)?[ \t]+(?:-\S+[ \t]+(?:\d\S*[ \t]+)?)*\d")

MESSAGE = (
    "Blocked: this command waits in a loop with no bound (until/while ... sleep, with no counter, deadline or "
    "timeout). If what it waits for never happens (the run died, the line is never written), the shell waits for "
    "ever, keeps your agent listed as running and is left behind for somebody to kill (docs/guides/agents.md, "
    "'Bounded waits'). Bound the wait instead:\n"
    "  for i in $(seq 1 30); do grep -q done LOG && break; sleep 20; done   # at most 30 looks\n"
    "  timeout 600 bash -c 'until grep -q done LOG; do sleep 20; done'       # at most 10 minutes\n"
    "  PowerShell: for ($i = 0; $i -lt 30; $i++) { if (...) { break }; Start-Sleep 20 }\n"
    "For a build or a test run, start `coney-tools build ... --max-wait MIN` with run_in_background and read its log "
    "when you are told it finished; for any other long command, run_in_background and check its output file once in "
    "a while instead of looping in the shell.\n"
)


def _code(command: str) -> str:
    """The command with what is only text removed: here-document and here-string bodies, and quoted strings. A quoted
    string right after `-c`, `-Command` or `eval` is a script a shell runs, so it is kept, on lines of its own."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    out: list[str] = []
    position = 0
    for quoted in _QUOTED.finditer(text):
        before = text[position : quoted.start()]
        out.append(before)
        runs = _RUNS_SCRIPT.search(before) is not None
        out.append("\n" + quoted.group()[1:-1] + "\n" if runs else '""')
        position = quoted.end()
    out.append(text[position:])
    return "".join(out)


def _loops(code: str) -> list[tuple[int, str, str]]:
    """Every wait-shaped loop in the code: where it starts, its condition and its body."""
    found = []
    for pattern in (_BASH_LOOP, _PS_WHILE, _PS_DO):
        for loop in pattern.finditer(code):
            found.append((loop.start(), loop.group("cond"), loop.group("body")))
    return found


def blocked(command: str) -> bool:
    """Whether the shell command holds a loop that sleeps with no bound."""
    code = _code(command)
    for start, condition, body in _loops(code):
        if not _SLEEP.search(" " + body):
            continue
        if _BOUND.search(condition) or _BOUND.search(body):
            continue
        if _TIMEOUT.search(code[:start]):
            continue
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
