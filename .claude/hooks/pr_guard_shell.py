# SPDX-License-Identifier: GPL-3.0-or-later
"""Reading a shell command for `pr_guard.py`: which `gh pr` calls it starts, and the helpers `pr_guard_routes.py` reads
`git push` and `gh api` with.

Standard library only, as the hooks run without packages. A command counts only where a program starts (the line start;
after `;`, `&`, `|`, `(`, `{`, `$(`, a backtick, a PowerShell assignment or the `&` call operator; after `then`, `do`,
`env`, `command`, `exec` and the like; inside `bash -c "..."` or `pwsh -Command "..."`), also inside a command
substitution in double quotes; quoted text, here-documents and here-strings are text. What the reading does not see is
listed in docs/guides/enforcement.md#pr-guard.
"""

import os
import re
import shlex
from dataclasses import dataclass, field
from pathlib import Path

#: Words that may stand before a program: shell keywords, `env`, `command`, `exec` and the like with their options,
#: `NAME=value` assignments and PowerShell's `&` call operator.
_PREFIX = (
    r"(?:(?:then|do|else|elif|time|!|env|command|exec|nohup|sudo|xargs)(?:[ \t]+-[^\s;&|()]+)*[ \t]+"
    r"|[A-Za-z_]\w*=[^\s;&|()]*[ \t]+|&[ \t]*)*"
)
#: Where a command starts: the line start, after `;`, `&`, `|`, `(`, `)`, `{`, `}`, `$(` or a backtick, or after a
#: PowerShell assignment (`$url =`); then the prefix words.
_START = r"(?:^|[;&|(){}`]|\$\(|\$[\w:]+[ \t]*=)[ \t]*" + _PREFIX
#: One argument, quoted or not.
_ARG = r"(?:\"[^\"\n]*\"|'[^'\n]*'|[^\s;&|()]+)"
#: The `gh` program, also by a path (`/usr/bin/gh`, `C:\tools\gh.exe`).
_GH = r"(?:[^\s;&|()'\"]*[/\\])?gh(?:\.exe)?"
#: `gh pr create` (also its alias `new`), `edit`, `merge` or `update-branch` where a command starts, with gh's own
#: options (`-R owner/name`) before `pr`.
_PR_COMMAND = re.compile(
    _START
    + _GH
    + r"(?P<globals>(?:[ \t]+(?:(?:-R|--repo|--hostname)(?:[ \t]+|=)"
    + _ARG
    + r"|--?[\w-]+(?:="
    + _ARG
    + r")?))*)[ \t]+pr[ \t]+(?P<verb>create|new|edit|merge|update-branch)(?![\w-])",
    re.MULTILINE,
)
#: A change of directory where a command starts: `cd`, `pushd`, `chdir`, `Set-Location` or `Push-Location`.
_CD = re.compile(
    _START + r"(?P<verb>cd|pushd|chdir|Set-Location|Push-Location|sl)(?:[ \t]+-\w+)*[ \t]+(?P<dir>" + _ARG + ")",
    re.MULTILINE | re.IGNORECASE,
)
#: A return to the folder before the last `pushd`: `popd` or `Pop-Location`.
_POPD = re.compile(_START + r"(?:popd|Pop-Location)(?![\w-])", re.MULTILINE | re.IGNORECASE)
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
#: Git Bash's form of a Windows drive (`/c/...`).
_MSYS_DRIVE = re.compile(r"^/(?P<drive>[A-Za-z])(?:/|$)")
#: A Windows drive path (`C:/x`), absolute on any system although `Path` on Linux and macOS reads it as relative.
#: A copy of `hooklib._DRIVE`: this guard stands alone and must work when `hooklib` does not import.
_DRIVE_PATH = re.compile(r"^[A-Za-z]:[\\/]")
#: A value the shell fills in (`$TITLE`, `$(...)`, PowerShell's `(...)`), which the hook cannot read.
EXPANSION = "$(...)"
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


@dataclass
class PrCall:
    """What one `gh pr create`, `edit`, `merge` or `update-branch` command asks for. `verb` is `create` also for
    `new`."""

    verb: str
    title: str | None = None
    body: str | None = None
    body_file: str | None = None
    repo: str | None = None
    attach: bool = False
    dry_run: bool = False
    #: `--auto` of `gh pr merge`.
    auto: bool = False
    #: `--help` or `-h`: the command prints its usage and does nothing else.
    help: bool = False
    #: The options that write the title or description, as spelled (`-F`, `--title`, `--attach`).
    text_options: list[str] = field(default_factory=list)
    #: The directory a leading `cd` moved to; None when it cannot be told (a `cd` to an expansion).
    cwd: str | None = None
    unchecked: list[str] = field(default_factory=list)
    #: The arguments could not be split (`$'...'` quoting, a quote with no end): nothing above them is known.
    unreadable: bool = False
    #: The raw text of the arguments, kept for an unreadable call (whether it may name `-R`/`--repo`).
    raw: str = ""


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


def _mask(text: str, powershell: bool = False) -> str:
    """The text with the inside of each quoted string replaced by `x`, keeping its quotes and length, so a command
    is only found outside quotes and the offsets still point into `text`.

    A command substitution runs inside double quotes (`"$(gh pr create ...)"`, bash's backticks too; PowerShell's
    `"$(...)"`), so those stay visible, and a quote inside one starts a string of its own. A single-quoted string is
    text always. A quote with no end is not a string (the shell would not run the line), so nothing after it is hidden.
    """
    chars = list(text)
    size = len(text)

    def blank(start: int, stop: int) -> None:
        for index in range(start, stop):
            chars[index] = "x"

    def code(start: int, closer: str) -> int:
        """Scan commands from `start` to the `closer` ( `)` or a backtick) that ends them; the index of it, or the
        end of the text."""
        depth = 0
        index = start
        while index < size:
            char = text[index]
            if char == "\\" and not powershell:
                index += 2
            elif char == "'":
                end = text.find("'", index + 1)
                if end < 0:
                    index += 1
                    continue
                blank(index + 1, end)
                index = end + 1
            elif char == '"':
                index = double(index)
            elif closer == ")" and char == "(":
                depth += 1
                index += 1
            elif closer == ")" and char == ")" and depth > 0:
                depth -= 1
                index += 1
            elif char == closer:
                return index
            else:
                index += 1
        return size

    def double(start: int) -> int:
        """Mask the double-quoted string that opens at `start`; the index after it (`start + 1` when it has no end)."""
        saved = chars[start:]
        index = start + 1
        while index < size:
            char = text[index]
            if char == ("`" if powershell else "\\"):  # the shell's escape character
                blank(index, min(index + 2, size))
                index += 2
            elif char == '"':
                return index + 1
            elif char == "$" and text[index + 1 : index + 2] == "(":
                index = code(index + 2, ")") + 1
            elif char == "`":
                index = code(index + 1, "`") + 1
            else:
                blank(index, index + 1)
                index += 1
        chars[start:] = saved  # no closing quote: undo, and hide nothing
        return start + 1

    code(0, "\0")
    return "".join(chars)


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


def _line_end(rest: str, powershell: bool) -> int:
    """Where the command that starts `rest` ends at a line break: the first newline outside quotes and parentheses,
    or the start of a `#` comment (which runs to the line's end); the text's length when there is none. A quoted
    argument may span lines (`--body "one<newline>two"`, `--body "$(cat <<'EOF' ...)"`)."""
    escape = "`" if powershell else "\\"
    quote = ""
    depth = 0
    index = 0
    while index < len(rest):
        char = rest[index]
        if char == escape and quote != "'":
            index += 2
            continue
        if quote:
            quote = "" if char == quote else quote
        elif char in "'\"":
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth = max(depth - 1, 0)
        elif depth == 0 and (char == "\n" or (char == "#" and (index == 0 or rest[index - 1] in " \t"))):
            return index
        index += 1
    return len(rest)


def _split(rest: str, powershell: bool) -> list[str] | None:
    """The arguments of one command, up to the first `;`, `&`, `|`, unmatched `)` or line break outside quotes; None
    when it cannot be split. A `$(...)` or PowerShell `(...)` becomes one argument, `EXPANSION`.

    PowerShell keeps backslashes (Windows paths) and has no backslash escapes, so its text is split without them.
    """
    line = rest[: _line_end(rest, powershell)]
    lexer = shlex.shlex(_escape_quotes(line) if powershell else line, posix=not powershell, punctuation_chars=";&|()")
    lexer.whitespace_split = True
    lexer.commenters = ""  # `_line_end` already cut a real comment; a `#` inside a word (`tools:#x`) is text
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


#: The short options whose value may be attached to them (`-tfoo`, `-FPR.md`, `-Rowner/repo`).
_ATTACHED = "tbFR"
#: What a `--auto=...` value says yes with.
_YES = frozenset({"", "true", "1", "t", "T", "TRUE", "True"})


def parse(verb: str, words: list[str]) -> PrCall:
    """The options of a `gh pr <verb>` command from its arguments (after the verb); `new` is read as `create`."""
    call = PrCall("create" if verb == "new" else verb)
    index = 0
    while index < len(words):
        word = words[index]
        if len(word) > 2 and word[0] == "-" and word[1] in _ATTACHED:
            # `-tfoo` is `-t foo`, and pflag reads `-t=foo` as `-t foo` too
            name, equals, inline = word[:2], "=", word[3:] if word[2] == "=" else word[2:]
        else:
            name, equals, inline = word.partition("=")
        if name == "--attach":
            call.attach = True
            call.text_options.append("--attach")
        elif name == "--dry-run":
            call.dry_run = inline in _YES  # `--dry-run=false` is no dry run; the last one wins
        elif name in ("--help", "-h"):
            call.help = inline in _YES  # pflag reads `-h=false` too
        elif name == "--auto":
            call.auto = inline in _YES
        elif name in _UNCHECKED:
            call.unchecked.append(name)
        elif name in _VALUE_OPTIONS:
            if _VALUE_OPTIONS[name] in ("title", "body", "body_file"):
                call.text_options.append(name)
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


def is_absolute(path: Path) -> bool:
    """Whether the path is absolute here or names a Windows drive (a command can carry one on any system)."""
    return path.is_absolute() or _DRIVE_PATH.match(str(path)) is not None


def _move(here: str, argument: str) -> str | None:
    """The folder `cd <argument>` (or `git -C <argument>`) leads to from `here`; None when it cannot be told (an
    expansion in the argument)."""
    try:
        target = shlex.split(argument, posix=True)
    except ValueError:
        return None
    if not target or "$" in target[0] or "%" in target[0] or target[0] == "-":  # `cd -`: the folder before
        return None
    step = native(target[0])
    return str(step) if is_absolute(step) else str(Path(here) / step)


def _closed_before(bare: str, start: int, end: int) -> bool:
    """Whether a parenthesis open at `start` closes before `end`: a subshell (`( ... )`, `$( ... )`, the body of
    `bash -c "..."`, which `_clean` puts in parentheses) that has ended, and its `cd` with it."""
    depth = 0
    for char in bare[start : end + 1]:  # a command's match starts at the `)` or `;` before it
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth < 0:
                return True
    return False


def _directory(text: str, bare: str, end: int, cwd: str, powershell: bool = False) -> str | None:
    """The directory a command at `end` runs in: `cwd`, moved by each `cd` before it, back by each `popd`; None
    when it cannot be told: a `cd` to an expansion (`cd "$DIR"`), a `popd` past the first folder, or (PowerShell, where
    parentheses are no subshell) a `cd` in parentheses that have closed.

    In bash a `cd` inside a subshell that ended before the command does not move it."""
    here: str | None = str(Path(cwd or "."))
    stack: list[str | None] = []
    moves = sorted([*_CD.finditer(bare, 0, end), *_POPD.finditer(bare, 0, end)], key=lambda found: found.start())
    for match in moves:
        if _closed_before(bare, match.end(), end):
            if powershell:
                return None
            continue
        if "dir" not in match.groupdict():  # popd, Pop-Location
            here = stack.pop() if stack else None
        else:
            if match.group("verb").lower() in ("pushd", "push-location"):
                stack.append(here)
            here = None if here is None else _move(here, text[match.start("dir") : match.end("dir")])
    return here


def pr_calls(command: str, powershell: bool = False, cwd: str = "") -> list[PrCall]:
    """Every `gh pr create`, `edit`, `merge` and `update-branch` the command starts; one whose arguments cannot be
    split is marked `unreadable`."""
    text = _clean(command)
    bare = _mask(text, powershell)
    calls: list[PrCall] = []
    ticks = [] if powershell else _backticks(bare)
    for match in _PR_COMMAND.finditer(bare):
        rest = text[match.end() :]
        before = sum(1 for tick in ticks if tick < match.end())
        if before % 2 == 1:  # inside backticks, which end the command
            after = [tick for tick in ticks if tick >= match.end()]
            if after:
                rest = rest[: after[0] - match.end()]
        arguments = text[match.start("globals") : match.end("globals")] + " " + rest
        words = _split(arguments, powershell)
        if words is None:
            call = PrCall("create" if match.group("verb") == "new" else match.group("verb"), unreadable=True)
            call.raw = arguments.split("\n", 1)[0]
        else:
            call = parse(match.group("verb"), words)
        call.cwd = _directory(text, bare, match.start(), cwd, powershell)
        calls.append(call)
    return calls


def _backticks(bare: str) -> list[int]:
    """The offsets of the backticks in masked text that open or close a substitution: not escaped by a backslash."""
    found: list[int] = []
    index = 0
    while index < len(bare):
        if bare[index] == "\\":
            index += 2
            continue
        if bare[index] == "`":
            found.append(index)
        index += 1
    return found
