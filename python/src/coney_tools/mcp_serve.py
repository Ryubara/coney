# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools mcp serve NAME`: start one of the debugging servers that the committed `.mcp.json` registers.

`.mcp.json` cannot hold paths: every machine keeps Ghidra's bridge and the PCSX2 server in its own place, and a
worktree sits at a different depth from the main checkout. So `.mcp.json` runs this command, which finds the server
through `coney.local.toml` (a worktree reads the main checkout's file) and runs it with stdin and stdout passed
through. A server that is not installed stops with one line on stderr: the agent tool shows it as failed and every
other tool keeps working. See docs/guides/agents.md#setup.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from collections.abc import Callable, Mapping
from pathlib import Path

from coney_tools.config import ConfigError, find_repo_root, load_config

#: The servers `.mcp.json` registers, by the name agents see them under.
SERVERS: tuple[str, ...] = ("ghidra-mcp", "pcsx2")

#: Where `tools/start-ghidra-mcp.ps1` serves the shared Ghidra project.
GHIDRA_MCP_URL = "http://127.0.0.1:8090"

_GUIDE = "see docs/guides/workspace.md"

Which = Callable[[str], str | None]


def _require_dir(key: str, path: Path | None, what: str) -> Path:
    """The configured folder for `key`; ConfigError naming the key when it is unset or missing."""
    if path is None:
        raise ConfigError(f"{key} is not set in coney.local.toml, so {what} cannot start ({_GUIDE})")
    if not path.is_dir():
        raise ConfigError(f"{key}: {path} does not exist, so {what} cannot start ({_GUIDE})")
    return path


def _ghidra_command(paths: Mapping[str, Path | None], which: Which, env: Mapping[str, str]) -> list[str]:
    """ghidra-mcp's Python bridge, run by uv from the configured ghidra-mcp checkout (uv keeps the bridge's
    environment in that checkout's own .venv)."""
    repo = _require_dir("ghidra_mcp_repo", paths["ghidra_mcp_repo"], "the ghidra-mcp bridge")
    # `uv run` sets UV to its own path, so the bridge runs under the uv that started coney-tools.
    uv = env.get("UV") or which("uv")
    if not uv:
        raise ConfigError("uv is not on PATH, so the ghidra-mcp bridge cannot start (https://docs.astral.sh/uv/)")
    # --frozen: install from ghidra-mcp's own uv.lock and never rewrite it; the checkout is shared and read-only to us.
    return [uv, "run", "--frozen", "--quiet", "--directory", str(repo), "bridge-mcp-ghidra", "--transport", "stdio"]


def _pcsx2_command(paths: Mapping[str, Path | None], which: Which) -> list[str]:
    """PCSX2-MCP's Node.js server. `pcsx2_mcp_dir` may be the server's own folder or the release folder that
    holds it in `pcsx2-mcp-server/`."""
    folder = _require_dir("pcsx2_mcp_dir", paths["pcsx2_mcp_dir"], "the PCSX2 server")
    for candidate in (folder / "dist" / "index.js", folder / "pcsx2-mcp-server" / "dist" / "index.js"):
        if candidate.is_file():
            script = candidate
            break
    else:
        raise ConfigError(f"pcsx2_mcp_dir: no dist/index.js in {folder} or its pcsx2-mcp-server/ ({_GUIDE})")
    node = which("node")
    if not node:
        raise ConfigError("Node.js (node) is not on PATH, so the PCSX2 server cannot start (https://nodejs.org/)")
    return [node, str(script)]


def server_command(
    name: str, paths: Mapping[str, Path | None], which: Which, env: Mapping[str, str]
) -> tuple[list[str], dict[str, str]]:
    """The command line that starts server `name` and the variables to add to its environment.

    `paths` is the configuration's path table, `which` finds a program on PATH and `env` is the current environment
    (both passed in so tests run without the tools). Raises ConfigError when the server is not installed.
    """
    if name == "ghidra-mcp":
        extra = {
            "GHIDRA_MCP_URL": env.get("GHIDRA_MCP_URL", GHIDRA_MCP_URL),
            # Load every tool group at connect: Claude Code does not pick up groups loaded later.
            "GHIDRA_MCP_LAZY": "0",
        }
        return _ghidra_command(paths, which, env), extra
    if name == "pcsx2":
        return _pcsx2_command(paths, which), {}
    raise ConfigError(f"unknown server {name!r}; known: {', '.join(SERVERS)}")


def run_serve(name: str) -> int:
    """Start server `name` with stdin and stdout passed through and return its exit status.

    The checkout is the agent tool's project folder (`CLAUDE_PROJECT_DIR`) when set, else the current folder.
    """
    start = Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path.cwd())
    config = load_config(find_repo_root(start))
    command, extra = server_command(name, config.paths, shutil.which, os.environ)
    # Nothing may reach stdout before the server starts: it carries the protocol.
    sys.stdout.flush()
    return _run_server(command, {**os.environ, **extra})


def _run_server(command: list[str], env: dict[str, str]) -> int:
    """Run the server with this process's stdin and stdout and return its exit status (tests replace it)."""
    return subprocess.run(command, env=env, check=False).returncode
