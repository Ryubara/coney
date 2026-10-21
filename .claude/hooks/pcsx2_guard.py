# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: keep an agent off a PCSX2 copy it has not claimed.

Every agent of a session shares one `pcsx2` MCP server with one connection, so one agent's `pcsx2_connect` moves, and
its `pcsx2_pause` or `pcsx2_write_memory` lands in, another agent's game. This hook, registered in
`.claude/settings.json` for the `mcp__pcsx2__.*` tools, refuses a `pcsx2_connect` that carries no `agent` or whose
ports are not those of a copy the caller claimed. Its second registration, on the shell tools (only for a command that
mentions both `coney-tools` and `pcsx2`), reads the `--agent ID` of a claiming `coney-tools pcsx2 ...` command to learn
which claim the caller holds; it never refuses anything.

The rules and their reasons are in `coney_tools/pcsx2_guard.py` and docs/guides/agents.md#pcsx2-guard. This script
only reads the payload and runs them. Exit code 2 blocks the call and shows stderr to the agent; every other outcome
exits 0, so a broken guard never locks the emulator out. Each one that is the guard's own failure (a payload it cannot
read, an error inside it, a package that does not load) leaves one line in `guard.log` beside the claims, or in the
system temp folder when the package is what cannot load. Only a hook killed by the timeout leaves none.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

#: The package, relative to this file (`.claude/hooks/` of the checkout): the hook runs without the project's venv.
PACKAGE = Path(__file__).resolve().parents[2] / "python" / "src"
#: The log of last resort, in the system temp folder (the same name as `coney_tools.pcsx2_guard.FALLBACK_LOG_FILE`).
FALLBACK_LOG_FILE = "coney-pcsx2-guard.log"


def start_folder(data: dict[str, Any]) -> Path:
    """A folder of the checkout, to find the claims from: the project directory Claude Code names, else the payload's
    `cwd` (when it is a string), else this process's."""
    cwd = data.get("cwd")
    return Path(os.environ.get("CLAUDE_PROJECT_DIR") or (cwd if isinstance(cwd, str) and cwd else Path.cwd()))


def record(start: Path, error: BaseException) -> None:
    """Leave one line about a failure of the guard: in `guard.log` when the package loads, else in the temp folder.
    Never raises."""
    try:
        if str(PACKAGE) not in sys.path:
            sys.path.insert(0, str(PACKAGE))
        from coney_tools import pcsx2_guard

        if pcsx2_guard.log_failure(start, error):
            return
    except Exception:
        pass
    try:
        line = f"{time.strftime('%Y-%m-%dT%H:%M:%S')} {type(error).__name__}: {str(error)[:1000]}".replace("\n", " ")
        with (Path(tempfile.gettempdir()) / FALLBACK_LOG_FILE).open("a", encoding="utf-8") as log:
            log.write(line + "\n")
    except Exception:
        return


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    start = start_folder({})
    try:
        data = json.loads(sys.stdin.read() or "{}")
    except (ValueError, OSError) as error:
        record(start, ValueError(f"unreadable hook payload: {error}"))
        return 0
    if not isinstance(data, dict):
        record(start, ValueError(f"hook payload is not a JSON object: {type(data).__name__}"))
        return 0
    start = start_folder(data)
    try:
        sys.path.insert(0, str(PACKAGE))
        from coney_tools import pcsx2_guard

        reason = pcsx2_guard.handle(data, pcsx2_guard.default_guard(start))
    except Exception as error:  # a broken guard must not lock the emulator out, but it leaves a line
        record(start, error)
        return 0
    if reason:
        sys.stderr.write(reason + "\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
