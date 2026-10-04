# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads `coney.local.toml`, the untracked per-machine configuration (see `coney.local.example.toml`)."""

from __future__ import annotations

import tomllib
from dataclasses import dataclass
from pathlib import Path

#: Keys of `coney.local.toml` that hold folders. Each may be relative to the repository root, absolute or "".
PATH_KEYS: tuple[str, ...] = (
    "game_dir",
    "ghidra_install",
    "ghidra_projects",
    "pcsx2_dir",
    "scratch_dir",
    "jdk_home",
    "ghidra_mcp_repo",
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


def load_config(root: Path) -> Config:
    """Read `root / "coney.local.toml"`.

    Relative paths resolve against `root`. Raises ConfigError for a missing or unreadable file, invalid TOML, an
    unknown key (a typo silently ignored would point a tool at the wrong folder) or a value of the wrong type.
    """
    path = root / LOCAL_FILE
    if not path.is_file():
        raise ConfigError(f"no {LOCAL_FILE} in {root}; copy {EXAMPLE_FILE} and edit it")
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
