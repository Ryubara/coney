# SPDX-License-Identifier: GPL-3.0-or-later
"""The compiler cache every `coney-tools build` shares: ccache, as in CI (docs/guides/building.md#compiler-cache).

Every agent's worktree is a fresh checkout of nearly the same sources, so without a cache each one compiles the whole
engine, its tests and the dependencies again. With it, a worktree's first build takes the objects another worktree
already compiled. The run's environment does it all, nothing in the presets or CMake files:

- `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER` name ccache. CMake reads them only when it first
  configures a folder, so a folder configured before keeps building as it did (a build running when the cache was
  turned on is never changed under it), and a new folder gets the cache.
- `CCACHE_DIR` is one folder for the machine (`ccache_dir`, default `cache/ccache` in the workspace), capped at
  `ccache_size_gb` (default 25; 0 turns the cache off and sets `CCACHE_DISABLE` for folders configured with it).
- `CCACHE_BASEDIR` is the checkout, so ccache rewrites its absolute paths relative to the build folder: the same
  source in two worktrees is one command line and one entry. `CCACHE_NOHASHDIR` keeps the build folder's own path
  out of the hash for the same reason.
- `CCACHE_STATSLOG` is a file of this run alone, so the summary reports this build's hits, not the machine's (other
  builds use the same cache at the same time).

The program is `ccache_program`, else the copy `workspace init --fetch ccache` put in the workspace's tools folder,
else `ccache` on PATH. Nothing here raises: a cache that cannot be used is reported and the build runs without it.
"""

from __future__ import annotations

import shutil
import sys
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from pathlib import Path

from coney_tools import workspace_fetch
from coney_tools.config import ConfigError, load_config, local_config_file, main_checkout

#: The default cap in GB when coney.local.toml sets none (the `ccache_size_gb` default of config.INT_KEYS).
DEFAULT_SIZE_GB = 25
#: The cache's folder in the workspace (beside repos/, worktrees/ and scratch/), when `ccache_dir` is not set.
DEFAULT_DIR = Path("cache") / "ccache"

#: ccache's counters (`--print-log-stats`) that count a call it could not cache: a link, a precompiled header, an
#: option it does not support, and so on. Hits and misses are counted apart; the rest are not calls.
UNCACHEABLE = frozenset(
    {
        "autoconf_test",
        "bad_compiler_arguments",
        "bad_input_file",
        "called_for_link",
        "called_for_preprocessing",
        "compiler_check_failed",
        "compiler_produced_empty_output",
        "compiler_produced_no_output",
        "could_not_find_compiler",
        "could_not_use_modules",
        "could_not_use_precompiled_header",
        "disabled",
        "error_hashing_extra_file",
        "missing_cache_file",
        "multiple_source_files",
        "no_input_file",
        "output_to_stdout",
        "preprocessor_error",
        "unsupported_code_directive",
        "unsupported_compiler_option",
        "unsupported_environment_variable",
        "unsupported_source_encoding",
        "unsupported_source_language",
    }
)


@dataclass(frozen=True)
class CompilerCache:
    """The machine's compiler cache: the program, its folder and its cap (0: off)."""

    program: Path | None
    directory: Path
    size_gb: int
    #: Why there is no program, for the summary (empty when there is one).
    missing: str = ""

    @property
    def on(self) -> bool:
        """Whether builds use it: a program was found and the cap is above 0."""
        return self.program is not None and self.size_gb > 0


@dataclass(frozen=True)
class Stats:
    """One run's calls of the cache, from its stats log."""

    hits: int
    misses: int
    uncacheable: int

    @property
    def rate(self) -> float:
        """Hits as a share of the cacheable calls (0 without any)."""
        cacheable = self.hits + self.misses
        return self.hits / cacheable if cacheable else 0.0

    def line(self) -> str:
        """The summary line: `compiler cache: 980 hits, 12 misses (98.8 %), 3 uncacheable`."""
        text = f"compiler cache: {self.hits} hits, {self.misses} misses ({100 * self.rate:.1f} %)"
        return text + (f", {self.uncacheable} uncacheable" if self.uncacheable else "")


def _executable() -> str:
    """ccache's file name on this platform."""
    return "ccache.exe" if sys.platform == "win32" else "ccache"


def find(root: Path, which: Callable[[str], str | None] = shutil.which) -> CompilerCache:
    """The compiler cache of the checkout at `root`, from coney.local.toml (this checkout's, else the main checkout's)
    and the defaults: `ccache_program`, else the fetched copy in the workspace's tools folder, else `ccache` on PATH;
    `ccache_dir`, else `cache/ccache` in the workspace; `ccache_size_gb`, else 25."""
    workspace = (main_checkout(root) / ".." / "..").resolve()
    try:
        config = load_config(root) if local_config_file(root) else None
    except ConfigError:
        config = None  # the build's own precheck reports a broken file; the cache only stays off
    size = config.numbers.get("ccache_size_gb", DEFAULT_SIZE_GB) if config else DEFAULT_SIZE_GB
    directory = (config.paths.get("ccache_dir") if config else None) or workspace / DEFAULT_DIR
    configured = config.paths.get("ccache_program") if config else None
    if configured is not None:
        if configured.is_file():
            return CompilerCache(configured, directory, size)
        return CompilerCache(None, directory, size, f"ccache_program {configured} is not a file")
    fetched = workspace_fetch.tool_destination("ccache", workspace) / _executable()
    if fetched.is_file():
        return CompilerCache(fetched, directory, size)
    found = which("ccache")
    if found:
        return CompilerCache(Path(found), directory, size)
    hint = "`coney-tools workspace init --fetch ccache`" if sys.platform == "win32" else "apt or brew install ccache"
    return CompilerCache(None, directory, size, f"ccache not found ({hint})")


def _has(env: Mapping[str, str], name: str) -> bool:
    """Whether `env` sets `name`, in any case (Windows names are case-insensitive)."""
    return any(key.upper() == name.upper() for key in env)


def environment(cache: CompilerCache, checkout: Path, stats_log: Path, base: Mapping[str, str]) -> dict[str, str]:
    """The variables a build run adds to `base` for the cache (see the module's docstring). A launcher the caller set
    already is kept. Off, it only sets `CCACHE_DISABLE`, so a folder configured with the cache compiles without it."""
    if not cache.on or cache.program is None:
        return {"CCACHE_DISABLE": "1"}
    added = {
        "CCACHE_DIR": str(cache.directory),
        "CCACHE_MAXSIZE": f"{cache.size_gb}G",
        "CCACHE_BASEDIR": str(checkout.resolve()),
        "CCACHE_NOHASHDIR": "1",
        "CCACHE_STATSLOG": str(stats_log),
    }
    for name in ("CMAKE_C_COMPILER_LAUNCHER", "CMAKE_CXX_COMPILER_LAUNCHER"):
        if not _has(base, name):
            added[name] = cache.program.as_posix()
    return added


def write_config(cache: CompilerCache) -> None:
    """Write the cap into the cache folder's own `ccache.conf` (`max_size`), keeping its other lines. The environment
    sets it for a build, but a ccache started by hand (`ccache -s`, or a person's own build) reads only the file, and
    with ccache's default of 5 GB it would trim the shared cache to that. A folder that cannot be written is left as it
    is: the build's environment still holds the cap."""
    if not cache.on:
        return
    path = cache.directory / "ccache.conf"
    try:
        lines = path.read_text(encoding="utf-8").splitlines() if path.is_file() else []
        wanted = f"max_size = {cache.size_gb}G"
        kept = [line for line in lines if line.split("=", 1)[0].strip() != "max_size"]
        if wanted in lines and len(kept) == len(lines) - 1:
            return
        cache.directory.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join([*kept, wanted]) + "\n", encoding="utf-8")
    except OSError:
        pass


def parse_stats(text: str) -> Stats | None:
    """The hits, misses and uncacheable calls in ccache's `--print-log-stats` output (`id<TAB>value` lines); None when
    it holds no counter."""
    counters: dict[str, int] = {}
    for line in text.splitlines():
        name, tab, value = line.strip().partition("\t")
        if tab and value.strip().isdigit():
            counters[name] = int(value)
    if not counters:
        return None
    hits = counters.get("direct_cache_hit", 0) + counters.get("preprocessed_cache_hit", 0)
    uncacheable = sum(value for name, value in counters.items() if name in UNCACHEABLE)
    return Stats(hits, counters.get("cache_miss", 0), uncacheable)
