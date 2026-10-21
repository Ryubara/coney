# SPDX-License-Identifier: GPL-3.0-or-later
"""The clang-tidy result cache (tidy_cache.py): what goes into a file's key, and the shared store."""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from coney_tools import tidy_cache
from coney_tools.tidy_cache import KeyMaker, TidyCache


def write(root: Path, name: str, text: str) -> None:
    """Write a file below `root`, making its folders."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def entry(root: Path, source: str, extra: str = "") -> dict[str, str]:
    """A cl.exe-style compile database entry for `source` in the checkout at `root`, as CMake writes it."""
    native = str(root / source).replace("/", "\\")
    include = str(root / "src").replace("/", "\\")
    return {
        "directory": (root / "build" / "dev").as_posix(),
        "command": f"C:\\VS\\cl.exe /nologo /TP -I{include} {extra}/W4 @src\\{Path(source).name}.obj.modmap "
        f"/Fo{Path(source).name}.obj -c {native}",
        "file": native,
    }


def checkout(root: Path) -> Path:
    """A checkout: a source that includes a header that includes another; a third header nobody includes."""
    write(root, "src/ai/brain.h", '#pragma once\n#include "ai/mind.h"\n#include <vector>\n')
    write(root, "src/ai/mind.h", "#pragma once\nint mind();\n")
    write(root, "src/ai/brain.cpp", '#include "ai/brain.h"\n#include "local.h"\nint b;\n')
    write(root, "src/ai/local.h", "int local;\n")
    write(root, "src/ai/other.h", "int other;\n")
    return root


def key_of(root: Path, source: str = "src/ai/brain.cpp", setup: str = "setup", extra: str = "") -> str:
    """The cache key of `source` in the checkout at `root`."""
    return KeyMaker(root, setup).key(source, entry(root, source, extra))


def test_the_key_is_stable(tmp_path: Path) -> None:
    root = checkout(tmp_path / "a")
    assert key_of(root) == key_of(root)


@pytest.mark.parametrize("name", ["src/ai/brain.cpp", "src/ai/brain.h", "src/ai/mind.h", "src/ai/local.h"])
def test_an_edit_to_the_source_or_any_header_it_reaches_changes_the_key(tmp_path: Path, name: str) -> None:
    root = checkout(tmp_path / "a")
    before = key_of(root)
    write(root, name, (root / name).read_text(encoding="utf-8") + "// edit\n")
    assert key_of(root) != before


def test_a_header_it_does_not_reach_leaves_the_key_alone(tmp_path: Path) -> None:
    root = checkout(tmp_path / "a")
    before = key_of(root)
    write(root, "src/ai/other.h", "int changed;\n")
    assert key_of(root) == before


def test_a_new_include_counts_the_new_header(tmp_path: Path) -> None:
    root = checkout(tmp_path / "a")
    write(root, "src/ai/brain.cpp", '#include "ai/brain.h"\n#include "ai/other.h"\n')
    before = key_of(root)
    write(root, "src/ai/other.h", "int changed;\n")
    assert key_of(root) != before


def test_the_same_sources_in_two_worktrees_share_a_key(tmp_path: Path) -> None:
    """Every worktree is a checkout of nearly the same files at another path: the path must not split the cache."""
    assert key_of(checkout(tmp_path / "one")) == key_of(checkout(tmp_path / "Two-other"))


def test_the_compile_flags_and_the_setup_are_in_the_key(tmp_path: Path) -> None:
    root = checkout(tmp_path / "a")
    assert key_of(root, extra="/DNEW ") != key_of(root)
    assert key_of(root, setup="clang-tidy 20") != key_of(root)


def test_a_module_map_that_appears_after_a_build_does_not_change_the_key(tmp_path: Path) -> None:
    """The check step drops a `@...modmap` that is not there yet; the key must not depend on whether a build ran."""
    root = checkout(tmp_path / "a")
    before = key_of(root)
    write(root, "build/dev/src/brain.cpp.obj.modmap", "x\n")
    assert key_of(root) == before


def test_a_header_including_itself_through_a_cycle_ends(tmp_path: Path) -> None:
    root = checkout(tmp_path / "a")
    write(root, "src/ai/mind.h", '#pragma once\n#include "ai/brain.h"\n')
    assert key_of(root)


def test_setup_text_holds_the_version_the_checks_and_the_config_files(tmp_path: Path) -> None:
    root = checkout(tmp_path / "a")
    write(root, ".clang-tidy", "Checks: '-*,bugprone-*'\n")
    write(root, "cmake/deps.cmake", "set(SDL 3.2)\n")
    base = tidy_cache.setup_text(root, "LLVM version 19.1.5", "-*,bugprone-*", ["--extra-arg=-Wall"])
    assert "19.1.5" in base and "bugprone" in base and "-Wall" in base
    write(root, "cmake/deps.cmake", "set(SDL 3.4)\n")
    assert tidy_cache.setup_text(root, "LLVM version 19.1.5", "-*,bugprone-*", ["--extra-arg=-Wall"]) != base
    write(root, ".clang-tidy", "Checks: '-*,bugprone-*'\nHeaderFilterRegex: '.*'\n")
    assert tidy_cache.setup_text(root, "LLVM version 19.1.5", "-*,bugprone-*", ["--extra-arg=-Wall"]) != base


def test_the_store_remembers_clean_files_only_by_key(tmp_path: Path) -> None:
    cache = TidyCache(tmp_path / "cache")
    assert not cache.hit("ab" * 32)
    cache.store("ab" * 32, "src/ai/brain.cpp")
    assert cache.hit("ab" * 32)
    assert not cache.hit("cd" * 32)


def test_old_entries_are_pruned(tmp_path: Path) -> None:
    cache = TidyCache(tmp_path / "cache")
    cache.store("ab" * 32, "src/a.cpp")
    cache.store("cd" * 32, "src/b.cpp")
    old = cache.path("ab" * 32)
    stamp = old.stat().st_mtime - tidy_cache.MAX_AGE_SECONDS - 60
    os.utime(old, (stamp, stamp))
    assert cache.prune() == 1
    assert not cache.hit("ab" * 32) and cache.hit("cd" * 32)


def test_a_hit_refreshes_the_entry_so_it_is_not_pruned(tmp_path: Path) -> None:
    cache = TidyCache(tmp_path / "cache")
    cache.store("ab" * 32, "src/a.cpp")
    old = cache.path("ab" * 32)
    stamp = old.stat().st_mtime - tidy_cache.MAX_AGE_SECONDS - 60
    os.utime(old, (stamp, stamp))
    assert cache.hit("ab" * 32)
    assert cache.prune() == 0


def test_the_default_folder_is_in_the_workspace_cache_unless_configured(tmp_path: Path) -> None:
    main = tmp_path / "repos" / "coney"
    write(main, "coney.local.example.toml", "")
    assert tidy_cache.default_cache(main).directory == (tmp_path / "cache" / "clang-tidy").resolve()
    write(main, "coney.local.toml", 'tidy_cache_dir = "../../elsewhere"\n')
    assert tidy_cache.default_cache(main).directory == (tmp_path / "elsewhere").resolve()
