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
  (Codex's `apply_patch` one per file it touches, a deletion with no content), a read becomes `Read`. A name in none
  of the lists is read by its arguments at any depth (`by_shape`; arguments that are no object kept under `input`):
  a command argument (a string, a list read as an argv and as several commands, a program beside its `args`) makes
  it a shell call, a patch anywhere one `Write` per file, a file path with text to write (or a name that says it
  changes a file) a `Write`; anything else (MCP tools, searches) keeps its name. A listed tool whose arguments the
  adapter cannot read is judged conservatively, never as nothing: a shell tool's every string inside its arguments as
  a command, a write tool's every path-like string as a `Write`. The working directory goes into `cwd`; the role into
  `agent_type` (`role`).
* **`answer`** gives the harness its refusal: Codex the Claude Code JSON, Gemini CLI and Antigravity a `decision`.
  Cursor, Copilot and Grok Build also run other tools' hook files (`.claude/settings.json`, `.cursor/hooks.json`) and
  may read the answer by that file's format, so they get every deny key at once (`UNION_HARNESSES`): a key the tool
  does not read is ignored, and a wrong guess of the format never becomes a silent allow. Always with exit code 0:
  Copilot denies a call whose hook exits with anything else, and every config ends its command with `; exit 0` so a
  launcher that cannot start does not lock the tools (the same reason as for Claude Code, docs/guides/agents.md#hooks).

A session of another harness has no agent type, so its role comes from `CONEY_AGENT_ROLE` in the environment the
harness was started with (no agent's command can change it; `identity.py` refuses one that tries), else
`[harnesses] default_role` of `hooks.toml`, `contributor`. The identity token is Claude Code's only: the other
harnesses get no `updatedInput`, so their `coney-tools` commands are recorded as unverified. The project root is this
file's checkout (`hooklib.CHECKOUT`), never `$CLAUDE_PROJECT_DIR`: `dispatch.other_harness` sets that variable to the
checkout while it judges, for the guards that read it. Standard library only.
"""

from __future__ import annotations

import json
import os
import re
import shlex
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
    # PascalCase too: Cursor also runs `.claude/settings.json`'s hook, and may send that file's event name.
    "cursor": ("preToolUse", "beforeShellExecution", "PreToolUse"),
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
#: The arguments that hold a shell command: a string, or a list (an argv, or several commands).
COMMAND_KEYS = (
    "command",
    "CommandLine",
    "commandLine",
    "Command",
    "cmd",
    "command_line",
    "cmd_line",
    "commands",
    "script",
    "input",
)
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
PATH_KEYS = (
    "file_path",
    "path",
    "target_file",
    "TargetFile",
    "filePath",
    "filepath",
    "file",
    "filename",
    "file_name",
    "uri",
    "notebook_path",
    "AbsolutePath",
    "target",
    "target_path",
    "destination",
    "destination_path",
    "dest",
    "new_path",
    "newPath",
)
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
    "code",
    "text",
    "new_text",
    "newText",
    "replacement",
    "diff",
    "patch",
    "edits",
    "data",
    "body",
)
#: Words in a tool name the adapter does not know that say the tool changes a file: such a call with a path and no text
#: (a deletion, a move) is judged as a `Write` with no content.
_WRITE_WORDS = re.compile(
    r"write|edit|delete|remove|move|rename|create|patch|replace|insert|append|save|update|modify|overwrite|change",
    re.IGNORECASE,
)
#: How deep the adapter reads into nested arguments.
_DEPTH = 8
#: The arguments of a program given apart from it (`{"command": "git", "args": ["push"]}`), and the program's names.
ARGS_KEYS = ("args", "arguments", "argv")
PROGRAM_KEYS = ("command", "program", "executable", "cmd")
#: The first line of a Codex patch, wherever it appears in an argument.
PATCH_START = "*** Begin Patch"
#: Arguments that hold text, not a path: the text an edit replaces, and words for the person.
_NOT_PATHS = ("old_string", "old_str", "old_text", "oldText", "description", "explanation", "instruction")
#: A string that may name a file: one line, at most 1024 characters (`_paths` then asks for a slash or an extension).
_PATHISH = re.compile(r"^[^\n\r]{1,1024}$")
#: The harnesses that get every deny key at once (see `answer`).
UNION_HARNESSES = ("cursor", "copilot", "grok")
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
    # Arguments that are no object (a list, a bare string, JSON text of either) are kept under `input`, a command
    # argument, so they are judged like any other instead of being read as none.
    if arguments is not None and not isinstance(arguments, dict):
        arguments = {"input": arguments}
    return (name if isinstance(name, str) else ""), (arguments if isinstance(arguments, dict) else {})


def _first(arguments: dict[str, Any], keys: tuple[str, ...]) -> str | None:
    """The first of the keys whose value is a non-empty string."""
    for key in keys:
        value = arguments.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def _text(arguments: dict[str, Any]) -> str:
    """Every text the call writes, joined: the strings under a text argument at any depth (a list of edits or of files
    included)."""
    return "\n".join(value for key, value in _nodes(arguments) if key in TEXT_KEYS and isinstance(value, str))


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


def _nodes(value: Any, key: str | None = None, depth: int = 0) -> list[tuple[str | None, Any]]:
    """Every value inside the arguments with the name it was given, at any depth up to `_DEPTH`: the value itself, the
    values of an object by their names, and the items of a list by the list's name."""
    found: list[tuple[str | None, Any]] = [(key, value)]
    if depth >= _DEPTH:
        return found
    if isinstance(value, dict):
        for name, item in value.items():
            found += _nodes(item, str(name), depth + 1)
    elif isinstance(value, list):
        for item in value:
            found += _nodes(item, key, depth + 1)
    return found


def _strings(value: Any) -> list[str]:
    """The readings of one value as command text: a string as it is; a list of strings and numbers both as one argv
    (`shlex.join`) and as several commands, one per line, since the adapter cannot tell which the tool meant."""
    if isinstance(value, str):
        return [value] if value else []
    if isinstance(value, list) and value and all(isinstance(item, (str, int, float)) for item in value):
        words = [str(item) for item in value]
        return list(dict.fromkeys([shlex.join(words), "\n".join(words)]))
    return []


def _split(value: Any) -> list[str]:
    """A program and its arguments given apart (`{"command": "git", "args": ["push"]}`), as one command line."""
    if not isinstance(value, dict):
        return []
    args = next((value[key] for key in ARGS_KEYS if isinstance(value.get(key), list)), None)
    program = next((value[key] for key in PROGRAM_KEYS if isinstance(value.get(key), str)), None)
    if program is None or args is None:
        return []
    return [shlex.join([program, *(str(item) for item in args if isinstance(item, (str, int, float)))])]


def _commands(arguments: dict[str, Any], keys: tuple[str, ...] | None = None) -> list[str]:
    """Every command text under the given argument names at any depth (every value when None), each reading once:
    everything inside a command argument (strings, lists, an object with a program and its arguments), and a program
    given apart from its arguments anywhere."""
    found: list[str] = []
    for key, value in _nodes(arguments):
        inside = _nodes(value) if keys is None or key in keys else []
        for _, item in inside:
            found += [text for text in _strings(item) if text not in found]
        found += [text for text in _split(value) if text not in found]
    return found


def _patches(arguments: dict[str, Any]) -> list[str]:
    """Every argument that holds a Codex patch, under any name and at any depth."""
    return [value for _, value in _nodes(arguments) if isinstance(value, str) and PATCH_START in value]


def _first_path(arguments: dict[str, Any]) -> str | None:
    """The first path argument at any depth (the top level first)."""
    top = _first(arguments, PATH_KEYS)
    if top is not None:
        return top
    return next(
        (value for key, value in _nodes(arguments) if key in PATH_KEYS and isinstance(value, str) and value), None
    )


def _as_path(value: str) -> str:
    """A path from a path argument: a `file://` URI made a plain path (`file:///C:/x` is `C:/x`)."""
    if value.startswith("file://"):
        value = value[len("file://") :]
        if re.match(r"^/[A-Za-z]:[\\/]", value):
            value = value[1:]
    return value


def _paths(arguments: dict[str, Any]) -> list[str]:
    """Every argument that may name a file, for a tool that changes files, at any depth: the path arguments first (also
    those of the objects in a list of files), then any other one-line string with a slash or an extension (a move's two
    paths, a list of paths to delete)."""
    listed: list[str] = []
    other: list[str] = []
    for key, value in _nodes(arguments):
        if key in TEXT_KEYS or key in CWD_KEYS or key in _NOT_PATHS:
            continue
        if not isinstance(value, str) or not _PATHISH.match(value) or PATCH_START in value:
            continue
        if key in PATH_KEYS:
            listed.append(_as_path(value))
        elif re.search(r"[\\/]|\.\w{1,8}$", value):
            other.append(_as_path(value))
    return list(dict.fromkeys(listed + other))


def _has_text(arguments: dict[str, Any]) -> bool:
    """Whether the call carries text to write, in any form and at any depth (a string, a list of edits)."""
    return any(key in TEXT_KEYS and value not in (None, "", [], {}) for key, value in _nodes(arguments))


def _shell(name: str, command: str) -> dict[str, Any]:
    """A shell call: `PowerShell` for a PowerShell tool, else `Bash`."""
    shell = "PowerShell" if re.search(r"powershell|pwsh", name, re.IGNORECASE) else "Bash"
    return {"tool_name": shell, "tool_input": {"command": command}}


def _writes(arguments: dict[str, Any], cwd: str, paths: list[str]) -> list[dict[str, Any]]:
    """One `Write` per path, each with the call's text."""
    text = _text(arguments)
    return [{"tool_name": "Write", "tool_input": {"file_path": _join(cwd, path), "content": text}} for path in paths]


def _patch_calls(arguments: dict[str, Any], cwd: str) -> list[dict[str, Any]]:
    """One `Write` per file of every patch the call carries."""
    return [
        {"tool_name": "Write", "tool_input": file} for patch in _patches(arguments) for file in patch_files(patch, cwd)
    ]


def by_shape(name: str, arguments: dict[str, Any], cwd: str) -> list[dict[str, Any]] | None:
    """The Claude Code calls an unlisted tool stands for, read by its arguments; None when they show neither a shell
    command nor a file write (the call then keeps its name).

    Every shape found is judged, so a call is refused when any reading of it is: a patch under any argument, a command
    under a command argument (a list both as an argv and as several commands), and a path with text to write, or a
    path under a name that says it changes a file (then every path-like argument, a move's target too)."""
    calls = _patch_calls(arguments, cwd)
    calls += [_shell(name, command) for command in _commands(arguments, COMMAND_KEYS) if PATCH_START not in command]
    path = _first_path(arguments)
    if _WRITE_WORDS.search(name):
        calls += _writes(arguments, cwd, _paths(arguments))
    elif path and _has_text(arguments):
        calls += _writes(arguments, cwd, [_as_path(path)])
    return calls or None


def claude_calls(harness: str, payload: dict[str, Any], role: str) -> list[dict[str, Any]]:
    """The Claude Code PreToolUse payloads that stand for this tool call (empty when it is no call to judge).

    A listed tool whose arguments are in no shape the adapter knows is judged conservatively, never let through as
    nothing: a shell tool with no command argument has every string and list of strings inside its arguments, at any
    depth, judged as a command, a patch or write tool every path-like string inside them as a `Write`."""
    if not is_pre_tool(harness, payload):
        return []
    name, arguments = _tool(harness, payload)
    cwd = _cwd(harness, payload, arguments)
    base: dict[str, Any] = {"hook_event_name": "PreToolUse", "cwd": cwd, "agent_type": role, "harness": harness}
    key = name.casefold()
    calls: list[dict[str, Any]] | None
    if key == "apply_patch":
        calls = _patch_calls(arguments, cwd) or _writes(arguments, cwd, _paths(arguments))
    elif key in SHELL_TOOLS or key in POWERSHELL_TOOLS:
        commands = _commands(arguments, COMMAND_KEYS) or _commands(arguments) or [""]
        calls = [_shell(name, command) for command in commands]
    elif key in WRITE_TOOLS:
        calls = _patch_calls(arguments, cwd) + _writes(arguments, cwd, _paths(arguments))
    elif key in READ_TOOLS and (path := _first_path(arguments)):
        calls = [{"tool_name": "Read", "tool_input": {"file_path": _join(cwd, _as_path(path))}}]
    elif key in READ_TOOLS:
        calls = None
    else:
        calls = by_shape(name, arguments, cwd)
    if not calls:
        return [{**base, "tool_name": name, "tool_input": arguments}]
    return [{**base, **call} for call in calls]


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
        # Cursor blocks on output that does not match its schema, whose one required key is `permission`.
        allowed = {
            "gemini": "{}",
            "cursor": json.dumps({"permission": "allow"}),
            "antigravity": json.dumps({"decision": "allow"}),
        }
        return 0, allowed.get(harness, ""), ""
    claude: dict[str, Any] = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }
    if harness == "codex":
        decision = claude
    elif harness in UNION_HARNESSES:
        # Cursor's own keys, Copilot's, Grok Build's and Claude Code's together (docs/guides/enforcement.md#harnesses).
        decision = {
            "permission": "deny",
            "user_message": reason,
            "agent_message": reason,
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
            "decision": "deny",
            "reason": reason,
            **claude,
        }
    else:  # gemini, antigravity
        decision = {"decision": "deny", "reason": reason}
    return 0, json.dumps(decision), reason
