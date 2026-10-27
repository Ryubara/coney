# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: the coordinator may not start an engineer whose slot is full.

The rule (docs/guides/agents.md#slots): a machine runs 3 task units and 1 fix unit at a time, a unit holds its slot
until it passes Review, and the agent of a unit that waits for CI holds none. This guard asks `coney-tools task slots
--admit` before each Agent call of the coordinator; the tool reads the task records (`TASK.md` for the kind, `unit.json`
and the stamp log for the liveness, a short reservation for an agent that has not run `task start` yet) and the
numbers in `.github/conventions.yml`, so none of that is copied here. The brief names the unit with a `Task id: <id>`
line, or says `Slot: none <reason>` for work that has no unit.

Only the main session's coordinator is held to it: an engineer starting its own helpers (`agent_type` is its own) is
not. The hook passes the call when `coney-tools` cannot run (a refusal is exit code 1 with the reasons; any other
failure is a line in `coney-hooks.log` and the call goes through), as the PR guard does. It is standard-library only,
registered through `dispatch.py`. Exit code 2 blocks the call and shows stderr to the agent; every other outcome
exits 0.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from pathlib import Path

#: Seconds `coney-tools task slots --admit` may take (a `uv run` start-up, the stamp logs, the slot lock).
TIMEOUT = 60
#: The role the guard holds to the rule.
ROLE = "coordinator"
AGENT_TOOLS = ("Agent", "Task")
LOG_FILE = "coney-hooks.log"

#: Runs `coney-tools <args>` with the brief on standard input; returns the exit code and what it printed.
Runner = Callable[[list[str], str], tuple[int, str]]


def record(what: str) -> None:
    """Append a line to the hooks' log in the temp folder."""
    try:
        with (Path(tempfile.gettempdir()) / LOG_FILE).open("a", encoding="utf-8") as log:
            log.write(f"{time.strftime('%Y-%m-%dT%H:%M:%S')} slot_guard: {what}\n")
    except OSError:
        return


def run_coney_tools(args: list[str], stdin: str) -> tuple[int, str]:
    """Run `coney-tools <args>` from the main checkout; (2, why) when it cannot be started."""
    root = Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path(__file__).resolve().parents[2])
    command = ["uv", "run", "--quiet", "--project", str(root / "python"), "coney-tools", *args]
    # uv warns when the session's VIRTUAL_ENV is not the project's; the warning would end up in the refusal.
    env = {name: value for name, value in os.environ.items() if name != "VIRTUAL_ENV"}
    try:
        done = subprocess.run(
            command, cwd=root, env=env, input=stdin.encode("utf-8"), capture_output=True, timeout=TIMEOUT, check=False
        )
    except (OSError, subprocess.SubprocessError) as error:
        return 2, str(error)
    out = done.stdout.decode("utf-8", errors="replace").strip()
    if done.returncode != 1:  # a refusal is the tool's own words; anything else keeps its error text for the log
        out = (out + "\n" + done.stderr.decode("utf-8", errors="replace")).strip()
    return done.returncode, out


def refusal(payload: object, runner: Runner | None = None) -> str | None:
    """The reason to refuse this hook input, or None to let the call run.

    Only an Agent call of the coordinator is judged. The tool decides what holds a slot (its `agent_types`), so a
    reviewer or a read-only helper passes without a slot, and the brief goes to it whole. `runner` is for the tests."""
    if not isinstance(payload, dict) or payload.get("tool_name") not in AGENT_TOOLS:
        return None
    if payload.get("agent_type") != ROLE:
        return None
    tool_input = payload.get("tool_input")
    if not isinstance(tool_input, dict):
        return None
    kind = tool_input.get("subagent_type")
    prompt = tool_input.get("prompt")
    agent_type = kind.strip() if isinstance(kind, str) else ""
    args = ["task", "slots", "--admit", f"--agent-type={agent_type}", "--brief", "-", "--reserve"]
    code, out = (runner or run_coney_tools)(args, prompt if isinstance(prompt, str) else "")
    if code == 1 and "Traceback (most recent call last)" not in out:
        return out + "\n"
    if code != 0:
        record(f"could not check the slots (exit {code}): {out[-300:]}")
    return None


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        payload = json.loads(sys.stdin.read() or "{}")
    except (ValueError, OSError):
        return 0
    message = refusal(payload)
    if message is not None:
        sys.stderr.write(message)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
