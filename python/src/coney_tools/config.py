# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads `coney.local.toml`, the untracked per-machine configuration (see `coney.local.example.toml`)."""

from __future__ import annotations

import subprocess
import tomllib
from dataclasses import dataclass
from pathlib import Path

#: Keys of `coney.local.toml` that hold folders. Each may be relative to the repository root, absolute or "".
PATH_KEYS: tuple[str, ...] = (
    "game_dir",
    "ghidra_install",
    "ghidra_projects",
    "pcsx2_dir",
    "pcsx2_root",
    "pcsx2_claims_dir",
    "scratch_dir",
    "jdk_home",
    "ghidra_mcp_repo",
    "pcsx2_mcp_dir",
    "ffmpeg_dir",
)

LOCAL_FILE = "coney.local.toml"
EXAMPLE_FILE = "coney.local.example.toml"


class ConfigError(Exception):
    """A problem with the configuration; the message names the file and the problem, so users never see a traceback."""


@dataclass(frozen=True)
class Config:
    """The parsed local configuration."""

    #: Every entry of PATH_KEYS; None when the key is missing or "".
    paths: dict[str, Path | None]


def find_repo_root(start: Path) -> Path:
    """Return the nearest ancestor of `start` (or `start` itself) that holds `coney.local.example.toml`.

    Raises ConfigError when there is none, which means `start` is not inside a Coney checkout.
    """
    start = start.resolve()
    for candidate in (start, *start.parents):
        if (candidate / EXAMPLE_FILE).is_file():
            return candidate
    raise ConfigError(f"{start}: not inside a Coney checkout (no {EXAMPLE_FILE} in it or any parent folder)")


def main_checkout(root: Path) -> Path:
    """The main checkout of the repository at `root` (a worktree's `.git` points back to it); `root` itself when it
    is the main one or git cannot say."""
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--path-format=absolute", "--git-common-dir"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return root
    common = Path(result.stdout.strip()) if result.returncode == 0 and result.stdout.strip() else None
    return common.parent if common is not None and common.name == ".git" else root


def local_config_file(root: Path) -> Path | None:
    """The `coney.local.toml` that applies to the checkout at `root`, or None when there is none.

    The file is untracked, so a worktree has none of its own: the main checkout's is used (a worktree that holds
    its own copy keeps using that one). Relative paths inside it resolve against the folder that holds it, so a
    worktree sees the same folders as the main checkout.
    """
    for candidate in dict.fromkeys((root, main_checkout(root))):
        if (candidate / LOCAL_FILE).is_file():
            return candidate / LOCAL_FILE
    return None


def load_config(root: Path) -> Config:
    """Read the `coney.local.toml` of the checkout at `root` (see `local_config_file`: a worktree reads the main
    checkout's).

    Relative paths resolve against the folder that holds the file. Raises ConfigError for a missing or unreadable
    file, invalid TOML, an unknown key (a typo silently ignored would point a tool at the wrong folder) or a value
    of the wrong type.
    """
    path = local_config_file(root)
    if path is None:
        main = main_checkout(root)
        where = str(root) if main == root else f"{root} or the main checkout {main}"
        raise ConfigError(f"no {LOCAL_FILE} in {where}; copy {EXAMPLE_FILE} and edit it")
    root = path.parent
    try:
        with path.open("rb") as handle:
            data = tomllib.load(handle)
    except (tomllib.TOMLDecodeError, UnicodeDecodeError, OSError) as error:
        raise ConfigError(f"{path}: {error}") from error

    unknown = sorted(set(data) - set(PATH_KEYS))
    if unknown:
        raise ConfigError(f"{path}: unknown key(s) {', '.join(unknown)}; known keys are {', '.join(PATH_KEYS)}")

    paths: dict[str, Path | None] = {}
    for key in PATH_KEYS:
        value = data.get(key, "")
        if not isinstance(value, str):
            raise ConfigError(f"{path}: {key} must be a string, got {type(value).__name__}")
        paths[key] = (root / value).resolve() if value else None
    return Config(paths=paths)
