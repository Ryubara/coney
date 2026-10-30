# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code Stop hook: the coordinator may not end a turn while an engineer place is free for no reason.

The rule (docs/guides/slot-fill.md): the places are always full unless there is a good reason. `slot_guard.py`
stops the coordinator starting too many engineers; nothing stopped it from ending a turn with too few, which is what
this hook does. It asks `coney-tools task slots --fill-check --session <id>` at the end of each turn of the
coordinator. The tool reads the places, the ready issues under the start gate and the failed pull requests, works out
the reasons that excuse a free place (no eligible issue, the claim limit, low disk) and the active waivers, and exits 1
with a message that starts with `Blocked:` when a place should be filled; the hook then blocks the end of the turn with
that message as the reason, so the coordinator starts the work or records a waiver.

It must never trap the coordinator, so it blocks only on that one answer: exit code 1 *and* a message starting with
`Blocked:` (a crash exits with 1 too, with nothing on standard output). Every other outcome (no `uv`, a crash, a
timeout, a bad option) passes the turn with a visible note and a line in `coney-hooks.log`. The tool itself lets a
session's turn end after `tasks.start.fill.block.max_in_a_row` blocks in a row, and a start or a waiver ends the blocks.

Only the main session's coordinator is held to it (`agent_type`). It is standard-library only and is run by
`dispatch.py` for the `Stop` event, through `judge`; `main` is the same check as a script of its own.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

HOOKS = Path(__file__).resolve().parent
if str(HOOKS) not in sys.path:
    sys.path.insert(0, str(HOOKS))
from hooklib import coney_tools_command  # noqa: E402

#: Seconds `coney-tools task slots --fill-check` may take: its GitHub reads take at most `budget_seconds` (30) and the
#: call in flight `github_seconds` (12) more, then `uv run` starts Python. A test holds this above both numbers of the
#: conventions plus 20 s, and the timeout in `.claude/settings.json` above this: a hook that timed out would be
#: reported by Claude Code as a failure of the hook, not as a pass.
TIMEOUT = 80
#: The exit code of a failing check, and the word its message starts with.
FAILED = 1
BLOCKED = "Blocked:"
#: The role the hook holds to the rule.
ROLE = "coordinator"
LOG_FILE = "coney-hooks.log"

#: Runs `coney-tools <args>`; returns the exit code and what it printed.
Runner = Callable[[list[str]], tuple[int, str]]


@dataclass(frozen=True)
class Verdict:
    """What the hook does with a turn end: block it with `block`, else let it end, showing `note` if there is one."""

    block: str | None = None
    note: str | None = None


def record(what: str) -> None:
    """Append a line to the hooks' log in the temp folder."""
    try:
        with (Path(tempfile.gettempdir()) / LOG_FILE).open("a", encoding="utf-8") as log:
            log.write(f"{time.strftime('%Y-%m-%dT%H:%M:%S')} slot_fill_guard: {what}\n")
    except OSError:
        return


def root_folder() -> Path:
    """The checkout the session started in, where `coney-tools` runs from."""
    return Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path(__file__).resolve().parents[2])


def tool_command(args: list[str]) -> list[str]:
    """The command line that runs `coney-tools <args>`."""
    return coney_tools_command(root_folder(), args)


def run_coney_tools(args: list[str]) -> tuple[int, str]:
    """Run `coney-tools <args>` from the main checkout; (2, why) when it cannot be started."""
    # uv warns when the session's VIRTUAL_ENV is not the project's; the warning would end up in the message.
    env = {name: value for name, value in os.environ.items() if name != "VIRTUAL_ENV"}
    try:
        done = subprocess.run(
            tool_command(args), cwd=root_folder(), env=env, capture_output=True, timeout=TIMEOUT, check=False
        )
    except (OSError, subprocess.SubprocessError) as error:
        return 2, str(error)
    out = done.stdout.decode("utf-8", errors="replace").strip()
    if done.returncode != 0 and not out.startswith(BLOCKED):
        out = (out + "\n" + done.stderr.decode("utf-8", errors="replace")).strip()
    return done.returncode, out


def judge(payload: object, runner: Runner | None = None) -> Verdict:
    """The verdict on one Stop hook input.

    Only the coordinator's turn end is judged. `runner` is for the tests."""
    if not isinstance(payload, dict) or payload.get("hook_event_name") not in (None, "Stop"):
        return Verdict()
    if payload.get("agent_type") != ROLE:
        return Verdict()
    session = payload.get("session_id")
    args = [
        "task",
        "slots",
        "--fill-check",
        "--session",
        session if isinstance(session, str) and session else "unknown",
    ]
    code, out = (runner or run_coney_tools)(args)
    if code == FAILED and out.startswith(BLOCKED):
        return Verdict(block=out)
    if code != 0:
        record(f"could not check the places (exit {code}): {out[-300:]}")
        return Verdict(
            note=f"Coney: the slot-fill check could not run (exit {code}), so this turn end was not checked."
        )
    notes = [line[len("warning:") :].strip() for line in out.splitlines() if line.startswith("warning:")]
    if notes:
        record(f"let through with a check skipped: {notes[0][:300]}")
        return Verdict(note="Coney: the slot-fill check passed with a warning: " + "; ".join(notes)[:400])
    return Verdict()


def main() -> int:
    """Read the hook's JSON from stdin and answer Claude Code: a block as `decision: block` with the reason, a note
    as `systemMessage`; always exit code 0."""
    try:
        payload = json.loads(sys.stdin.read() or "{}")
    except (ValueError, OSError):
        return 0
    try:
        verdict = judge(payload)
    except Exception as error:  # a broken hook must not trap the coordinator
        record(f"{type(error).__name__}: {error}")
        return 0
    if verdict.block is not None:
        sys.stdout.write(json.dumps({"decision": "block", "reason": verdict.block}))
    elif verdict.note is not None:
        sys.stdout.write(json.dumps({"systemMessage": verdict.note}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
