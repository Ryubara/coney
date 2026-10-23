# SPDX-License-Identifier: GPL-3.0-or-later
"""Who runs a command, vouched for by the hook: the identity token of the Claude Code hooks.

The tools that stamp work or check a commit need to know which agent ran them, and an agent's own word cannot be
trusted. The hook knows (the hook input names the agent and its type, and its transcript names its model), so before
a `coney-tools` or `git commit` command runs, `updated_input` writes that identity to
`~/.coney/identity/<hash>.json`, where the hash is the SHA-256 of a random 128-bit token, and returns the command with
only `export CONEY_AGENT_TOKEN=<token>;` in front. `coney_tools.identity` hashes the token and reads the file back. A
token that is typed, changed or expired finds no file, so the tool records the identity as unverified: never as
another role. A listing of the folder shows hashes, which give no token away.

What an agent could still do: write such a file itself, for which it must name `~/.coney`, which `protect.py` forbids
every session (visible and deliberate; the agent and the tool are the same user, so not impossible); or read the token
of a command that is running, from its command line in a process listing (`ps`, Task Manager), for as long as that
command runs and the file lives. The listing readers that give command lines away are refused by spelling too, but
`ps` is not.

The file is read, not consumed, because one command can run several tools (`coney-tools ... && git commit`). It is
deleted when the command ends (`release`, from PostToolUse, PostToolUseFailure and PermissionDenied), except for a
background command, whose tool may not have started reading it yet; a call that issues a token (`issue`) removes the
expired files (`sweep`), also leftover temporary ones. The folder is 0700 and a file 0600. A file records the session,
agent and tool call it was written for, so a stray one is easy to trace. A tool that starts after the file's life
(`[identity] ttl_seconds`, 10 minutes), for instance a later `coney-tools` in a long chain, finds none and is
unverified.

Claude Code lets a `PreToolUse` hook change a call only by returning a new input (`updatedInput`), not the
environment, so the token goes in front of the command: `export ...;` for Bash, the one form that keeps the permission
rules working (the rewritten command is checked part by part, and `export CONEY_AGENT_TOKEN=*` is allowed in
`.claude/settings.json`); `$env:` for PowerShell, which refuses it in every permission mode but `bypassPermissions`.

`assignment_refusal` also refuses a command that sets `CONEY_AGENT_*`, as a courtesy: it reads spelling and shell can
spell it around (`export "CONEY_AGENT_TYPE=x"`), which is why the protection is the token, not this. Standard library
only.
"""

from __future__ import annotations

import contextlib
import hashlib
import json
import os
import re
import secrets
import time
from pathlib import Path
from typing import Any

import hooklib

PREFIX_TOOLS = ("Bash", "PowerShell")
#: The variable the token travels in, and the file format (both read by `coney_tools.identity`).
TOKEN_VARIABLE = "CONEY_AGENT_TOKEN"
VERSION = 1
#: A temporary file older than this (seconds) is a leftover.
_STALE_TEMPORARY = 120
#: A command that the injection is for: `coney-tools` anywhere, or a `git ... commit`.
_WANTED = re.compile(r"\bconey-tools\b")
#: Ways of setting or removing a `CONEY_AGENT_*` variable: an assignment where a command starts (`NAME=x cmd`,
#: `export NAME=x`, `$env:NAME = x`, `os.environ["NAME"] = x`) but not text that merely contains one (`sed
#: s/CONEY_AGENT_ID=a/b/ file`), a command that takes the name (`export`, `unset`, `declare`, `env`, `setx`,
#: `Set-Item`, `Remove-Item`, `SetEnvironmentVariable`, `putenv`, `environ.update(`), whatever the quoting. Reading one
#: (`$NAME`, `os.environ.get("NAME")`, `printenv NAME`) matches none of them.
_ASSIGNS = re.compile(
    r"(?<![/\\\w.-])CONEY_AGENT_\w+[\"'\]}]*[ \t]*[+\-*/]?=(?!=)"
    r"|(?<![$:\w-])(?:export|unset|declare|typeset|readonly|local|env)(?![\w:-])[^\n;&|]*CONEY_AGENT_"
    r"|\benviron\.(?:update|pop|setdefault)\([^)]*CONEY_AGENT_",
    re.IGNORECASE,
)
#: Commands whose name is unmistakable, so they count even with the variable's name inside quotes.
_SETTERS = re.compile(
    r"\b(?:SetEnvironmentVariable|Set-Item|New-Item|Remove-Item|Set-Variable|putenv|unsetenv|setx)\b"
    r"[^\n;&|]*CONEY_AGENT_",
    re.IGNORECASE,
)
#: What comes before a quoted script: `eval`, PowerShell's `-Command`, or `-c` of a shell or interpreter (not grep's).
_SCRIPT_OPTION = re.compile(
    r"(?:(?:^|[\s;&|(])(?:eval|[^\s;&|()]*[/\\]?(?:pwsh|powershell)(?:\.exe)?[^;&|()]*?-(?:Command|EncodedCommand))"
    r"|(?:^|[\s;&|(])(?:\S*[/\\])?(?:bash|sh|zsh|dash|python3?|py|node|ruby|perl)(?:\.exe)?(?:[ \t]+-\S+)*[ \t]+-c)$",
    re.IGNORECASE,
)
_SAFE_VALUE = re.compile(r"[^A-Za-z0-9._-]")
_BRACKETS = re.compile(r"\[[^\]]*\]|@.*$")
UNKNOWN = "unknown"

REFUSAL = (
    "Blocked: do not set CONEY_AGENT_* variables. The hook sets CONEY_AGENT_TOKEN for you on every `coney-tools` "
    "and `git commit` command, and the tools take who you are from the file it names; a value you type is worth "
    "nothing and is recorded as unverified (docs/guides/agents.md#identity).\n"
)


def default_folder() -> Path:
    """Where identity files live: `~/.coney/identity` (the same place `coney_tools.identity` reads)."""
    return Path.home() / ".coney" / "identity"


def file_name(token: str) -> str:
    """The name of a token's identity file: the SHA-256 of the token, so the folder does not list tokens."""
    return hashlib.sha256(token.encode("ascii")).hexdigest() + ".json"


def assignment_refusal(command: str) -> str | None:
    """The refusal for a command that sets or unsets a `CONEY_AGENT_*` variable, or None.

    A courtesy, not the protection. Here-document bodies and the text inside quotes are not read, except after `-c`,
    `-Command` and `eval`, whose quoted text is a script.
    """
    text = hooklib.clean_command(command)
    if "CONEY_AGENT_" not in text:
        return None
    if _SETTERS.search(text) or _ASSIGNS.search(_mask_non_scripts(text)):
        return REFUSAL
    return None


def _mask_non_scripts(text: str) -> str:
    """The text with quoted strings blanked unless a script option (`-c`, `-Command`, `eval`) leads to them."""
    out: list[str] = []
    last = 0
    for match in hooklib.QUOTED.finditer(text):
        before = text[: match.start()].rstrip()
        runs_script = _SCRIPT_OPTION.search(before)
        out.append(text[last : match.start()])
        out.append(match.group(0) if runs_script else hooklib.mask_quotes(match.group(0)))
        last = match.end()
    out.append(text[last:])
    return "".join(out)


def safe(value: str) -> str:
    """A value made of letters, digits, dot, underscore and dash only; a context suffix like `[1m]` is dropped."""
    return _SAFE_VALUE.sub("", _BRACKETS.sub("", value))


def agent_model(payload: dict[str, Any]) -> str:
    """The model id of the agent's latest turn, from its transcript; `unknown` when it cannot be found."""
    trailer = hooklib.load_guard("trailer_guard")
    transcript = trailer.transcript_for(payload)
    model = trailer.latest_model(transcript) if isinstance(transcript, Path) else None
    return safe(model) or UNKNOWN if model else UNKNOWN


def values(payload: dict[str, Any]) -> dict[str, str]:
    """The identity of the caller: the subagent's id (`main` in the main session), its type (`none` for a session with
    no role) and its model."""
    agent_id = payload.get("agent_id")
    agent_type = payload.get("agent_type")
    return {
        "id": safe(agent_id) if isinstance(agent_id, str) and safe(agent_id) else "main",
        "type": safe(agent_type) if isinstance(agent_type, str) and safe(agent_type) else "none",
        "model": agent_model(payload),
    }


def wanted(payload: dict[str, Any], config: dict[str, Any]) -> bool:
    """Whether this call gets an identity: injection on, a shell command that runs coney-tools or commits, and for
    PowerShell a permission mode that lets the tool set variables."""
    settings = config.get("identity", {})
    if not isinstance(settings, dict) or settings.get("enabled") is not True:
        return False
    tool = payload.get("tool_name")
    tool_input = payload.get("tool_input")
    command = tool_input.get("command") if isinstance(tool_input, dict) else None
    if tool not in PREFIX_TOOLS or not isinstance(command, str):
        return False
    if tool == "PowerShell" and payload.get("permission_mode") not in hooklib.strings(settings.get("powershell_modes")):
        return False
    return _WANTED.search(hooklib.mask_quotes(hooklib.clean_command(command))) is not None or hooklib.is_git_commit(
        command
    )


def ttl(config: dict[str, Any]) -> float:
    """How long an identity file lives, in seconds (`[identity] ttl_seconds`, default 600)."""
    settings = config.get("identity", {})
    value = settings.get("ttl_seconds") if isinstance(settings, dict) else None
    return float(value) if isinstance(value, int | float) and not isinstance(value, bool) and value > 0 else 600.0


def sweep(folder: Path, now: float | None = None) -> None:
    """Delete the identity files that have expired (or cannot be read as ours) and the leftover temporary ones."""
    moment = time.time() if now is None else now
    try:
        files = list(folder.glob("*.json")) + list(folder.glob("*.tmp"))
    except OSError:
        return
    for path in files:
        try:
            if path.suffix == ".tmp":
                if path.stat().st_mtime < moment - _STALE_TEMPORARY:
                    path.unlink(missing_ok=True)
                continue
            expires = json.loads(path.read_text(encoding="utf-8")).get("expires")
            if not isinstance(expires, int | float) or expires <= moment:
                path.unlink(missing_ok=True)
        except (OSError, ValueError, AttributeError):
            with contextlib.suppress(OSError):
                path.unlink(missing_ok=True)


def issue(payload: dict[str, Any], config: dict[str, Any], folder: Path | None = None, now: float | None = None) -> str:
    """Write the caller's identity file (folder 0700, file 0600) and return its token (128 random bits as 32 hex)."""
    where = default_folder() if folder is None else folder
    moment = time.time() if now is None else now
    where.mkdir(parents=True, exist_ok=True, mode=0o700)
    with contextlib.suppress(OSError):
        where.chmod(0o700)
    sweep(where, moment)
    token = secrets.token_hex(16)
    data = {
        "version": VERSION,
        **values(payload),
        "session": str(payload.get("session_id", "")),
        "agent": str(payload.get("agent_id", "")),
        "tool_use_id": str(payload.get("tool_use_id", "")),
        "issued": moment,
        "expires": moment + ttl(config),
    }
    final = where / file_name(token)
    temporary = final.with_suffix(".tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        handle.write(json.dumps(data))
    temporary.replace(final)
    return token


def release(payload: dict[str, Any], folder: Path | None = None) -> None:
    """Delete the identity files written for this tool call, once it has ended (or failed, or been denied).

    Not for a background command (`run_in_background`): PostToolUse comes as soon as it is started, before uv has
    finished starting the tool that reads the file; those files wait for the sweep.
    """
    tool_input = payload.get("tool_input")
    if isinstance(tool_input, dict) and tool_input.get("run_in_background"):
        return
    wanted_id = payload.get("tool_use_id")
    if not isinstance(wanted_id, str) or not wanted_id:
        return
    where = default_folder() if folder is None else folder
    try:
        files = list(where.glob("*.json"))
    except OSError:
        return
    for path in files:
        try:
            if json.loads(path.read_text(encoding="utf-8")).get("tool_use_id") == wanted_id:
                path.unlink(missing_ok=True)
        except (OSError, ValueError, AttributeError):
            continue


def prefix(payload: dict[str, Any], token: str) -> str:
    """The shell text that puts the token in the environment, to go before the command."""
    if payload.get("tool_name") == "PowerShell":
        return f"$env:{TOKEN_VARIABLE} = '{token}'; "
    return f"export {TOKEN_VARIABLE}={token}; "


def updated_input(
    payload: dict[str, Any], config: dict[str, Any], folder: Path | None = None, now: float | None = None
) -> dict[str, Any] | None:
    """The tool input with the token in front of its command (the identity file written), or None when the call does
    not get one."""
    if not wanted(payload, config):
        return None
    token = issue(payload, config, folder, now)
    tool_input = dict(payload["tool_input"])
    tool_input["command"] = prefix(payload, token) + tool_input["command"]
    return tool_input
