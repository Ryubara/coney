# SPDX-License-Identifier: GPL-3.0-or-later
"""The role rules of the Claude Code hooks: what each Coney role may do, and what a session with no role may do.

Claude Code puts the agent's type in every hook input as `agent_type`: a subagent's type, and for the main session the
agent it was started as (`claude --agent coordinator`). The rules live in `hooks.toml`
(docs/guides/enforcement.md#hooks); this module reads them and judges one tool call:

* **require_role**: when on, a call whose type is no Coney role (and no allowed helper) may use only the read-only
  tools in `readonly_tools`, and the refusal tells the person to start with `coney-agent`. It is on in the
  committed file.
* **a role's refusals**: tools by name prefix, `coney-tools` commands, shell commands by pattern, folders Edit and
  Write may not change, the only files a role may write, a `git commit` under a unit worktree, and the GitHub writes
  `push_guard.py` refuses (also for a main session, which has no agent id), and `allow_agent_types`, the only
  `subagent_type`s the role may start with the Agent tool.

Like every guard it judges spelling, never a security boundary: a command can be hidden from it. A setting of the
wrong shape is ignored, and an invalid pattern is skipped, so one bad entry never disables the rest. Standard library
only; `check` returns the reason to refuse a call, or None.
"""

from __future__ import annotations

import fnmatch
import re
from pathlib import Path
from typing import Any

import hooklib

#: The tools that change a file, and the input fields that name it.
WRITE_TOOLS = {"Edit": "file_path", "Write": "file_path", "MultiEdit": "file_path", "NotebookEdit": "notebook_path"}
SHELLS = ("Bash", "PowerShell")
#: The tools that start an agent.
AGENT_TOOLS = ("Agent", "Task")


def role_of(payload: dict[str, Any]) -> str:
    """The agent type of the call, stripped; empty when Claude Code gave none (a plain session)."""
    value = payload.get("agent_type")
    return value.strip() if isinstance(value, str) else ""


def role_rules(config: dict[str, Any], agent_type: str) -> dict[str, Any] | None:
    """The `[roles.*]` table that applies to an agent type (the role itself or one of its aliases); None for none."""
    roles = config.get("roles")
    if not isinstance(roles, dict):
        return None
    for name, rules in roles.items():
        if isinstance(rules, dict) and (agent_type == name or agent_type in hooklib.strings(rules.get("aliases"))):
            return rules
    return None


def has_role(config: dict[str, Any], agent_type: str) -> bool:
    """Whether the type is a Coney role or an allowed helper."""
    return bool(agent_type) and (
        role_rules(config, agent_type) is not None or agent_type in hooklib.strings(config.get("helpers"))
    )


def _coney_tools(words: str) -> re.Pattern[str]:
    """A `coney-tools <words>` command, with options between (`uv run --project python coney-tools orig study`)."""
    spelled = r"[ \t]+".join(re.escape(word) for word in words.split())
    return re.compile(r"\bconey-tools(?:\.exe)?[ \t]+(?:-\S+[ \t]+)*" + spelled + r"\b")


def _search(pattern: str, text: str) -> bool:
    """Whether the regular expression finds something in the text; an invalid pattern finds nothing."""
    try:
        return re.search(pattern, text) is not None
    except re.error:
        hooklib.record(f"hooks.toml: invalid pattern {pattern!r}")
        return False


def _command(payload: dict[str, Any]) -> str:
    """The shell command of a call, or an empty string."""
    tool_input = payload.get("tool_input")
    command = tool_input.get("command") if isinstance(tool_input, dict) else None
    return command if isinstance(command, str) else ""


def _command_text(payload: dict[str, Any]) -> str:
    """The shell command of a call with quoted text blanked, or an empty string."""
    command = _command(payload)
    return hooklib.mask_quotes(hooklib.clean_command(command)) if command else ""


def _refuse(role: str, what: str, rules: dict[str, Any]) -> str:
    """The refusal text: what the role may not do, and what to do instead."""
    hint = rules.get("hint", "")
    return f"Blocked: as {role} you may not {what}. {hint} (docs/guides/agents.md#roles)\n"


def _unit_commit(payload: dict[str, Any], under: list[str]) -> bool:
    """Whether the command commits in a directory under one of the given folders."""
    cwd = payload.get("cwd")
    folders = hooklib.commit_folders(_command(payload), cwd if isinstance(cwd, str) else "")
    return any(part.casefold() in folder.replace("\\", "/").casefold() + "/" for folder in folders for part in under)


def _write_refusal(role: str, rules: dict[str, Any], payload: dict[str, Any]) -> str | None:
    """The refusal of an Edit or Write by this role, or None."""
    tool_input = payload.get("tool_input")
    field = WRITE_TOOLS[str(payload.get("tool_name"))]
    path = tool_input.get(field) if isinstance(tool_input, dict) else None
    if not isinstance(path, str) or not path:
        return None
    only = hooklib.strings(rules.get("write_only"))
    # An entry is a file name or a pattern for one (`CODE-REVIEW*.md`), matched against the last part of the path in any
    # letter case (Windows ignores it).
    name = Path(path).name.casefold()
    if only and not any(fnmatch.fnmatchcase(name, entry.casefold()) for entry in only):
        return _refuse(role, f"write {Path(path).name}: you write only {', '.join(only)}", rules)
    relative = hooklib.relative_path(path)
    for folder in hooklib.strings(rules.get("deny_write_dirs")):
        if relative is not None and (relative == folder.casefold() or relative.startswith(folder.casefold() + "/")):
            return _refuse(role, f"change files under {folder}/", rules)
    return None


def _agent_refusal(role: str, rules: dict[str, Any], payload: dict[str, Any]) -> str | None:
    """The refusal of an Agent call whose type is not one the role may start (`allow_agent_types`), or None.

    A role without the key may start any; an empty list means none. A call with no type runs as the generic agent, so
    it is refused too."""
    if not isinstance(rules.get("allow_agent_types"), list):
        return None
    allowed = hooklib.strings(rules.get("allow_agent_types"))
    tool_input = payload.get("tool_input")
    value = tool_input.get("subagent_type") if isinstance(tool_input, dict) else None
    requested = value.strip() if isinstance(value, str) else ""
    if requested.casefold() in [name.casefold() for name in allowed]:
        return None
    shown = f"a {requested} agent" if requested else "an agent with no type"
    only = f"you may start only {', '.join(allowed)}" if allowed else "you may start no agent"
    return _refuse(role, f"start {shown}: {only}", rules)


def _shell_refusal(role: str, rules: dict[str, Any], payload: dict[str, Any]) -> str | None:
    """The refusal of a shell command by this role, or None."""
    text = _command_text(payload)
    if not text:
        return None
    for words in hooklib.strings(rules.get("deny_coney_tools")):
        if _coney_tools(words).search(text):
            return _refuse(role, f"run `coney-tools {words}`", rules)
    for pattern in hooklib.strings(rules.get("deny_commands")):
        if _search(pattern, text):
            return _refuse(role, "run that command", rules)
    under = hooklib.strings(rules.get("deny_commit_under"))
    if under and _unit_commit(payload, under):
        return _refuse(role, "commit in a unit's worktree", rules)
    if rules.get("no_github_writes") is True and not payload.get("agent_id"):
        # A subagent is already held to this by push_guard.py; a main session started as this role has no agent id.
        reason = hooklib.load_guard("push_guard").refused(_command(payload))
        if isinstance(reason, str) and reason:
            return reason
    return None


def role_refusal(config: dict[str, Any], payload: dict[str, Any]) -> str | None:
    """The reason to refuse the call under the rules of its role, or None."""
    role = role_of(payload)
    rules = role_rules(config, role)
    if rules is None:
        return None
    tool = str(payload.get("tool_name", ""))
    for prefix in hooklib.strings(rules.get("deny_tool_prefixes")):
        if tool.startswith(prefix):
            return _refuse(role, f"use {tool}", rules)
    if tool in AGENT_TOOLS:
        return _agent_refusal(role, rules, payload)
    if tool in WRITE_TOOLS:
        return _write_refusal(role, rules, payload)
    if tool in SHELLS:
        return _shell_refusal(role, rules, payload)
    return None


def missing_role_refusal(config: dict[str, Any], payload: dict[str, Any]) -> str | None:
    """The refusal of a call from a session with no role while `require_role` is on, or None."""
    if config.get("require_role") is not True:
        return None
    if has_role(config, role_of(payload)):
        return None
    allowed = hooklib.strings(config.get("readonly_tools")) or ["Read", "Grep", "Glob"]
    if payload.get("tool_name") in allowed:
        return None
    return (
        "Blocked: this session has no Coney role, so it may only read "
        f"({', '.join(allowed)}). Start Claude Code with the session launcher, `coney-agent` (or "
        "`uv run --project python coney-agent`), which starts it as the coordinator "
        "(docs/guides/agents.md#session-launcher).\n"
    )


def check(config: dict[str, Any], payload: dict[str, Any]) -> str | None:
    """The reason to refuse a tool call by the role rules, or None; the missing role is judged first.

    What no session may touch is `protect.py`'s, which the dispatcher runs on its own."""
    return missing_role_refusal(config, payload) or role_refusal(config, payload)


def session_notice(config: dict[str, Any], payload: dict[str, Any]) -> str | None:
    """What SessionStart tells the person when the session has no role; None when it has one."""
    if has_role(config, role_of(payload)):
        return None
    if config.get("require_role") is True:
        return (
            "Coney: this session has no role, so it is read-only (Read, Grep, Glob). Quit and start it with "
            "`coney-agent` (or `uv run --project python coney-agent`)."
        )
    return (
        "Coney: this session was not started through `coney-agent`, so it has no role and the role rules do not "
        "apply. Start with `coney-agent` (or `uv run --project python coney-agent`); sessions without a role will "
        "be read-only."
    )
