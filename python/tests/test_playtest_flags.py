# SPDX-License-Identifier: GPL-3.0-or-later
"""The `playtest` build compiles and links as the release build does (docs/guides/building.md#playtest-flags).

`playtest` is CMake's RelWithDebInfo, which MSVC compiles with `/Ob1` (only functions marked inline are inlined) and
links incrementally with every call through a jump thunk, so a bench or a play test measured a build a quarter to two
fifths slower than the player's. `cmake/coney_inlining.cmake` gives that configuration the release build's `/Ob2` and
a non-incremental, folded link; these tests run the module under `cmake -P` with MSVC's default flags and read the
result, and check that the top-level file includes it before any target exists.
"""

import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("engine")

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "cmake" / "coney_inlining.cmake"

#: CMake's default flags for MSVC (CMake 3.28 and later, with CMP0141 NEW the debug-information flag is separate).
_DEFAULTS = {
    "CMAKE_C_FLAGS_RELWITHDEBINFO": "/O2 /Ob1 /DNDEBUG",
    "CMAKE_CXX_FLAGS_RELWITHDEBINFO": "/O2 /Ob1 /DNDEBUG",
    "CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO": "/debug /INCREMENTAL",
    "CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO": "/debug /INCREMENTAL",
    "CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO": "/debug /INCREMENTAL",
    "CMAKE_CXX_FLAGS_RELEASE": "/O2 /Ob2 /DNDEBUG",
    "CMAKE_EXE_LINKER_FLAGS_RELEASE": "/INCREMENTAL:NO",
    "CMAKE_CXX_FLAGS_DEBUG": "/Od /Ob0 /RTC1",
}


def _cmake() -> str | None:
    """cmake on PATH, else the copy Visual Studio ships (the one `coney-tools build` uses on Windows)."""
    found = shutil.which("cmake")
    if found:
        return found
    inside = "Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    copies = sorted(str(path) for folder in Path("C:/").glob("Program Files*") for path in folder.glob(inside))
    return copies[-1] if copies else None


def _run_module(tmp_path: Path, *, msvc: bool, flags: dict[str, str]) -> dict[str, str]:
    """Runs the module under `cmake -P` with the given variables set and returns each one's value afterwards."""
    cmake = _cmake()
    if cmake is None:
        pytest.skip("no cmake")
    lines = ["set(MSVC TRUE)" if msvc else "unset(MSVC)"]
    lines += [f'set({name} "{value}")' for name, value in flags.items()]
    lines.append(f'include("{MODULE.as_posix()}")')
    lines.append('set(_out "{}")')
    lines += [f'string(JSON _out SET "${{_out}}" {name} "\\"${{{name}}}\\"")' for name in flags]
    lines.append('message(STATUS "RESULT=${_out}")')
    script = tmp_path / "run.cmake"
    script.write_text("\n".join(lines) + "\n", encoding="utf-8")
    done = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True, check=True)
    text = done.stdout + done.stderr
    match = re.search(r"RESULT=(\{.*\})", text, re.DOTALL)
    assert match, text
    result: dict[str, str] = json.loads(match.group(1))
    return result


def test_relwithdebinfo_inlines_as_release_does(tmp_path: Path) -> None:
    after = _run_module(tmp_path, msvc=True, flags=_DEFAULTS)
    for name in ("CMAKE_C_FLAGS_RELWITHDEBINFO", "CMAKE_CXX_FLAGS_RELWITHDEBINFO"):
        assert after[name].split() == ["/O2", "/Ob2", "/DNDEBUG"], name


def test_relwithdebinfo_links_without_incremental_and_folds(tmp_path: Path) -> None:
    after = _run_module(tmp_path, msvc=True, flags=_DEFAULTS)
    for kind in ("EXE", "SHARED", "MODULE"):
        words = after[f"CMAKE_{kind}_LINKER_FLAGS_RELWITHDEBINFO"].split()
        # Debug information stays; the incremental switch (which would match `/INCREMENTAL` by prefix) is off once.
        assert words.count("/debug") == 1, kind
        assert words.count("/INCREMENTAL:NO") == 1 and "/INCREMENTAL" not in words, kind
        assert "/OPT:REF" in words and "/OPT:ICF" in words, kind


def test_running_it_twice_changes_nothing(tmp_path: Path) -> None:
    once = _run_module(tmp_path, msvc=True, flags=_DEFAULTS)
    twice = _run_module(tmp_path, msvc=True, flags=once)
    assert once == twice


def test_the_other_configurations_and_other_compilers_are_untouched(tmp_path: Path) -> None:
    after = _run_module(tmp_path, msvc=True, flags=_DEFAULTS)
    for name in ("CMAKE_CXX_FLAGS_RELEASE", "CMAKE_EXE_LINKER_FLAGS_RELEASE", "CMAKE_CXX_FLAGS_DEBUG"):
        assert after[name] == _DEFAULTS[name], name
    # GCC and Clang have no /Ob and no /INCREMENTAL; their -O2 already inlines across a call.
    assert _run_module(tmp_path, msvc=False, flags=_DEFAULTS) == _DEFAULTS


def test_the_top_level_file_includes_it_before_any_target() -> None:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "include(coney_inlining)" in text
    assert text.index("include(coney_inlining)") < text.index("include(deps)")


def test_the_release_presets_set_no_flags_of_their_own() -> None:
    # The release build is not changed (issue 1552): its flags come from CMake's Release defaults, which already
    # inline with /Ob2, so no preset overrides a flag variable.
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    for preset in presets:
        assert not [k for k in preset.get("cacheVariables", {}) if "FLAGS" in k], preset["name"]
