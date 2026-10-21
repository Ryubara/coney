# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: open or edit a pull request only with a title and description that pass CI's checks.

The coordinator opens each pull request from an agent's description file with `gh pr create --title ... --body-file
<file>` (docs/guides/how-we-work.md#release-queue). The rule is that the file passed `coney-tools repo check-review`
first, which runs the same title and body checks as CI's required `title` and `body` jobs; but nothing ran it, and on
2026-10-20 and 21 four pull requests were pushed whose descriptions CI's `body` job then refused (a missing
`## Summary` or `## Screenshots`), each costing a CI round and an edit. This hook, registered in
`.claude/settings.json` for the Bash and PowerShell tools, runs those two checks before the command does, from the main
checkout's `coney-tools`:

* `gh pr create` must give `--title` and `--body-file <file>` (not `--body`, `--fill`, `--web` or a body on stdin,
  which the check cannot see), and both must pass `repo check-title` and `repo check-body --title`; a local image is
  accepted when the command has `--attach`, which uploads it;
* `gh pr edit` with `--title` or `--body-file` runs the same check on what it changes, and refuses `--body`.

A command that names only another repository (`-R owner/name`, `--repo`) passes. A command counts only where `gh`
starts one (the line start, or after `;`, `&`, `|`, `(` or `$(`), so text that mentions one passes, also inside a
here-document or here-string. It fails open when it cannot tell: an argument list it cannot split, a file it cannot
find (gh then fails by itself), `coney-tools` not starting or its rules unusable. Exit code 2 blocks the call and
shows stderr to the agent. Standard library only; the checks themselves run in a subprocess.
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

#: Where a command starts: the line start or after `;`, `&`, `|`, `(` or `$(`, then optional `NAME=value` assignments.
_START = r"(?:^|[;&|(]|\$\()[ \t]*(?:[A-Za-z_]\w*=\S*[ \t]+)*"
#: `gh pr create` or `gh pr edit` where a command starts; the rest of the text follows.
_PR_COMMAND = re.compile(_START + r"gh(?:\.exe)?[ \t]+pr[ \t]+(?P<verb>create|edit)\b", re.MULTILINE)
#: A here-document (`<<EOF`, `<<-'EOF'`): its head line is kept, its body is text, not commands.
_HEREDOC = re.compile(
    r"(?P<head><<-?[ \t]*(?P<quote>['\"]?)(?P<tag>\w+)(?P=quote)[^\n]*)\n.*?^[ \t]*(?P=tag)[ \t]*$",
    re.DOTALL | re.MULTILINE,
)
#: A PowerShell here-string (`@'` ... `'@`).
_HERESTRING = re.compile(r"@(?P<quote>['\"])[ \t]*\n.*?\n(?P=quote)@", re.DOTALL)
#: A line continuation (a backslash, or a backtick in PowerShell, at the end of a line).
_CONTINUATION = re.compile(r"(?:\\|`)[ \t]*\n")
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
    unchecked: list[str] = field(default_factory=list)


def _clean(command: str) -> str:
    """The command without here-document and here-string bodies or line continuations: only what runs."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    return _CONTINUATION.sub(" ", text)


def _split(rest: str, powershell: bool) -> list[str] | None:
    """The arguments of one command, up to the first `;`, `&`, `|` or `)` outside quotes; None when it cannot be split.

    PowerShell keeps backslashes (Windows paths) and has no backslash escapes, so its text is split without them.
    """
    lexer = shlex.shlex(rest.split("\n", 1)[0], posix=not powershell, punctuation_chars=";&|()")
    lexer.whitespace_split = True
    words: list[str] = []
    try:
        for word in lexer:
            if word and all(char in ";&|()" for char in word):
                break
            if powershell and len(word) >= 2 and word[0] == word[-1] and word[0] in "'\"":
                word = word[1:-1]
            words.append(word)
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


def pr_calls(command: str, powershell: bool = False) -> list[PrCall] | None:
    """Every `gh pr create` and `gh pr edit` the command starts; None when one of them cannot be read."""
    text = _clean(command)
    calls: list[PrCall] = []
    for match in _PR_COMMAND.finditer(text):
        words = _split(text[match.end() :], powershell)
        if words is None:
            return None
        calls.append(parse(match.group("verb"), words))
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


def problems(call: PrCall, cwd: str, runner: Runner) -> list[str]:
    """What CI's `title` and `body` jobs would refuse in the call's title and description file (empty: it passes).

    A check that cannot run (exit code 2: no `coney-tools`, rules unusable) or a file that is not there passes: the
    hook fails open, and gh reports a missing file itself.
    """
    found: list[str] = []
    if call.title is not None:
        code, out = runner(["repo", "check-title", "-"], call.title + "\n")
        if code == 1:
            found.append(out)
    if call.body_file is not None:
        path = Path(call.body_file)
        if not path.is_absolute():
            path = Path(cwd or ".") / path
        if path.is_file():
            args = ["repo", "check-body", "--body", str(path)]
            if call.title is not None:
                args += ["--title", call.title]
            if call.attach:
                args.append("--local-images")
            code, out = runner(args, "")
            if code == 1:
                found.append(out)
    return found


def check(command: str, cwd: str, powershell: bool = False, runner: Runner = run_coney_tools) -> str | None:
    """The reason the command is refused, or None when it may run."""
    calls = pr_calls(command, powershell)
    if not calls:
        return None
    reasons: list[str] = []
    for call in calls:
        if call.repo is not None:
            here = this_repository(cwd)
            if here is not None and call.repo.lower() != here:
                continue  # another repository's pull request
        shape = refusal(call)
        if shape is not None:
            reasons.append(shape)
            continue
        reasons.extend(problems(call, cwd, runner))
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
