# SPDX-License-Identifier: GPL-3.0-or-later
"""The build stays one that the shared compiler cache can serve to every worktree
(docs/guides/building.md#compiler-cache).

ccache rewrites the checkout's absolute paths in a command line's file options (`CCACHE_BASEDIR`), but not inside a
compile definition, so a definition that holds one (`CONEY_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets"`) makes the same
source compile with a different command line in each worktree, and every file it is set on misses the cache: on
2026-10-20 that was all 413 test objects of a second worktree. Such a definition belongs on the one source file that
needs it (`set_source_files_properties`), never on a target. Every preset also inherits `cacheable` (no precompiled
headers, which ccache cannot use).
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

#: A target-wide definition (`target_compile_definitions` or `add_compile_definitions`) with a checkout path in it.
_PATH_DEFINITION = re.compile(
    r"(?:target_compile_definitions|add_compile_definitions)\s*\([^)]*\$\{(?:PROJECT|CMAKE_CURRENT|CMAKE)_(?:SOURCE|BINARY)_DIR\}",
    re.DOTALL,
)


def _cmake_files() -> list[Path]:
    """Coney's own CMake files: the top one, cmake/ and every CMakeLists.txt under src/ and tests/."""
    found = [ROOT / "CMakeLists.txt", *(ROOT / "cmake").rglob("*.cmake"), *(ROOT / "cmake").rglob("CMakeLists.txt")]
    for folder in ("src", "tests"):
        found += (ROOT / folder).rglob("CMakeLists.txt")
    return sorted(set(found))


def test_no_target_gets_a_definition_holding_a_checkout_path() -> None:
    offenders = [
        f"{path.relative_to(ROOT).as_posix()}: {match.group(0).splitlines()[0]}"
        for path in _cmake_files()
        for match in _PATH_DEFINITION.finditer(path.read_text(encoding="utf-8"))
    ]
    assert offenders == [], "put the definition on the one source that needs it (set_source_files_properties)"


def test_the_pattern_catches_a_target_wide_path_and_leaves_a_source_property_alone() -> None:
    assert _PATH_DEFINITION.search('target_compile_definitions(t PRIVATE X="${PROJECT_SOURCE_DIR}/assets")')
    assert _PATH_DEFINITION.search('add_compile_definitions(\n    X="${CMAKE_CURRENT_BINARY_DIR}")')
    assert not _PATH_DEFINITION.search('set_source_files_properties(a.cpp PROPERTIES COMPILE_DEFINITIONS "X=${A}")')
    assert not _PATH_DEFINITION.search("target_compile_definitions(t PRIVATE HAVE_AV_CONFIG_H)")


def test_every_configure_preset_is_cacheable() -> None:
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    by_name = {preset["name"]: preset for preset in presets}

    def inherited(name: str) -> set[str]:
        parents = by_name[name].get("inherits", [])
        parents = [parents] if isinstance(parents, str) else parents
        return {name}.union(*(inherited(parent) for parent in parents))

    visible = [name for name, preset in by_name.items() if not preset.get("hidden")]
    assert visible and all("cacheable" in inherited(name) for name in visible), visible
