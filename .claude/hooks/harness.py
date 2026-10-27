# SPDX-License-Identifier: GPL-3.0-or-later
"""The adapters of Coney's hooks for agent tools other than Claude Code: one per harness, so the guards never know
which tool called them.

Every harness that has project hooks runs `dispatch.py --harness <name>` before a tool call (the files are listed in
docs/guides/enforcement.md#harnesses). Each sends its own JSON on stdin and wants its own answer on stdout, so this
module does three things, and `dispatch.py` does the judging in between with the same rules and guards it runs for
Claude Code:

* **`detect`** names the harness of a payload that came without `--harness`: Cursor, GitHub Copilot and Grok Build
  also run the hooks of `.claude/settings.json`, whose command names none, so their payloads must not be read as
  Claude Code's (a Claude Code session with no role may only read; a Cursor session would be locked out).
* **`claude_calls`** turns one tool call of the harness into the Claude Code calls the guards read: its shell tool
  becomes `Bash` (`PowerShell` for Copilot's), a file write or edit becomes `Write` with `file_path` and `content`
  (Codex's `apply_patch` one per file it touches, a deletion with no content), a read becomes `Read`, and any other
  tool keeps its name. The working directory goes into `cwd`; the role into `agent_type` (`role`).
* **`answer`** gives the harness its own refusal: Codex the Claude Code JSON, Gemini CLI, Antigravity and Grok a
  `decision`, Cursor a `permission`, Copilot a `permissionDecision`. Always with exit code 0: Copilot denies a call
  whose hook exits with anything else, and every config ends its command with `|| exit 0` so a launcher that cannot
  start does not lock the tools (the same reason as for Claude Code, docs/guides/agents.md#hooks).

A session of another harness has no agent type, so its role comes from `CONEY_AGENT_ROLE` in the environment the
harness was started with (no agent's command can change it; `identity.py` refuses one that tries), else
`[harnesses] default_role` of `hooks.toml`, `contributor`. The identity token is Claude Code's only: the other
harnesses get no `updatedInput`, so their `coney-tools` commands are recorded as unverified. The project root is this
file's checkout (`hooklib.CHECKOUT`), never `$CLAUDE_PROJECT_DIR`. Standard library only.
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any

#: The harnesses with an adapter (`claude` is Claude Code's own, unchanged path).
HARNESSES = ("claude", "codex", "gemini", "antigravity", "cursor", "copilot", "grok")
#: The environment variable that names the role of a session of another harness.
ROLE_VARIABLE = "CONEY_AGENT_ROLE"
DEFAULT_ROLE = "contributor"

#: The event names that come before a tool runs, per harness (Copilot's camelCase payload and Antigravity's carry none).
PRE_EVENTS = {
    "codex": ("PreToolUse",),
    "gemini": ("BeforeTool",),
    "antigravity": ("PreToolUse",),
    "cursor": ("preToolUse", "beforeShellExecution"),
    "copilot": ("PreToolUse", "preToolUse"),
    "grok": ("PreToolUse", "preToolUse"),
}
#: Gemini CLI's own event names, which no other harness uses.
_GEMINI_EVENTS = (
    "BeforeTool",
    "AfterTool",
    "BeforeAgent",
    "AfterAgent",
    "BeforeModel",
    "AfterModel",
    "BeforeToolSelection",
    "PreCompress",
)

#: Tool names (lower case) that run a shell command, and the argument that holds it.
SHELL_TOOLS = (
    "bash",
    "shell",
    "run_shell_command",
    "run_command",
    "exec_command",
    "run_terminal_cmd",
    "terminal",
    "local_shell",
    "unified_exec",
)
POWERSHELL_TOOLS = ("powershell",)
COMMAND_KEYS = ("command", "CommandLine", "commandLine", "cmd")
#: Tool names (lower case) that write, edit or delete a file.
WRITE_TOOLS = (
    "write",
    "edit",
    "multiedit",
    "notebookedit",
    "create",
    "write_file",
    "replace",
    "write_to_file",
    "replace_file_content",
    "multi_replace_file_content",
    "edit_file",
    "str_replace",
    "str_replace_editor",
    "search_replace",
    "delete",
    "delete_file",
    "apply_patch",
)
#: Tool names (lower case) that read a file.
READ_TOOLS = ("read", "view", "view_file", "read_file", "read_many_files")
#: The arguments that name a file, and those that hold the text written into it.
PATH_KEYS = ("file_path", "path", "target_file", "TargetFile", "filePath", "notebook_path", "AbsolutePath")
TEXT_KEYS = (
    "content",
    "contents",
    "file_text",
    "new_string",
    "new_str",
    "new_source",
    "CodeContent",
    "ReplacementContent",
    "code_edit",
    "text",
)
#: The arguments that name the folder a command runs in.
CWD_KEYS = ("cwd", "Cwd", "working_directory", "dir_path", "workdir")

#: Codex's patch format: one header line per file, `+` lines are the text it adds.
_PATCH_FILE = re.compile(r"^\*\*\* (?P<what>Add|Update|Delete) File: (?P<path>.+?)\s*$")
_PATCH_MOVE = re.compile(r"^\*\*\* Move to: (?P<path>.+?)\s*$")


def detect(payload: dict[str, Any]) -> str:
    """The harness a payload came from, by fields only that harness sends; `claude` when none matches."""
    if "cursor_version" in payload:
        return "cursor"
    if "toolCall" in payload:
        return "antigravity"
    if "hookEventName" in payload or "toolInput" in payload:
        return "grok"
    if "toolName" in payload or "toolArgs" in payload:
        return "copilot"
    if "turn_id" in payload:
        return "codex"
    if payload.get("hook_event_name") in _GEMINI_EVENTS:
        return "gemini"
    # Copilot's Claude-compatible form (a PascalCase event) has a timestamp; Claude Code always sends its transcript.
    if "timestamp" in payload and "transcript_path" not in payload:
        return "copilot"
    return "claude"


def _event(harness: str, payload: dict[str, Any]) -> str:
    """The event name the harness gave, or an empty string."""
    for key in ("hook_event_name", "hookEventName"):
        value = payload.get(key)
        if isinstance(value, str):
            return value
    return ""


def is_pre_tool(harness: str, payload: dict[str, Any]) -> bool:
    """Whether the payload is a tool call about to run (the only event the adapters judge)."""
    event = _event(harness, payload)
    if event:
        return event in PRE_EVENTS.get(harness, ())
    # No event name: Antigravity's and Copilot's camelCase payloads. A result means the tool has already run.
    results = ("toolResult", "tool_response", "result", "output")
    if harness == "antigravity":
        return isinstance(payload.get("toolCall"), dict) and not any(key in payload for key in results)
    if harness == "copilot":
        return "toolName" in payload and not any(key in payload for key in results)
    return False


def _tool(harness: str, payload: dict[str, Any]) -> tuple[str, dict[str, Any]]:
    """The tool name and its arguments, as the harness sent them."""
    if harness == "antigravity":
        call = payload.get("toolCall")
        call = call if isinstance(call, dict) else {}
        name, arguments = call.get("name"), call.get("args")
    elif harness == "cursor" and _event(harness, payload) == "beforeShellExecution":
        name, arguments = "Shell", {"command": payload.get("command"), "cwd": payload.get("cwd")}
    elif "toolName" in payload or "toolInput" in payload or "toolArgs" in payload:
        name = payload.get("toolName")
        arguments = payload.get("toolInput", payload.get("toolArgs"))
    else:
        name, arguments = payload.get("tool_name"), payload.get("tool_input")
    if isinstance(arguments, str):  # Copilot may send the arguments as JSON text
        try:
            arguments = json.loads(arguments)
        except ValueError:
            arguments = {"command": arguments}
    return (name if isinstance(name, str) else ""), (arguments if isinstance(arguments, dict) else {})


def _first(arguments: dict[str, Any], keys: tuple[str, ...]) -> str | None:
    """The first of the keys whose value is a non-empty string."""
    for key in keys:
        value = arguments.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def _text(arguments: dict[str, Any]) -> str:
    """Every text the call writes, joined: the text fields, and those of a list of edits or chunks."""
    parts = [value for key in TEXT_KEYS if isinstance(value := arguments.get(key), str)]
    for value in arguments.values():
        if isinstance(value, list):
            parts += [
                text
                for item in value
                if isinstance(item, dict)
                for key in TEXT_KEYS
                if isinstance(text := item.get(key), str)
            ]
    return "\n".join(parts)


def _join(cwd: str, path: str) -> str:
    """The path made absolute against the working directory (a relative path in a patch is relative to it)."""
    if not cwd or Path(path).is_absolute() or re.match(r"^[A-Za-z]:[\\/]", path):
        return path
    return str(Path(cwd) / path)


def patch_files(patch: str, cwd: str) -> list[dict[str, Any]]:
    """The files a Codex patch touches, as Write inputs: the path and the lines it adds (none for a deletion)."""
    files: list[dict[str, Any]] = []
    for line in patch.replace("\r\n", "\n").split("\n"):
        header = _PATCH_FILE.match(line)
        move = _PATCH_MOVE.match(line)
        if header:
            files.append({"file_path": _join(cwd, header.group("path")), "content": ""})
        elif move and files:
            files.append({"file_path": _join(cwd, move.group("path")), "content": files[-1]["content"]})
        elif files and line.startswith("+") and not line.startswith("+++"):
            files[-1]["content"] += line[1:] + "\n"
    return files


def _cwd(harness: str, payload: dict[str, Any], arguments: dict[str, Any]) -> str:
    """The folder the call runs in: the command's own, else the payload's, else the first workspace root."""
    found = _first(arguments, CWD_KEYS) or _first(payload, ("cwd",))
    if found:
        return found
    for key in ("workspace_roots", "workspacePaths"):
        roots = payload.get(key)
        if isinstance(roots, list) and roots and isinstance(roots[0], str):
            return roots[0]
    return _first(payload, ("workspaceRoot",)) or ""


def claude_calls(harness: str, payload: dict[str, Any], role: str) -> list[dict[str, Any]]:
    """The Claude Code PreToolUse payloads that stand for this tool call (empty when it is no call to judge)."""
    if not is_pre_tool(harness, payload):
        return []
    name, arguments = _tool(harness, payload)
    cwd = _cwd(harness, payload, arguments)
    base: dict[str, Any] = {"hook_event_name": "PreToolUse", "cwd": cwd, "agent_type": role, "harness": harness}
    key = name.casefold()
    command = _first(arguments, COMMAND_KEYS)
    if key == "apply_patch":
        return [{**base, "tool_name": "Write", "tool_input": file} for file in patch_files(command or "", cwd)]
    if key in SHELL_TOOLS or key in POWERSHELL_TOOLS:
        tool = "PowerShell" if key in POWERSHELL_TOOLS else "Bash"
        return [{**base, "tool_name": tool, "tool_input": {"command": command or ""}}]
    path = _first(arguments, PATH_KEYS)
    if key in WRITE_TOOLS and path:
        return [
            {**base, "tool_name": "Write", "tool_input": {"file_path": _join(cwd, path), "content": _text(arguments)}}
        ]
    if key in READ_TOOLS and path:
        return [{**base, "tool_name": "Read", "tool_input": {"file_path": _join(cwd, path)}}]
    return [{**base, "tool_name": name, "tool_input": arguments}]


def role(config: dict[str, Any], environment: dict[str, str] | None = None) -> str:
    """The role of a session of another harness: `CONEY_AGENT_ROLE` when set, else `[harnesses] default_role`."""
    given = (os.environ if environment is None else environment).get(ROLE_VARIABLE, "").strip()
    if given:
        return given
    settings = config.get("harnesses")
    value = settings.get("default_role") if isinstance(settings, dict) else None
    return value.strip() if isinstance(value, str) and value.strip() else DEFAULT_ROLE


def answer(harness: str, reason: str | None) -> tuple[int, str, str]:
    """What the hook tells the harness: its own refusal JSON with the reasons, or its "no objection"; exit code 0."""
    if reason is None:
        allowed = {"gemini": "{}", "cursor": "{}", "antigravity": json.dumps({"decision": "allow"})}
        return 0, allowed.get(harness, ""), ""
    if harness == "codex":
        decision: dict[str, Any] = {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "permissionDecisionReason": reason,
            }
        }
    elif harness == "cursor":
        decision = {"permission": "deny", "user_message": reason, "agent_message": reason}
    elif harness == "copilot":
        decision = {"permissionDecision": "deny", "permissionDecisionReason": reason}
    else:  # gemini, antigravity, grok
        decision = {"decision": "deny", "reason": reason}
    return 0, json.dumps(decision), reason
