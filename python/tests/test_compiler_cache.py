# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.compiler_cache: where the program, the folder and the cap come from, the environment a build
gets (launchers only where the caller set none, the checkout as the base folder, a stats log per run, the cache
switched off), and reading ccache's per-run counters. No ccache runs: the program is a file made here."""

from pathlib import Path

import pytest

from coney_tools import compiler_cache, workspace_fetch
from coney_tools.compiler_cache import CompilerCache, Stats, environment, find, parse_stats, write_config


@pytest.fixture
def workspace(tmp_path: Path) -> Path:
    """A workspace with its main checkout at repos/coney (no git: main_checkout falls back to the checkout)."""
    checkout = tmp_path / "repos" / "coney"
    checkout.mkdir(parents=True)
    (checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
    return tmp_path


def _checkout(workspace: Path) -> Path:
    return workspace / "repos" / "coney"


def _config(workspace: Path, text: str) -> None:
    (_checkout(workspace) / "coney.local.toml").write_text(text, encoding="utf-8")


def test_the_defaults_are_the_workspace_cache_folder_and_25_gb(workspace: Path) -> None:
    cache = find(_checkout(workspace), which=lambda name: None)
    assert cache.directory == (workspace / "cache" / "ccache").resolve()
    assert cache.size_gb == compiler_cache.DEFAULT_SIZE_GB == 25
    assert not cache.on and "ccache not found" in cache.missing


def test_the_program_is_the_configured_one_then_the_fetched_copy_then_path(workspace: Path) -> None:
    on_path = workspace / "bin" / "ccache"
    on_path.parent.mkdir()
    on_path.write_text("", encoding="utf-8")
    assert find(_checkout(workspace), which=lambda name: str(on_path)).program == on_path
    fetched = workspace_fetch.tool_destination("ccache", workspace) / compiler_cache._executable()
    fetched.parent.mkdir(parents=True)
    fetched.write_text("", encoding="utf-8")
    assert find(_checkout(workspace), which=lambda name: str(on_path)).program == fetched.resolve()
    mine = workspace / "mine" / "ccache.exe"
    mine.parent.mkdir()
    mine.write_text("", encoding="utf-8")
    _config(workspace, f"ccache_program = '{mine.as_posix()}'\n")
    assert find(_checkout(workspace), which=lambda name: str(on_path)).program == mine.resolve()


def test_a_configured_program_that_is_missing_turns_the_cache_off_and_says_why(workspace: Path) -> None:
    _config(workspace, "ccache_program = 'nowhere/ccache.exe'\n")
    cache = find(_checkout(workspace), which=lambda name: "/usr/bin/ccache")
    assert not cache.on and "ccache_program" in cache.missing and "is not a file" in cache.missing


def test_the_folder_and_the_cap_come_from_the_config_and_a_cap_of_0_is_off(workspace: Path) -> None:
    _config(workspace, "ccache_dir = '../../elsewhere'\nccache_size_gb = 0\n")
    cache = find(_checkout(workspace), which=lambda name: "/usr/bin/ccache")
    assert cache.directory == (workspace / "elsewhere").resolve()
    assert cache.size_gb == 0 and cache.program is not None and not cache.on


def test_a_broken_config_leaves_the_cache_on_its_defaults(workspace: Path) -> None:
    _config(workspace, "no_such_key = 1\n")
    cache = find(_checkout(workspace), which=lambda name: "/usr/bin/ccache")
    assert cache.on and cache.size_gb == 25


def test_the_environment_routes_both_compilers_through_ccache_with_the_checkout_as_base(tmp_path: Path) -> None:
    cache = CompilerCache(tmp_path / "bin" / "ccache.exe", tmp_path / "cache", 25)
    env = environment(cache, tmp_path / "wt", tmp_path / "run.ccache-stats", {"PATH": "x"})
    assert env == {
        "CMAKE_C_COMPILER_LAUNCHER": (tmp_path / "bin" / "ccache.exe").as_posix(),
        "CMAKE_CXX_COMPILER_LAUNCHER": (tmp_path / "bin" / "ccache.exe").as_posix(),
        "CCACHE_DIR": str(tmp_path / "cache"),
        "CCACHE_MAXSIZE": "25G",
        "CCACHE_BASEDIR": str((tmp_path / "wt").resolve()),
        "CCACHE_NOHASHDIR": "1",
        "CCACHE_STATSLOG": str(tmp_path / "run.ccache-stats"),
    }


def test_a_launcher_the_caller_set_is_kept_whatever_its_case(tmp_path: Path) -> None:
    cache = CompilerCache(tmp_path / "ccache", tmp_path / "cache", 25)
    env = environment(cache, tmp_path, tmp_path / "s", {"cmake_cxx_compiler_launcher": "sccache"})
    assert "CMAKE_CXX_COMPILER_LAUNCHER" not in env and "CMAKE_C_COMPILER_LAUNCHER" in env


def test_an_off_cache_only_disables_ccache_for_folders_configured_with_it(tmp_path: Path) -> None:
    for cache in (
        CompilerCache(None, tmp_path, 25, "ccache not found"),
        CompilerCache(tmp_path / "ccache", tmp_path, 0),
    ):
        assert environment(cache, tmp_path, tmp_path / "s", {}) == {"CCACHE_DISABLE": "1"}


PRINT_LOG_STATS = """\
autoconf_test\t0
called_for_link\t3
cache_miss\t12
could_not_use_precompiled_header\t265
direct_cache_hit\t970
preprocessed_cache_hit\t10
files_in_cache\t5000
local_storage_hit\t980
"""


def test_the_counters_give_hits_misses_and_uncacheable_calls() -> None:
    stats = parse_stats(PRINT_LOG_STATS)
    assert stats == Stats(hits=980, misses=12, uncacheable=268)
    assert stats.line() == "compiler cache: 980 hits, 12 misses (98.8 %), 268 uncacheable"


def test_no_counters_is_no_stats_and_no_calls_is_a_rate_of_0() -> None:
    assert parse_stats("") is None and parse_stats("ccache: error: something\n") is None
    assert Stats(0, 0, 0).rate == 0.0 and Stats(0, 0, 0).line() == "compiler cache: 0 hits, 0 misses (0.0 %)"


def test_the_cap_is_written_into_the_cache_folder_for_a_ccache_started_by_hand(tmp_path: Path) -> None:
    folder = tmp_path / "cache"
    write_config(CompilerCache(tmp_path / "ccache", folder, 0))
    assert not folder.exists()  # off: nothing written
    write_config(CompilerCache(tmp_path / "ccache", folder, 25))
    assert (folder / "ccache.conf").read_text(encoding="utf-8") == "max_size = 25G\n"
    (folder / "ccache.conf").write_text("compression_level = 3\nmax_size = 5.0G\n", encoding="utf-8")
    write_config(CompilerCache(tmp_path / "ccache", folder, 10))
    assert (folder / "ccache.conf").read_text(encoding="utf-8") == "compression_level = 3\nmax_size = 10G\n"
