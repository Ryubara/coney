# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: keep an agent off a PCSX2 copy it has not claimed.

Every agent of a session shares one `pcsx2` MCP server with one connection, so one agent's `pcsx2_connect` moves, and
its `pcsx2_pause` or `pcsx2_write_memory` lands in, another agent's game. This hook, registered in
`.claude/settings.json` for the `mcp__pcsx2__.*` tools, refuses a `pcsx2_connect` to ports that are not those of a copy
the caller claimed. Its second registration, on the shell tools (only for a command that mentions `coney-tools
pcsx2`), reads the `--agent ID` of a claiming `coney-tools pcsx2 ...` command to learn which claim the caller holds; it
never refuses anything.

The rules and their reasons are in `coney_tools/pcsx2_guard.py` and docs/guides/agents.md#setup. This script only
reads the payload and runs them. Exit code 2 blocks the call and shows stderr to the agent; every other outcome (also
input it cannot read, and anything that goes wrong inside the guard, which leaves one line in `guard.log` beside the
claims) exits 0, so a broken guard never locks the emulator out.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

#: The package, relative to this file (`.claude/hooks/` of the checkout): the hook runs without the project's venv.
PACKAGE = Path(__file__).resolve().parents[2] / "python" / "src"


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
    except (ValueError, OSError):
        return 0
    if not isinstance(data, dict):
        return 0
    start = Path(os.environ.get("CLAUDE_PROJECT_DIR") or data.get("cwd") or Path.cwd())
    try:
        sys.path.insert(0, str(PACKAGE))
        from coney_tools import pcsx2_guard

        try:
            reason = pcsx2_guard.handle(data, pcsx2_guard.default_guard(start))
        except Exception as error:  # a broken guard must not lock the emulator out, but it leaves a line
            pcsx2_guard.log_failure(start, error)
            return 0
    except Exception:  # the package itself cannot load
        return 0
    if reason:
        sys.stderr.write(reason + "\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
