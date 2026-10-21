# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: a subagent never pushes and never writes to GitHub; the coordinator does.

Agents work local-first: an agent commits on its branch, writes the pull request's description to its scratch folder
and tells the coordinator, who pushes and opens the pull request when CI has room (AGENTS.md;
docs/guides/how-we-work.md#release-queue). An agent reads issues and pull requests and writes nothing to GitHub: no
push, pull request, comment, label or review (docs/guides/issues.md#agents). A push from an agent spends CI the queue
was keeping for something else, and a comment would go out under the maintainer's account.

Claude Code gives a hook an `agent_id` only when the call comes from a subagent (docs/guides/agents.md#trailer-guard),
so the main session, the coordinator, is never refused. Inside a subagent this hook, registered in
`.claude/settings.json` for the Bash and PowerShell tools, refuses a command that starts

* `git push` (also after `-C <dir>`, `-c <key=value>` and other options of git itself);
* a `gh` command that is not one of the reads in `READS` (`gh pr view`, `gh issue list`, `gh run view`, ...);
* `gh api` with a method other than GET, with fields or `--input` (gh then sends a POST), or a GraphQL mutation.

`coney-tools issue create` and the other `coney-tools` commands run `gh` as their own subprocess, which is not a tool
call, so an agent whose brief says to file an issue still can. A command counts only where it starts (the line start,
or after `;`, `&`, `|`, `(` or `$(`), so text that mentions one passes, also inside a here-document or here-string.
Exit code 2 blocks the call and shows stderr to the agent; every other outcome (also input it cannot read) exits 0.
Standard library only.
"""

import json
import re
import sys

#: Where a command starts: the line start or after `;`, `&`, `|`, `(` or `$(`, then optional `NAME=value` assignments.
_START = r"(?:^|[;&|(]|\$\()[ \t]*(?:[A-Za-z_]\w*=\S*[ \t]+)*"
#: One argument, quoted or not.
_ARG = r"(?:\"[^\"\n]*\"|'[^'\n]*'|[^\s;&|()]+)"
#: `git push`, with git's own options (`-C dir`, `-c key=value`, `--no-pager`, `--git-dir=...`) in between.
_GIT_PUSH = re.compile(
    _START + r"git(?:\.exe)?(?:[ \t]+(?:-[Cc][ \t]+" + _ARG + r"|--?[\w-]+(?:=" + _ARG + r")?))*[ \t]+push\b",
    re.MULTILINE,
)
#: A `gh` command: its group (`pr`), its verb (`view`) when there is one, and the rest of its line.
_GH = re.compile(
    _START + r"gh(?:\.exe)?[ \t]+(?P<group>[\w-]+)(?:[ \t]+(?P<verb>[a-z][\w-]*))?(?P<args>[^\n;&|]*)",
    re.MULTILINE,
)
#: The `gh` commands an agent may run: they read GitHub or change only the local checkout. A group mapped to None
#: is read-only as a whole.
READS: dict[str, frozenset[str] | None] = {
    "pr": frozenset({"view", "list", "diff", "checks", "status", "checkout"}),
    "issue": frozenset({"view", "list", "status"}),
    "run": frozenset({"view", "list", "watch", "download"}),
    "repo": frozenset({"view", "clone"}),
    "release": frozenset({"view", "list", "download"}),
    "workflow": frozenset({"view", "list"}),
    "label": frozenset({"list"}),
    "cache": frozenset({"list"}),
    "gist": frozenset({"view", "list"}),
    "ruleset": frozenset({"list", "view", "check"}),
    "auth": frozenset({"status"}),
    "config": frozenset({"get", "list"}),
    "search": None,
    "status": None,
    "help": None,
    "version": None,
}
#: Options of `gh` itself that only print.
_PRINTS = {"--version", "--help", "-h"}
_METHOD = re.compile(r"(?:^|[ \t])(?:-X|--method)(?:[ \t]+|=)[\"']?(?P<method>\w+)", re.IGNORECASE)
_FIELDS = re.compile(r"(?:^|[ \t])(?:-f|-F|--field|--raw-field|--input)(?=[ \t=])")
_MUTATION = re.compile(r"\bmutation\b")
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
_QUOTED = re.compile(r"'[^'\n]*'|\"(?:[^\"\\\n]|\\.)*\"")

MESSAGE = (
    "Blocked: {what}. A subagent never pushes and never writes to GitHub (no pull request, comment, label, review, "
    "merge or rerun): commit on your branch, write the pull request's title and description to your scratch folder, "
    "and tell the coordinator, who pushes from the release queue (docs/guides/how-we-work.md#release-queue, "
    "docs/guides/issues.md#agents). Reading stays allowed: gh pr view/list/diff/checks, gh issue view/list, gh run "
    "view/list, gh api GET. An issue your brief says to file goes through `coney-tools issue create`.\n"
)


def _clean(command: str) -> str:
    """The command without here-document and here-string bodies or line continuations: only what runs."""
    text = command.replace("\r\n", "\n")
    text = _HEREDOC.sub(lambda match: match.group("head"), text)
    text = _HERESTRING.sub(" ", text)
    return _CONTINUATION.sub(" ", text)


def _api_writes(args: str) -> bool:
    """Whether the arguments of a `gh api` call write: a method other than GET, fields or input without an explicit
    GET (gh then sends a POST), or a GraphQL mutation."""
    method = _METHOD.search(args)
    if method is not None:
        return method.group("method").upper() != "GET"
    if re.search(r"(?:^|[ \t])graphql(?=[ \t]|$)", args):
        return _MUTATION.search(args) is not None
    return _FIELDS.search(args) is not None


def refused(command: str) -> str | None:
    """Why the command is refused inside a subagent, or None when it only reads."""
    text = _clean(command)
    # Commands start outside quotes: mask the inside of each quoted string (keeping its quotes and length, so it
    # stays one argument and the offsets still point into `text`) before finding where a command starts.
    bare = _QUOTED.sub(lambda match: match.group(0)[0] + "x" * (len(match.group(0)) - 2) + match.group(0)[-1], text)
    if _GIT_PUSH.search(bare):
        return MESSAGE.format(what="`git push`")
    for call in _GH.finditer(bare):
        group, verb = call.group("group"), call.group("verb") or ""
        if group in _PRINTS or group.startswith("-"):
            continue
        if group == "api":
            # The arguments as written (the verb slot may hold the first one), with their quoted text.
            args = text[call.end("group") : call.end("args")]
            if _api_writes(args):
                return MESSAGE.format(what="a `gh api` call that writes")
            continue
        if group in READS:
            allowed = READS[group]
            if allowed is None or verb in allowed:
                continue
        return MESSAGE.format(what=f"`{f'gh {group} {verb}'.strip()}`")
    return None


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
        command = data["tool_input"]["command"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    if not data.get("agent_id") or not isinstance(command, str):
        return 0  # the main session (the coordinator) pushes and opens pull requests
    reason = refused(command)
    if reason is None:
        return 0
    sys.stderr.write(reason)
    return 2


if __name__ == "__main__":
    sys.exit(main())
