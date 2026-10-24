# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads `coney.local.toml`, the untracked per-machine configuration (see `coney.local.example.toml`)."""

from __future__ import annotations

import subprocess
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

#: Keys of `coney.local.toml` that hold folders. Each may be relative to the repository root, absolute or "".
PATH_KEYS: tuple[str, ...] = (
    "game_dir",
    "ghidra_install",
    "ghidra_projects",
    "pcsx2_dir",
    "pcsx2_root",
    "pcsx2_claims_dir",
    "pcsx2_states_dir",
    "build_claims_dir",
    "ccache_dir",
    "ccache_program",
    "tidy_cache_dir",
    "deps_dir",
    "scratch_dir",
    "jdk_home",
    "ghidra_mcp_repo",
    "pcsx2_mcp_dir",
    "ffmpeg_dir",
    "luac_program",
    "lua_dir",
)

#: Keys of `coney.local.toml` that hold whole numbers, with their defaults (`build_slots`: how many builds run at
#: once; `build_worktree_cap`: how many worktrees may hold a `build/` folder at once, 0 turning the cap off
#: (build_cap.py); `build_fast_lane_steps` and `build_fast_lane_tests`: the sizes below which a job uses the fast lane;
#: `build_fast_lane_test_seconds`: the recorded test time above which a filtered test run takes a main slot anyway, 0
#: turning that off; `test_jobs`: the tests `coney-tools build --test` runs in parallel (`ctest -j`), 0 meaning half the
#: cores or a build slot's share when that is more (build_run.py, `parallel_tests_for`);
#: `pcsx2_copies`: how many PCSX2 copies `pcsx2 setup-copies` makes; `pcsx2_pine_base` and `pcsx2_debug_base`: the
#: PINE and debug server ports of the first copy, each next copy taking the next port; `ccache_size_gb`: the cap of the
#: shared compiler cache, 0 turning it off, compiler_cache.py; `tidy_slots`: how many `check changed` clang-tidy runs
#: go at once, machine-wide; `tidy_jobs`: the clang-tidy processes of each, 0 meaning a build slot's share of the
#: cores, check_cli.py).
INT_KEYS: dict[str, int] = {
    "build_slots": 2,
    "build_worktree_cap": 7,
    "build_fast_lane_steps": 50,
    "build_fast_lane_tests": 20,
    "build_fast_lane_test_seconds": 300,
    "test_jobs": 0,
    "ccache_size_gb": 25,
    "tidy_slots": 1,
    "tidy_jobs": 0,
    "pcsx2_copies": 5,
    "pcsx2_pine_base": 28011,
    "pcsx2_debug_base": 21512,
}

#: The smallest value a number key accepts (default 0): `build_slots` and `tidy_slots` need a slot, 0 turns a fast
#: lane off, and a port is above the privileged range.
INT_MINIMUM: dict[str, int] = {
    "build_slots": 1,
    "tidy_slots": 1,
    "pcsx2_copies": 1,
    "pcsx2_pine_base": 1024,
    "pcsx2_debug_base": 1024,
}

LOCAL_FILE = "coney.local.toml"
#: In a plain clone (one not in a workspace's `repos/` folder), what the workspace folder beside it is named after:
#: `~/code/coney` keeps its scratch, cache, tools, Ghidra projects and emulators in `~/code/coney-workspace/`.
PLAIN_CLONE_SUFFIX = "-workspace"
EXAMPLE_FILE = "coney.local.example.toml"


class ConfigError(Exception):
    """A problem with the configuration; the message names the file and the problem, so users never see a traceback."""


@dataclass(frozen=True)
class Config:
    """The parsed local configuration."""

    #: Every entry of PATH_KEYS; None when the key is missing or "".
    paths: dict[str, Path | None]
    #: Every entry of INT_KEYS, the default where the file does not set it.
    numbers: dict[str, int] = field(default_factory=lambda: dict(INT_KEYS))


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


def disc_for_coney(game: Path) -> Path:
    """The disc to hand to Coney for `game`, a `game_dir`: the one `.iso` in the folder, else the folder itself
    (extracted files, a mounted disc, or several images, which are not guessed between); an image given itself stays."""
    if not game.is_dir():
        return game
    images = sorted(game.glob("*.iso"))
    return images[0] if len(images) == 1 else game


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

    unknown = sorted(set(data) - set(PATH_KEYS) - set(INT_KEYS))
    if unknown:
        known = ", ".join((*PATH_KEYS, *INT_KEYS))
        raise ConfigError(f"{path}: unknown key(s) {', '.join(unknown)}; known keys are {known}")

    paths: dict[str, Path | None] = {}
    for key in PATH_KEYS:
        value = data.get(key, "")
        if not isinstance(value, str):
            raise ConfigError(f"{path}: {key} must be a string, got {type(value).__name__}")
        paths[key] = (root / value).resolve() if value else None
    numbers = dict(INT_KEYS)
    for key in INT_KEYS:
        value = data.get(key, INT_KEYS[key])
        least = INT_MINIMUM.get(key, 0)
        if isinstance(value, bool) or not isinstance(value, int) or value < least:
            raise ConfigError(f"{path}: {key} must be a whole number of at least {least}, got {value!r}")
        numbers[key] = value
    return Config(paths=paths, numbers=numbers)


def workspace_root(root: Path) -> Path:
    """The folder that holds the workspace's sibling folders (`scratch/`, `cache/`, `tools/`, `ghidra/`) for the
    checkout at `root`.

    In the workspace layout (docs/guides/workspace.md) the main checkout is `<workspace>/repos/coney`, so it is the
    workspace, two levels up; worktrees share their main checkout's. A plain clone anywhere else would otherwise
    write two levels above itself (`~/code/coney` writing `~/scratch`), so there it is a folder beside the clone,
    `<clone>-workspace`. It is never inside the clone: the Ghidra projects, the copied executable, the emulators and
    the BIOS stay out of the repository (LEGAL.md). A path set in coney.local.toml always wins over this default.
    """
    main = main_checkout(root).resolve()
    return main.parent.parent if main.parent.name == "repos" else main.parent / (main.name + PLAIN_CLONE_SUFFIX)


def workspace_folder(root: Path, name: str) -> Path:
    """`name` (such as `scratch` or `cache/ccache`) in the workspace of the checkout at `root` (`workspace_root`)."""
    return workspace_root(root) / name
