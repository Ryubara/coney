# SPDX-License-Identifier: GPL-3.0-or-later
"""The one entry point of Coney's Claude Code hooks: `.claude/settings.json` runs this file for every event.

Claude Code starts one process per hook entry, and each entry here cost a Python start-up (a `uv run` each) on every
shell command. Now it starts this file once per event (PreToolUse for every tool, PostToolUse for the shell tools,
SessionStart), and the checks run inside it:

* **PreToolUse**, in this order: what no one may touch (`protect.py`); the role rules (`roles.py`: a session with no
  role while `require_role` is on, then the refusals of the caller's role); a command that sets `CONEY_AGENT_*`
  (`identity.py`);
  then the guards that were separate hooks, loaded as modules and run unchanged (`build_guard.py`, `issue_guard.py`,
  `bounded_waits.py`, `pr_guard.py`, `push_guard.py` for the shell tools; `pcsx2_guard.py` for the shell commands that
  name PCSX2 and for the PCSX2 MCP tools; `agent_guard.py` for the Agent tool). Every refusal is collected and sent
  together, as the separate hooks' were. When nothing refuses, a `coney-tools` or `git commit` command gets the
  caller's identity token put in front of it (`identity.py`, returned as `updatedInput`).
* **PostToolUse**: `trailer_guard.py` after a `git commit`; the identity file of the command is removed, also when
  the command failed (PostToolUseFailure) or was denied (PermissionDenied).
* **SessionStart**: a notice when the session has no role, or when `hooks.toml` could not be read.

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

SHELLS = ("Bash", "PowerShell")
LOG_FILE = "coney-hooks.log"
#: The shell commands pcsx2_guard reads: `coney-tools ... pcsx2` (the old `if` filter `*coney-tools*pcsx2*`).
_PCSX2_COMMAND = re.compile(r"coney-tools.*pcsx2", re.DOTALL)
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


def _agent(payload: dict[str, Any]) -> bool:
    """Whether the call starts an agent."""
    return payload.get("tool_name") in ("Agent", "Task")


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
    ("bounded_waits", _shell),
    ("pr_guard", _shell),
    ("push_guard", _shell),
    ("pcsx2_guard", _pcsx2),
    ("agent_guard", _agent),
]
#: The guards of PostToolUse.
POST_GUARDS: list[tuple[str, Callable[[dict[str, Any]], bool]]] = [("trailer_guard", _commit)]


def run_guard(name: str, raw: str, config: dict[str, Any]) -> str | None:
    """Run the hook script `name` on the hook input `raw`; its stderr when it refused (exit 2), else None."""
    previous = sys.stdin
    try:
        module = guard_module(name)
        stdin = io.TextIOWrapper(io.BytesIO(raw.encode("utf-8")), encoding="utf-8")
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            sys.stdin = stdin
            if name == "pr_guard":
                pr = config.get("pr_guard", {})
                code = module.main(refuse_create=isinstance(pr, dict) and pr.get("refuse_pr_create") is True)
            else:
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
        if runs:
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


def main() -> int:
    """Read the hook input from stdin and answer Claude Code: stdout JSON, stderr reasons, exit code."""
    raw = sys.stdin.buffer.read().decode("utf-8", errors="replace")
    try:
        code, out, err = answer(raw)
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
