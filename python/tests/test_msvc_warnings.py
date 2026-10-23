# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that Coney's MSVC warning set is the one the other compilers' is (cmake/coney_target.cmake).

GCC and Clang refuse an unhandled enumerator in a `switch` over an enum (-Wswitch, part of -Wall, an error in CI).
MSVC reports the same thing only as C4062, which is off by default, so a missing case passed the local build on
Windows and failed in CI. The flag `/w44062` turns it on; measured with `cl /W4` (19.44) on 2026-10-23, a switch over
a three-value enum with two cases warns only with the flag. It is added for MSVC itself, not clang-cl (which CMake
also treats as MSVC): clang-cl's `/W4` already maps to `-Wall -Wextra`, which has `-Wswitch`, and whether it takes
`/w44062` was not measured (no clang-cl here), so it does not get a flag it might reject under `/WX`.
"""

import json
from pathlib import Path

import pytest
from conventions_support import REPO

TARGET = REPO / "cmake" / "coney_target.cmake"
PRESETS = REPO / "CMakePresets.json"
FLAG = "/w44062"
#: The folders of the checkout whose sources are Coney's own targets (`add_subdirectory` in CMakeLists.txt). A
#: dependency can sit inside the checkout too (FetchContent puts it in `build/<preset>/_deps/` when the shared folder
#: `CONEY_DEPS_SOURCES` is off), and `coney_target_defaults` does not apply to it.
COMING_FROM = ("src", "tests")


def msvc_branch() -> list[str]:
    """The lines of `coney_target_defaults`'s MSVC branch, `if(MSVC)` up to its `else()`."""
    lines = TARGET.read_text(encoding="utf-8").splitlines()
    start = next(index for index, line in enumerate(lines) if line.strip() == "if(MSVC)")
    end = next(index for index in range(start, len(lines)) if lines[index].strip() == "else()")
    return lines[start + 1 : end]


def conditions_around_flag() -> list[str]:
    """The `if(...)` conditions that enclose the line that adds the flag, innermost last."""
    open_conditions: list[str] = []
    for line in msvc_branch():
        text = line.strip()
        if text.startswith("if("):
            open_conditions.append(text)
        elif text.startswith("endif("):
            open_conditions.pop()
        elif FLAG in text and not text.startswith("#"):
            return open_conditions
    raise AssertionError(f"{FLAG} is not added in the MSVC branch")


def test_the_flag_is_added_for_cl_exe_only() -> None:
    assert conditions_around_flag() == ['if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")']


def test_the_flag_does_not_depend_on_the_preset() -> None:
    # Every preset builds Coney's targets through this function: dev, playtest, release, ci, ci-release, asan. The flag
    # sits under no option (warnings as errors, sanitizers, debug tools), so a local dev build shows it too.
    conditions = " ".join(conditions_around_flag())
    for option in ("CONEY_WARNINGS_AS_ERRORS", "CONEY_SANITIZERS", "CONEY_DEBUG_TOOLS", "CMAKE_BUILD_TYPE"):
        assert option not in conditions
    presets = json.loads(PRESETS.read_text(encoding="utf-8"))["configurePresets"]
    assert {"dev", "playtest", "release", "ci", "ci-release", "asan"} <= {preset["name"] for preset in presets}


def test_the_ci_presets_make_warnings_errors() -> None:
    # The presets a unit's local check builds with: /WX shows before CI does.
    presets = {p["name"]: p for p in json.loads(PRESETS.read_text(encoding="utf-8"))["configurePresets"]}
    for name in ("ci", "ci-release"):
        assert presets[name]["cacheVariables"]["CONEY_WARNINGS_AS_ERRORS"] == "ON", name


def coney_cl_commands(commands: list[dict[str, str]], root: Path) -> list[dict[str, str]]:
    """The entries of a compile database that compile Coney's own sources with cl.exe: a file in `src/` or `tests/` of
    the checkout `root`, not a dependency that happens to be unpacked under it."""
    ours = []
    for entry in commands:
        path = Path(entry["file"]).resolve()
        in_coney = path.is_relative_to(root) and path.relative_to(root).parts[0] in COMING_FROM
        if in_coney and "cl.exe" in entry["command"].lower():
            ours.append(entry)
    return ours


def test_a_dependency_unpacked_inside_the_checkout_is_not_coney_s_own(tmp_path: Path) -> None:
    # With the shared folder off, FetchContent unpacks a dependency into build/<preset>/_deps/<name>-src/ of the
    # checkout, and cl.exe compiles it without the flag. It must not make the check below fail.
    commands = [
        {"file": str(tmp_path / "src" / "core" / "a.cpp"), "command": "cl.exe /W4 /w44062 /c a.cpp"},
        {"file": str(tmp_path / "tests" / "b.cpp"), "command": "cl.exe /W4 /w44062 /c b.cpp"},
        {"file": str(tmp_path / "build" / "ci" / "_deps" / "enet-src" / "c.c"), "command": "cl.exe /W4 /c c.c"},
        {
            "file": str(tmp_path / "build" / "ci" / "_deps" / "librw-src" / "src" / "d.cpp"),
            "command": "cl.exe /c d.cpp",
        },
        {"file": str(tmp_path / "src" / "e.cpp"), "command": "g++ -Wall -c e.cpp"},
        {"file": str(tmp_path.parent / "cache" / "deps" / "enet" / "f.c"), "command": "cl.exe /c f.c"},
    ]
    ours = coney_cl_commands(commands, tmp_path.resolve())
    assert [Path(entry["file"]).name for entry in ours] == ["a.cpp", "b.cpp"]


def test_the_flag_is_on_every_coney_command_of_a_real_msvc_configure() -> None:
    # A CMake text check cannot see what CMake did with it: read a compile database from a build folder, when there is
    # one from an MSVC configure (`coney-tools build --preset ci --configure-only`; the folder is not committed).
    found = 0
    for database in sorted((REPO / "build").glob("*/compile_commands.json")):
        commands = json.loads(database.read_text(encoding="utf-8"))
        ours = coney_cl_commands(commands, REPO.resolve())
        for entry in ours:
            assert FLAG in entry["command"].split(), f"{entry['file']} is compiled without {FLAG} ({database})"
        found += len(ours)
    if not found:
        pytest.skip("no compile database from an MSVC configure in build/")
