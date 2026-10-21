# SPDX-License-Identifier: GPL-3.0-or-later
"""The dependency sources every `coney-tools build` of a machine shares (docs/guides/building.md#shared-deps).

`cmake/deps.cmake` fetches SDL3, librw, Catch2, Dear ImGui, ENet and FFmpeg at pinned versions: into each build
folder's `_deps/` by default. With `CONEY_DEPS_SOURCES` naming a folder it fetches them into
`<folder>/<version>/<name>` once (the version is a hash of the pin lines, worked out on every configure), stamps each,
and every later configure of that version uses the stamped copies without a download: the mechanism CI's dependency
cache uses. It also locks the version's folder while it fetches, so two first configures never fetch into it at once.

A build run sets that variable to `deps_dir` of coney.local.toml, default `cache/deps` in the workspace. CMake keeps the
folder in a build folder's cache from its first configure, so folders configured before keep their own copies. A
variable the caller set (`--env CONEY_DEPS_SOURCES=` with no value turns the sharing off) is kept.
"""

from __future__ import annotations

from collections.abc import Mapping
from pathlib import Path

from coney_tools.config import ConfigError, load_config, local_config_file, workspace_root

#: The shared folder in the workspace (beside the compiler cache), when `deps_dir` is not set.
DEFAULT_DIR = Path("cache") / "deps"
#: The variable cmake/deps.cmake reads.
VARIABLE = "CONEY_DEPS_SOURCES"


def folder(root: Path) -> Path:
    """The shared sources folder for the checkout at `root`: `deps_dir`, else `cache/deps` in the workspace."""
    try:
        config = load_config(root) if local_config_file(root) else None
    except ConfigError:
        config = None  # the build's own precheck reports a broken file
    configured = config.paths.get("deps_dir") if config else None
    return configured or workspace_root(root) / DEFAULT_DIR


def environment(root: Path, base: Mapping[str, str]) -> dict[str, str]:
    """The variable a build run adds to `base`: `CONEY_DEPS_SOURCES` naming the shared folder, unless `base` sets it
    already (in any case) or the checkout has no `cmake/deps.cmake`."""
    if any(key.upper() == VARIABLE for key in base) or not (root / "cmake" / "deps.cmake").is_file():
        return {}
    return {VARIABLE: folder(root).as_posix()}
