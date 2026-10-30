# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: keep an agent off an xemu copy it has not claimed.

Every agent of a session shares one `xemu` MCP server. The server connects a caller only to the copy its `agent` id
holds; this hook makes sure the id is the caller's own: it refuses an `mcp__xemu__xemu_*` call whose `agent` is not an
id the caller used in one of its claiming `coney-tools xemu ... --agent ID` shell commands (which it reads from the
shell tools, refusing nothing there). The dispatcher (`dispatch.py`) runs it for the xemu MCP tools and for the shell
commands that name `coney-tools ... xemu`.

The rules and their reasons are in `coney_tools/xemu_guard.py` and docs/guides/coney-tools-xemu.md#xemu-guard. Exit
code 2 blocks the call and shows stderr to the agent; every other outcome exits 0, so a broken guard never locks the
emulator out, and leaves one line in `guard.log` beside the bindings (or the system temp folder when the package
cannot load).
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
#: The log of last resort, in the system temp folder (the hook cannot import the package when it writes this one).
FALLBACK_LOG_FILE = "coney-xemu-guard.log"


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
        from coney_tools import xemu_guard

        if xemu_guard.log_failure(start, error):
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
        from coney_tools import xemu_guard

        reason = xemu_guard.handle(data, xemu_guard.default_guard(start))
    except Exception as error:  # a broken guard must not lock the emulator out, but it leaves a line
        record(start, error)
        return 0
    if reason:
        sys.stderr.write(reason + "\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
