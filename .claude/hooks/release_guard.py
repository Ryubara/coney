# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: open or edit a pull request only with a title and description that pass CI's checks.

The coordinator opens each pull request from an agent's description file with `gh pr create --title ... --body-file
<file>` (docs/guides/how-we-work.md#release-queue). The rule is that the file passed `coney-tools repo check-review`
first, which runs the same title and body checks as CI's required `title` and `body` jobs; but nothing ran it, and on
2026-10-20 and 21 six pull requests were pushed whose descriptions CI's `body` job then refused (a missing section or
a local image not attached), each costing a CI round and an edit. This hook, registered in
`.claude/settings.json` for the Bash and PowerShell tools, runs those two checks before the command does, from the main
checkout's `coney-tools`:

* `gh pr create` must give `--title` and `--body-file <file>` (not `--body`, `--fill`, `--web` or a body on stdin,
  which the check cannot see), and both must pass `repo check-title` and `repo check-body --title`; a local image is
  accepted when the command has `--attach`, which uploads it;
* `gh pr edit` with `--title` or `--body-file` runs the same check on what it changes, and refuses `--body`.

A command that names only another repository (`-R owner/name` or `--repo`, before or after `pr`) passes. A command
counts only where `gh` starts one: the line start; after `;`, `&`, `|`, `(`, `{`, `$(`, a PowerShell assignment
(`$url = gh ...`) or the `&` call operator; after `then`, `do`, `env`, `command`, `exec` and the like; or inside
`bash -c "..."` or `pwsh -Command "..."`. Text that mentions one passes, also inside quotes, a here-document or a
here-string. A relative `--body-file` is found from the directory a leading `cd`, `pushd` or `Set-Location` moved to.
A title or file that is a shell expansion (`"$TITLE"`, `$(head -1 PR.md)`, `(Get-Content ...)`) is not checked, and
`--dry-run` creates nothing, so it passes. It fails open when it cannot tell: an argument list it cannot split, a
directory or file it cannot find (gh then fails by itself), `coney-tools` not starting, crashing or its rules
unusable. Exit code 2 blocks the call and shows stderr to the agent. Standard library only; the checks themselves run
in a subprocess. Not covered: a `gh` started through another program (`xargs` with a placeholder, a script file) or
through a quoted full path.
"""

import json
import os
import re
import shlex
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

#: Words that may stand before a program: shell keywords, `env`, `command`, `exec` and the like with their options,
#: `NAME=value` assignments and PowerShell's `&` call operator.
_PREFIX = (
    r"(?:(?:then|do|else|elif|time|!|env|command|exec|nohup|sudo|xargs)(?:[ \t]+-[^\s;&|()]+)*[ \t]+"
    r"|[A-Za-z_]\w*=[^\s;&|()]*[ \t]+|&[ \t]*)*"
)
#: Where a command starts: the line start, after `;`, `&`, `|`, `(`, `)`, `{`, `}` or `$(`, or after a PowerShell
#: assignment (`$url =`); then the prefix words.
_START = r"(?:^|[;&|(){}]|\$\(|\$[\w:]+[ \t]*=)[ \t]*" + _PREFIX
#: One argument, quoted or not.
_ARG = r"(?:\"[^\"\n]*\"|'[^'\n]*'|[^\s;&|()]+)"
#: The `gh` program, also by a path (`/usr/bin/gh`, `C:\tools\gh.exe`).
_GH = r"(?:[^\s;&|()'\"]*[/\\])?gh(?:\.exe)?"
#: `gh pr create` or `gh pr edit` where a command starts, with gh's own options (`-R owner/name`) before `pr`.
_PR_COMMAND = re.compile(
    _START
    + _GH
    + r"(?P<globals>(?:[ \t]+(?:(?:-R|--repo|--hostname)(?:[ \t]+|=)"
    + _ARG
    + r"|--?[\w-]+(?:="
    + _ARG
    + r")?))*)[ \t]+pr[ \t]+(?P<verb>create|edit)\b",
    re.MULTILINE,
)
#: A change of directory where a command starts: `cd`, `pushd`, `chdir`, `Set-Location` or `Push-Location`.
_CD = re.compile(
    _START + r"(?:cd|pushd|chdir|Set-Location|Push-Location|sl)(?:[ \t]+-\w+)*[ \t]+(?P<dir>" + _ARG + ")",
    re.MULTILINE | re.IGNORECASE,
)
#: `bash -c "..."`, `sh -lc '...'`, `pwsh -Command "..."`: the quoted text is a command, not text.
_SHELL_C = re.compile(
    r"\b(?:(?:ba|z|da)?sh|pwsh|powershell)(?:\.exe)?(?:[ \t]+-\w+)*?[ \t]+-(?:[a-z]*c|Command)[ \t]+"
    r"(?P<quote>[\"'])(?P<body>(?:\\.|(?!(?P=quote))[^\\])*)(?P=quote)",
    re.IGNORECASE,
)
#: A here-document (`<<EOF`, `<<-'EOF'`): its head line is kept, its body is text, not commands.
_HEREDOC = re.compile(
    r"(?P<head><<-?[ \t]*(?P<quote>['\"]?)(?P<tag>\w+)(?P=quote)[^\n]*)\n.*?^[ \t]*(?P=tag)[ \t]*$",
    re.DOTALL | re.MULTILINE,
)
#: A PowerShell here-string (`@'` ... `'@`).
_HERESTRING = re.compile(r"@(?P<quote>['\"])[ \t]*\n.*?\n(?P=quote)@", re.DOTALL)
#: A line continuation (a backslash, or a backtick in PowerShell, at the end of a line).
_CONTINUATION = re.compile(r"(?:\\|`)[ \t]*\n")
#: A quoted string ('...' or "..."), which is text, not a command.
_QUOTED = re.compile(r"'[^']*'|\"(?:[^\"\\]|\\.)*\"")
#: Git Bash's form of a Windows drive (`/c/...`).
_MSYS_DRIVE = re.compile(r"^/(?P<drive>[A-Za-z])(?:/|$)")
#: A value the shell fills in (`$TITLE`, `$(...)`, PowerShell's `(...)`), which the hook cannot read.
EXPANSION = "$(...)"
#: A GitHub remote address, in the `https://host/owner/name(.git)` or `git@host:owner/name(.git)` form.
_REMOTE = re.compile(r"[/:](?P<repo>[\w.-]+/[\w.-]+?)(?:\.git)?/?$")
#: Options whose value is the next argument (or follows `=`), by every spelling gh accepts.
_VALUE_OPTIONS = {
    "-t": "title",
    "--title": "title",
    "-b": "body",
    "--body": "body",
    "-F": "body_file",
    "--body-file": "body_file",
    "-R": "repo",
    "--repo": "repo",
}
#: Options that fill the description from somewhere the check cannot see.
_UNCHECKED = ("-f", "--fill", "--fill-first", "--fill-verbose", "-w", "--web")
#: How long one check may take (a first `uv run` may sync the environment).
CHECK_TIMEOUT = 90
HOW = (
    'Open a pull request with `gh pr create --title "<title>" --body-file <file>` from a description that passes '
    "`uv run --project python coney-tools repo check-title` and `repo check-body --title ... --body <file>` (or "
    "`repo check-review`), the checks CI's required `title` and `body` jobs run "
    "(docs/guides/how-we-work.md#release-queue)."
)


@dataclass
class PrCall:
    """What one `gh pr create` or `gh pr edit` command asks for."""

    verb: str
    title: str | None = None
    body: str | None = None
    body_file: str | None = None
    repo: str | None = None
    attach: bool = False
    dry_run: bool = False
    #: The directory a leading `cd` moved to; None when it cannot be told (a `cd` to an expansion).
    cwd: str | None = None
    unchecked: list[str] = field(default_factory=list)


def _shell_body(match: re.Match[str]) -> str:
    """`bash -c "<body>"` as `bash -c (<body>)`, so the body's commands start like any other."""
    body = match.group("body")
    if match.group("quote") == '"':
        body = re.sub(r"\\(.)", r"\1", body)
    return match.group(0)[: match.start("quote") - match.start()] + "(" + body + ")"


def _clean(command: str) -> str:
    """The command without here-document and here-string bodies or line continuations, with the commands inside
    `bash -c "..."` unwrapped: only what runs."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    text = _CONTINUATION.sub(" ", text)
    return _SHELL_C.sub(_shell_body, text)


def _mask(text: str) -> str:
    """The text with the inside of each quoted string replaced by `x`, keeping its quotes and length, so a command
    is only found outside quotes and the offsets still point into `text`."""
    return _QUOTED.sub(lambda match: match.group(0)[0] + "x" * (len(match.group(0)) - 2) + match.group(0)[-1], text)


#: A PowerShell quoted string, with its escaped quotes (`''` in '...'; `` `" `` or `""` in "...").
_PS_QUOTED = re.compile(r"'(?:[^']|'')*'|\"(?:[^\"`]|`.|\"\")*\"")
#: Stand-ins for PowerShell's escaped quotes while the text is split, which would otherwise end the string.
_SINGLE, _DOUBLE = "\x01", "\x02"


def _escape_quotes(text: str) -> str:
    """PowerShell text with each escaped quote inside a string replaced by a stand-in."""

    def inner(match: re.Match[str]) -> str:
        found = match.group(0)
        body = found[1:-1].replace("''", _SINGLE) if found[0] == "'" else found[1:-1].replace('`"', _DOUBLE)
        return found[0] + body.replace('""', _DOUBLE) + found[-1]

    return _PS_QUOTED.sub(inner, text)


def _unquote(word: str, powershell: bool) -> str:
    """A PowerShell word without its quotes and with its escaped quotes back; others unchanged."""
    if powershell and len(word) >= 2 and word[0] == word[-1] and word[0] in "'\"":
        word = word[1:-1]
    return word.replace(_SINGLE, "'").replace(_DOUBLE, '"')


def _split(rest: str, powershell: bool) -> list[str] | None:
    """The arguments of one command, up to the first `;`, `&`, `|` or unmatched `)` outside quotes; None when it
    cannot be split. A `$(...)` or PowerShell `(...)` becomes one argument, `EXPANSION`.

    PowerShell keeps backslashes (Windows paths) and has no backslash escapes, so its text is split without them.
    """
    line = rest.split("\n", 1)[0]
    lexer = shlex.shlex(_escape_quotes(line) if powershell else line, posix=not powershell, punctuation_chars=";&|()")
    lexer.whitespace_split = True
    words: list[str] = []
    depth = 0
    try:
        for word in lexer:
            if word and all(char in ";&|()" for char in word):
                if depth == 0 and not word.startswith("("):
                    break
                if depth == 0:
                    if words and words[-1] == "$":
                        words.pop()
                    words.append(EXPANSION)
                depth = max(depth + word.count("(") - word.count(")"), 0)
                continue
            if depth == 0:
                words.append(_unquote(word, powershell))
    except ValueError:
        return None
    return words


def parse(verb: str, words: list[str]) -> PrCall:
    """The options of a `gh pr <verb>` command from its arguments (after the verb)."""
    call = PrCall(verb)
    index = 0
    while index < len(words):
        word = words[index]
        name, equals, inline = word.partition("=")
        if word == "--attach" or name == "--attach":
            call.attach = True
        elif word == "--dry-run":
            call.dry_run = True
        elif word in _UNCHECKED:
            call.unchecked.append(word)
        elif name in _VALUE_OPTIONS:
            if equals:
                value: str | None = inline
            else:
                index += 1
                value = words[index] if index < len(words) else None
            setattr(call, _VALUE_OPTIONS[name], value)
        index += 1
    return call


def native(path: str) -> Path:
    """A path as this system reads it: `~` expanded and, on Windows, Git Bash's `/c/...` as `C:/...`."""
    drive = _MSYS_DRIVE.match(path) if os.name == "nt" else None
    if drive is not None:
        path = drive.group("drive") + ":/" + path[drive.end() :]
    return Path(path).expanduser()


def _directory(text: str, bare: str, end: int, cwd: str) -> str | None:
    """The directory a command at `end` runs in: `cwd`, moved by each `cd` before it; None when a `cd` goes to an
    expansion (`cd "$DIR"`), which the hook cannot follow."""
    here: Path | None = Path(cwd or ".")
    for match in _CD.finditer(bare, 0, end):
        try:
            target = shlex.split(text[match.start("dir") : match.end("dir")], posix=True)
        except ValueError:
            target = []
        if here is None or not target or "$" in target[0] or "%" in target[0]:
            here = None
            continue
        step = native(target[0])
        here = step if step.is_absolute() else here / step
    return None if here is None else str(here)


def pr_calls(command: str, powershell: bool = False, cwd: str = "") -> list[PrCall] | None:
    """Every `gh pr create` and `gh pr edit` the command starts; None when one of them cannot be read."""
    text = _clean(command)
    bare = _mask(text)
    calls: list[PrCall] = []
    for match in _PR_COMMAND.finditer(bare):
        words = _split(text[match.start("globals") : match.end("globals")] + " " + text[match.end() :], powershell)
        if words is None:
            return None
        call = parse(match.group("verb"), words)
        call.cwd = _directory(text, bare, match.start(), cwd)
        calls.append(call)
    return calls


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


def refusal(call: PrCall) -> str | None:
    """Why the command's shape is refused before any check runs, or None."""
    if call.dry_run:
        return None
    if call.unchecked:
        return f"`gh pr {call.verb} {call.unchecked[0]}` takes the description from somewhere the checks cannot see."
    if call.body is not None:
        return f"`gh pr {call.verb} --body` passes the description inline; write it to a file and use --body-file."
    if call.body_file == "-":
        return f"`gh pr {call.verb} --body-file -` reads the description from stdin; give a file."
    if call.verb == "create" and call.body_file is None:
        return "`gh pr create` has no --body-file."
    if call.verb == "create" and not call.title:
        return "`gh pr create` has no --title."
    return None


#: Runs one `coney-tools` command (its arguments after `coney-tools`) with `stdin`; returns the exit code and output.
Runner = Callable[[list[str], str], tuple[int, str]]


def run_coney_tools(args: list[str], stdin: str) -> tuple[int, str]:
    """Run `coney-tools <args>` from the main checkout; (2, why) when it cannot be started."""
    root = Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path(__file__).resolve().parents[2])
    command = ["uv", "run", "--quiet", "--project", str(root / "python"), "coney-tools", *args]
    try:
        done = subprocess.run(
            command, cwd=root, input=stdin, capture_output=True, text=True, timeout=CHECK_TIMEOUT, check=False
        )
    except (OSError, subprocess.SubprocessError) as error:
        return 2, str(error)
    return done.returncode, (done.stdout + done.stderr).strip()


def _refused(code: int, out: str) -> bool:
    """Whether a check's result is a refusal: exit code 1 with the check's findings, not a crash's traceback."""
    return code == 1 and "Traceback (most recent call last)" not in out


def problems(call: PrCall, runner: Runner) -> list[str]:
    """What CI's `title` and `body` jobs would refuse in the call's title and description file (empty: it passes).

    A check that cannot run (exit code 2: no `coney-tools`, rules unusable; or a crash), a title or file that is a
    shell expansion, a file in a directory the hook cannot follow, or a file that is not there passes: the hook fails
    open, and gh reports a missing file itself.
    """
    found: list[str] = []
    title = call.title if call.title is not None and "$" not in call.title else None
    if title is not None:
        code, out = runner(["repo", "check-title", "-"], title + "\n")
        if _refused(code, out):
            found.append(out)
    if call.body_file is None or "$" in call.body_file or "%" in call.body_file:
        return found
    path = native(call.body_file)
    if not path.is_absolute():
        if call.cwd is None:
            return found
        path = Path(call.cwd) / path
    if path.is_file():
        args = ["repo", "check-body", "--body", str(path)]
        if title is not None:
            args += ["--title", title]
        if call.attach:
            args.append("--local-images")
        code, out = runner(args, "")
        if _refused(code, out):
            found.append(out)
    return found


def check(command: str, cwd: str, powershell: bool = False, runner: Runner = run_coney_tools) -> str | None:
    """The reason the command is refused, or None when it may run."""
    calls = pr_calls(command, powershell, cwd)
    if not calls:
        return None
    reasons: list[str] = []
    for call in calls:
        if call.repo is not None:
            here = this_repository(call.cwd or cwd)
            if here is not None and call.repo.lower() != here:
                continue  # another repository's pull request
        shape = refusal(call)
        if shape is not None:
            reasons.append(shape)
            continue
        if not call.dry_run:
            reasons.extend(problems(call, runner))
    if not reasons:
        return None
    return "Blocked: " + " ".join(reasons) + "\n" + HOW + "\n"


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
        command = data["tool_input"]["command"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    if not isinstance(command, str) or "gh" not in command:
        return 0
    cwd = data.get("cwd") if isinstance(data.get("cwd"), str) else str(Path.cwd())
    reason = check(command, cwd, data.get("tool_name") == "PowerShell")
    if reason is None:
        return 0
    sys.stderr.write(reason)
    return 2


if __name__ == "__main__":
    sys.exit(main())
