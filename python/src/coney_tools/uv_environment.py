# SPDX-License-Identifier: GPL-3.0-or-later
"""Running `uv run` without syncing the shared environment (docs/guides/coney-tools-repo.md#task-check).

`uv run --project python <tool>` syncs `python/.venv` (or the environment `UV_PROJECT_ENVIRONMENT` names) before it
starts the tool, and on Windows that fails ("Access is denied") while another session has `coney-tools.exe` or
`coney-agent.exe` open from that environment: every agent session on the machine does. `task check` runs ruff, mypy and
pytest this way, and it was failing, or its agents were working around it with `UV_NO_SYNC=1`, whenever another
session held the executable. The gate never needs a sync: the session that started it was started from the environment,
so it exists. `no_sync` puts `--no-sync` into such a command once the environment is built (a `pyvenv.cfg` in it); an
environment that is not built yet is left to `uv` to build on the first run. The hooks do the same for the commands they
start (`coney_tools_command` in `.claude/hooks/hooklib.py`).
"""

from __future__ import annotations

import os
from collections.abc import Sequence
from pathlib import Path


def project_environment(root: Path) -> Path:
    """The environment `uv run --project <root>/python` uses: the one `UV_PROJECT_ENVIRONMENT` names (a relative name
    is taken from the project folder, as `uv` does), else `python/.venv`."""
    named = os.environ.get("UV_PROJECT_ENVIRONMENT")
    return root / "python" / named if named else root / "python" / ".venv"


def no_sync(command: Sequence[str], root: Path) -> list[str]:
    """`command` with `--no-sync` after its `uv run`, when it is one and the environment of `root`'s project is built;
    any other command, or one that has the option, comes back as it was."""
    words = list(command)
    if words[:2] != ["uv", "run"] or "--no-sync" in words:
        return words
    if not (project_environment(root) / "pyvenv.cfg").is_file():
        return words
    return [*words[:2], "--no-sync", *words[2:]]
