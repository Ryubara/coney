# SPDX-License-Identifier: GPL-3.0-or-later
"""The one entry point of Coney's hooks: `.claude/settings.json` runs this file for every event, and the hook file of
every other agent tool that has project hooks runs it with `--harness <name>` before each tool call (Codex, Gemini CLI,
Antigravity, Cursor, GitHub Copilot, Grok Build; docs/guides/enforcement.md#harnesses). `harness.py` turns their calls
into Claude Code's and the answer back into theirs (`answer_any`, `other_harness`), so the guards below run unchanged
for all of them; what follows is Claude Code's path, which a payload of another tool never takes.

Claude Code starts one process per hook entry, and each entry here cost a Python start-up (a `uv run` each) on every
shell command. Now it starts this file once per event (PreToolUse for every tool, PostToolUse for the shell tools,
SessionStart), and the checks run inside it:

* **PreToolUse**, in this order: what no one may touch (`protect.py`); the role rules (`roles.py`: a session with no
  role while `require_role` is on, then the refusals of the caller's role); a command that sets `CONEY_AGENT_*`
  (`identity.py`);
  then the guards that were separate hooks, loaded as modules and run unchanged (`build_guard.py`, `issue_guard.py`,
  `comment_guard.py`, `bounded_waits.py`, `pr_guard.py`, `push_guard.py` for the shell tools; `pcsx2_guard.py` for the
  shell commands that name PCSX2 and for the PCSX2 MCP tools, `xemu_guard.py` the same for xemu; `agent_guard.py` and,
  last, `slot_guard.py` (the coordinator's engineer slots) for the Agent tool; `lua_guard.py` for Write, Edit, MultiEdit
  and NotebookEdit). Every refusal is collected and sent together, as the separate hooks' were; `slot_guard.py` runs only
  when nothing else
  refused, since it keeps a slot. When nothing refuses, a `coney-tools` or `git commit` command gets the caller's
  identity token put in front of it (`identity.py`, returned as `updatedInput`).
* **PostToolUse**: `trailer_guard.py` after a `git commit`; the identity file of the command is removed, also when
  the command failed (PostToolUseFailure) or was denied (PermissionDenied).
* **SessionStart**: a notice when the session has no role, or when `hooks.toml` could not be read.
* **Stop**: `slot_fill_guard.py` blocks the end of a coordinator turn while an engineer slot is free with work to
  start and no reason (docs/guides/slot-fill.md). It asks `coney-tools task slots --fill-check`; it passes the turn
  (with a note) when the tool cannot run, and the tool lets a session's turn end after a few blocks in a row.

**Fail-safe.** The guards are what keeps agents from breaking the shared machine, so nothing else may turn them off. The
rules (`protect.py`, `roles.py`, `identity.py`, `hooklib.py`) are imported inside `try`, `protect.py` apart from the
others and with its rules built in, not read from the settings; a settings file that cannot be read gives
the defaults (no role rules, `require_role` off); each of roles and identity runs in a `try` of its own; a guard that
crashes passes the call alone. In each case a line goes to `coney-hooks.log` in the temp folder, and the other guards
still run. This file needs nothing but the standard library to run the guards, and the command in `settings.json` pins
the interpreter (`--python ">=3.12"`) so an old default Python cannot fail an import.

A guard is run through `run_guard`, which gives its `main()` the hook input on a stand-in stdin and takes its exit code
and stderr, so a guard stays a script of its own that its tests load by path. Settings: `hooks.toml`
(docs/guides/enforcement.md#hooks).

A refusal is Claude Code's JSON answer with exit code 0, and `settings.json` ends the command with `|| exit 0`: a
launcher that fails (no Python 3.12 for uv, a checkout without this file) exits with 2, which would block every tool
call, so it must not reach Claude Code. `dispatch()` returns exit code 2 for "refused" inside the process; `answer()`
turns it into the JSON.
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import re
import sys
import tempfile
import time
from collections.abc import Callable
from pathlib import Path
from types import ModuleType
from typing import Any

HOOKS = Path(__file__).resolve().parent
sys.path.insert(0, str(HOOKS))

#: Why the rule modules could not be imported, else None. The guards do not need them, and `protect` does not need
#: `roles` or `identity`, so one broken module takes away only its own rules.
RULES_ERROR: str | None = None
PROTECT_ERROR: str | None = None
try:
    import hooklib
except Exception as _error:  # a broken helper must not turn the guards off
    RULES_ERROR = PROTECT_ERROR = f"{type(_error).__name__}: {_error}"
else:
    try:
        import protect
    except Exception as _error:
        PROTECT_ERROR = f"{type(_error).__name__}: {_error}"
    try:
        import identity
        import roles
    except Exception as _error:
        RULES_ERROR = f"{type(_error).__name__}: {_error}"
#: Why the adapters of the other harnesses could not be imported, else None (Claude Code's path does not need them).
HARNESS_ERROR: str | None = None
try:
    import harness
except Exception as _error:
    HARNESS_ERROR = f"{type(_error).__name__}: {_error}"

SHELLS = ("Bash", "PowerShell")
LOG_FILE = "coney-hooks.log"
#: The shell commands pcsx2_guard reads: `coney-tools ... pcsx2` (the old `if` filter `*coney-tools*pcsx2*`), and the
#: commands that drive PCSX2 from other groups, `trace original`, `trace events` (and its old name `trace mission`,
#: until the cutover) and `clips record`.
_PCSX2_COMMAND = re.compile(r"coney-tools.*(?:pcsx2|trace\s+(?:original|events|mission)|clips\s+record)", re.DOTALL)
#: The shell commands xemu_guard reads: `coney-tools ... xemu`.
_XEMU_COMMAND = re.compile(r"coney-tools.*xemu", re.DOTALL)
_modules: dict[str, ModuleType] = {}


def record(what: str) -> None:
    """Append a line to the hooks' log in the temp folder (this file's own copy: it must work when hooklib does not)."""
    try:
        with (Path(tempfile.gettempdir()) / LOG_FILE).open("a", encoding="utf-8") as log:
            log.write(f"{time.strftime('%Y-%m-%dT%H:%M:%S')} {what}\n")
    except OSError:
        return


def guard_module(name: str) -> ModuleType:
    """The hook script `<name>.py` next to this file as a module, loaded once (with the standard library only)."""
    if name not in _modules:
        spec = importlib.util.spec_from_file_location(f"coney_hook_{name}", HOOKS / f"{name}.py")
        if spec is None or spec.loader is None:
            raise ImportError(name)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        _modules[name] = module
    return _modules[name]


def load_settings() -> tuple[dict[str, Any], str | None]:
    """`hooks.toml` and the problem reading it, or about a rule module that did not import; `{}` when the file cannot
    be read (no setting is then on)."""
    if RULES_ERROR is not None:
        return {}, f"the rule modules did not import ({RULES_ERROR})"
    try:
        config, problem = hooklib.load_config_checked()
    except Exception as error:
        return {}, f"{type(error).__name__}: {error}"
    if PROTECT_ERROR is not None:
        problem = f"protect.py did not import ({PROTECT_ERROR})" + (f"; {problem}" if problem else "")
    return config, problem


def _command(payload: dict[str, Any]) -> str:
    """The shell command of a call, or an empty string."""
    tool_input = payload.get("tool_input")
    command = tool_input.get("command") if isinstance(tool_input, dict) else None
    return command if isinstance(command, str) else ""


def _shell(payload: dict[str, Any]) -> bool:
    """Whether the call runs a shell command."""
    return payload.get("tool_name") in SHELLS


def _pcsx2(payload: dict[str, Any]) -> bool:
    """Whether pcsx2_guard has to look at the call: a PCSX2 MCP tool, or a shell command that names the tool."""
    tool = str(payload.get("tool_name", ""))
    return tool.startswith("mcp__pcsx2__") or (_shell(payload) and _PCSX2_COMMAND.search(_command(payload)) is not None)


def _xemu(payload: dict[str, Any]) -> bool:
    """Whether xemu_guard has to look at the call: an xemu MCP tool, or a shell command that names the tool."""
    tool = str(payload.get("tool_name", ""))
    return tool.startswith("mcp__xemu__") or (_shell(payload) and _XEMU_COMMAND.search(_command(payload)) is not None)


def _agent(payload: dict[str, Any]) -> bool:
    """Whether the call starts an agent."""
    return payload.get("tool_name") in ("Agent", "Task")


#: The tools that write a file's text, which lua_guard reads.
WRITERS = ("Write", "Edit", "MultiEdit", "NotebookEdit")


def _writes(payload: dict[str, Any]) -> bool:
    """Whether the call writes text into a file."""
    return payload.get("tool_name") in WRITERS


#: What `git commit` looks like (`git commit`, `git -C x commit`, `git -c k=v commit`), without hooklib.
_COMMIT = re.compile(
    r"(?:^|[;&|(\n]|\$\()[ \t]*(?:[A-Za-z_]\w*=\S*[ \t]+)*(?:\S*[/\\])?git(?:\.exe)?"
    r"(?:[ \t]+(?:(?:-C|-c|--git-dir|--work-tree)[ \t]+\S+|-\S+))*[ \t]+commit(?![\w-])"
)


def _commit(payload: dict[str, Any]) -> bool:
    """Whether the call runs `git commit` (not any command with the word in it, such as `git config commit.gpgsign`)."""
    return _shell(payload) and _COMMIT.search(_command(payload)) is not None


#: The guards of PreToolUse, in the order they ran as separate hooks: (script, whether it looks at this call).
PRE_GUARDS: list[tuple[str, Callable[[dict[str, Any]], bool]]] = [
    ("build_guard", _shell),
    ("issue_guard", _shell),
    ("comment_guard", _shell),
    ("bounded_waits", _shell),
    ("pr_guard", _shell),
    ("push_guard", _shell),
    ("pcsx2_guard", _pcsx2),
    ("xemu_guard", _xemu),
    ("agent_guard", _agent),
    ("slot_guard", _agent),
    ("lua_guard", _writes),
]
#: Guards that run only when nothing else refused the call: they have an effect (slot_guard keeps a slot for the agent
#: it lets start), which a call that is refused anyway must not leave behind.
LAST_GUARDS = ("slot_guard",)
#: The guards of PostToolUse.
POST_GUARDS: list[tuple[str, Callable[[dict[str, Any]], bool]]] = [("trailer_guard", _commit)]
#: The guards of Stop: the end of a turn of the main session. They judge through `judge(payload)`, not `main()`.
STOP_GUARDS = ("slot_fill_guard",)


def run_guard(name: str, raw: str, config: dict[str, Any]) -> str | None:
    """Run the hook script `name` on the hook input `raw`; its stderr when it refused (exit 2), else None."""
    previous = sys.stdin
    try:
        module = guard_module(name)
        stdin = io.TextIOWrapper(io.BytesIO(raw.encode("utf-8")), encoding="utf-8")
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            sys.stdin = stdin
            code = module.main()
    except Exception as error:  # a broken guard must not lock the tools: it passes and leaves a line
        record(f"{name}: {type(error).__name__}: {error}")
        return None
    finally:
        sys.stdin = previous
    return (err.getvalue() or "Blocked.\n") if code == 2 else None


def _rule(what: str, judge: Callable[[], str | None]) -> str | None:
    """Run one rule; a rule that crashes refuses nothing, leaves a line, and does not stop the ones after it."""
    if RULES_ERROR is not None:
        return None
    try:
        return judge()
    except Exception as error:
        record(f"{what}: {type(error).__name__}: {error}")
        return None


def _protect(judge: Callable[[], str | None]) -> str | None:
    """Run the protect rules; like `_rule`, but they do not depend on the other rule modules."""
    if PROTECT_ERROR is not None:
        return None
    try:
        return judge()
    except Exception as error:
        record(f"protect: {type(error).__name__}: {error}")
        return None


def pre_tool_use(payload: dict[str, Any], raw: str, config: dict[str, Any]) -> tuple[int, str, str]:
    """Judge one tool call before it runs: (exit code, stdout, stderr)."""
    reasons: list[str] = []
    judged = (
        _protect(lambda: protect.refusal(payload)),
        _rule("roles", lambda: roles.check(config, payload)),
        _rule("identity", lambda: identity.assignment_refusal(_command(payload)) if _shell(payload) else None),
    )
    reasons.extend(reason for reason in judged if reason)
    for name, applies in PRE_GUARDS:
        try:
            runs = applies(payload)
        except Exception as error:
            record(f"{name}: {type(error).__name__}: {error}")
            runs = False
        if runs and not (name in LAST_GUARDS and reasons):
            reason = run_guard(name, raw, config)
            if reason:
                reasons.append(reason)
    if reasons:
        return 2, "", "".join(reasons)
    updated: dict[str, Any] | None = None
    if RULES_ERROR is None:
        try:
            updated = identity.updated_input(payload, config)
        except Exception as error:
            record(f"identity: {type(error).__name__}: {error}")
    if updated is None:
        return 0, "", ""
    output = {"hookSpecificOutput": {"hookEventName": "PreToolUse", "updatedInput": updated}}
    return 0, json.dumps(output), ""


def release_identity(payload: dict[str, Any]) -> None:
    """Remove the identity file of a shell command whose call has ended, failed or been denied."""
    if RULES_ERROR is None and _shell(payload):
        _rule("identity release", lambda: identity.release(payload))


def post_tool_use(payload: dict[str, Any], raw: str, config: dict[str, Any]) -> tuple[int, str, str]:
    """Judge one finished tool call: (exit code, stdout, stderr)."""
    release_identity(payload)
    reasons: list[str] = []
    for name, applies in POST_GUARDS:
        try:
            runs = applies(payload)
        except Exception as error:
            record(f"{name}: {type(error).__name__}: {error}")
            runs = False
        reason = run_guard(name, raw, config) if runs else None
        if reason:
            reasons.append(reason)
    return (2, "", "".join(reasons)) if reasons else (0, "", "")


def stop(payload: dict[str, Any]) -> tuple[int, str, str]:
    """Judge the end of a turn: (exit code, stdout, stderr). A block is exit code 2 with the reason (`answer` turns it
    into Claude Code's `decision: block`); a note for the user is a `systemMessage`. A guard that crashes passes."""
    reasons: list[str] = []
    notes: list[str] = []
    for name in STOP_GUARDS:
        try:
            verdict = guard_module(name).judge(payload)
        except Exception as error:  # a broken guard must not trap the coordinator
            record(f"{name}: {type(error).__name__}: {error}")
            continue
        if verdict.block:
            reasons.append(verdict.block.rstrip("\n") + "\n")
        if verdict.note:
            notes.append(verdict.note)
    if reasons:
        return 2, "", "".join(reasons)
    if notes:
        return 0, json.dumps({"systemMessage": " ".join(notes)}), ""
    return 0, "", ""


def session_start(payload: dict[str, Any], config: dict[str, Any], problem: str | None) -> tuple[int, str, str]:
    """The notices a session start shows: that the settings could not be read, and that the session has no role."""
    notices: list[str] = []
    if problem is not None:
        notices.append(
            f"Coney: the hooks' settings could not be read ({problem}), so no role rule is on; the guards still run."
        )
    notice = _rule("session notice", lambda: roles.session_notice(config, payload))
    if notice and problem is None:
        notices.append(notice)
    if not notices:
        return 0, "", ""
    text = " ".join(notices)
    output = {"systemMessage": text, "hookSpecificOutput": {"hookEventName": "SessionStart", "additionalContext": text}}
    return 0, json.dumps(output), ""


def dispatch(raw: str, config: dict[str, Any] | None = None) -> tuple[int, str, str]:
    """Handle one hook input (the JSON text Claude Code sends): (exit code, stdout, stderr)."""
    try:
        payload = json.loads(raw or "{}")
    except ValueError:
        return 0, "", ""
    if not isinstance(payload, dict):
        return 0, "", ""
    problem: str | None = None
    if config is None:
        config, problem = load_settings()
        if problem is not None:
            record(f"settings: {problem}")
    event = payload.get("hook_event_name")
    if event == "PreToolUse":
        return pre_tool_use(payload, raw, config)
    if event == "PostToolUse":
        return post_tool_use(payload, raw, config)
    if event in ("PostToolUseFailure", "PermissionDenied"):
        release_identity(payload)
        return 0, "", ""
    if event == "SessionStart":
        return session_start(payload, config, problem)
    if event == "Stop":
        return stop(payload)
    return 0, "", ""


def answer(raw: str, config: dict[str, Any] | None = None) -> tuple[int, str, str]:
    """What the script tells Claude Code about one hook input: (exit code, stdout, stderr).

    A refusal is the JSON answer with exit code 0 (`permissionDecision: deny` before a tool runs, `decision: block`
    after it), never exit code 2: uv and Python also exit with 2 when they cannot start, and a PreToolUse hook that
    exits with 2 blocks every tool call, Read included.
    """
    code, out, err = dispatch(raw, config)
    if code != 2:
        return code, out, err
    try:
        event = json.loads(raw).get("hook_event_name")
    except (ValueError, AttributeError):
        event = None
    if event == "PreToolUse":
        decision: dict[str, Any] = {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "permissionDecisionReason": err,
            }
        }
    else:
        decision = {"decision": "block", "reason": err}
    return 0, json.dumps(decision), ""


def other_harness(name: str, payload: dict[str, Any], config: dict[str, Any] | None = None) -> tuple[int, str, str]:
    """Judge a tool call of another harness (`harness.py` adapts it) by the same rules and guards: (exit code, stdout,
    stderr) in that harness's own format.

    The call's role is `harness.role` (default `contributor`), the identity token is left out (it is Claude Code's), and
    a call that stands for several (a Codex patch of several files) is refused when any of them is. While it judges,
    `$CLAUDE_PROJECT_DIR` is this checkout: `pr_guard.py` and `pcsx2_guard.py` read it first, and another tool may
    have none, or one inherited from a Claude Code session of another checkout.
    """
    if config is None:
        config, problem = load_settings()
        if problem is not None:
            record(f"settings: {problem}")
    settings = dict(config)
    settings["identity"] = {"enabled": False}
    reasons: list[str] = []
    before = os.environ.get("CLAUDE_PROJECT_DIR")
    os.environ["CLAUDE_PROJECT_DIR"] = str(HOOKS.parent.parent)
    try:
        for call in harness.claude_calls(name, payload, harness.role(config)):
            code, _, err = pre_tool_use(call, json.dumps(call), settings)
            if code == 2 and err not in reasons:
                reasons.append(err)
    finally:
        if before is None:
            os.environ.pop("CLAUDE_PROJECT_DIR", None)
        else:
            os.environ["CLAUDE_PROJECT_DIR"] = before
    return harness.answer(name, "".join(reasons) if reasons else None)


def harness_option(argv: list[str]) -> str | None:
    """The harness named by `--harness <name>` (or `--harness=<name>`), else None."""
    for index, word in enumerate(argv):
        if word == "--harness" and index + 1 < len(argv):
            return argv[index + 1]
        if word.startswith("--harness="):
            return word.split("=", 1)[1]
    return None


def answer_any(raw: str, named: str | None, config: dict[str, Any] | None = None) -> tuple[int, str, str]:
    """Answer one hook input from any harness: Claude Code's unchanged path (`answer`), or the adapter of the harness
    `--harness` names, or that the payload shows when the command named none (Cursor, Copilot and Grok Build also run
    `.claude/settings.json`'s command)."""
    payload: Any = None
    if named != "claude":
        try:
            payload = json.loads(raw or "{}")
        except ValueError:
            payload = None
    if named == "claude" or not isinstance(payload, dict) or HARNESS_ERROR is not None:
        if HARNESS_ERROR is not None and named not in (None, "claude"):
            record(f"harness: {HARNESS_ERROR}")
            return 0, "", ""
        return answer(raw, config)
    # What the payload shows wins: Grok Build also runs `.cursor/hooks.json`, whose command names Cursor. The name given
    # decides only a payload that looks like Claude Code's.
    detected = harness.detect(payload)
    name = detected if detected != "claude" else (named if named in harness.HARNESSES else "claude")
    if named is not None and named not in harness.HARNESSES:
        record(f"harness: unknown --harness {named!r}, read as {name}")
    if name == "claude":
        return answer(raw, config)
    return other_harness(name, payload, config)


def main(argv: list[str] | None = None) -> int:
    """Read the hook input from stdin and answer the harness that ran this file: stdout JSON, stderr reasons, exit
    code. `--harness <name>` names it (`.claude/settings.json` names none: Claude Code, or a harness that borrows
    it)."""
    raw = sys.stdin.buffer.read().decode("utf-8", errors="replace")
    try:
        code, out, err = answer_any(raw, harness_option(sys.argv[1:] if argv is None else argv))
    except Exception as error:  # last resort: the dispatcher itself broke. Say so, and let the call through
        record(f"dispatch: {type(error).__name__}: {error}")
        return 0
    if out:
        sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return code


if __name__ == "__main__":
    sys.exit(main())
