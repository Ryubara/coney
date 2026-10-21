# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the shared dependency sources (docs/guides/building.md#shared-deps): coney_tools.shared_deps names the
base folder, and cmake/deps.cmake picks the version's folder under it on every configure from the pins alone, and
locks it before it fetches."""

import glob
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from coney_tools import shared_deps

ROOT = Path(__file__).resolve().parents[2]
DEPS = ROOT / "cmake" / "deps.cmake"


@pytest.fixture
def checkout(tmp_path: Path) -> Path:
    """A main checkout at workspace/repos/coney with a deps.cmake (no git: it is its own main checkout)."""
    root = tmp_path / "repos" / "coney"
    (root / "cmake").mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "cmake" / "deps.cmake").write_text("FetchContent_Declare(SDL3 ...)\n", encoding="utf-8")
    return root


def test_the_folder_is_in_the_workspace_cache_unless_deps_dir_moves_it(checkout: Path, tmp_path: Path) -> None:
    assert shared_deps.folder(checkout) == tmp_path.resolve() / "cache" / "deps"
    (checkout / "coney.local.toml").write_text("deps_dir = '../../elsewhere'\n", encoding="utf-8")
    assert shared_deps.folder(checkout) == (tmp_path / "elsewhere").resolve()


def test_the_environment_names_the_folder_unless_the_caller_set_it(checkout: Path) -> None:
    assert shared_deps.environment(checkout, {"PATH": "x"}) == {
        "CONEY_DEPS_SOURCES": shared_deps.folder(checkout).as_posix()
    }
    assert shared_deps.environment(checkout, {"coney_deps_sources": ""}) == {}  # off, by the caller's choice
    (checkout / "cmake" / "deps.cmake").unlink()
    assert shared_deps.environment(checkout, {}) == {}


def test_deps_cmake_takes_the_folders_lock_before_it_decides_what_to_fetch() -> None:
    text = DEPS.read_text(encoding="utf-8")
    lock = text.index('file(LOCK "${_coney_deps_dir}" DIRECTORY GUARD FILE')
    assert lock < text.index("set(_coney_${_dep}_where SOURCE_DIR")


def test_the_version_folder_is_worked_out_on_every_configure() -> None:
    # Kept in the cache, a folder would keep its first version and fetch new pins into the old version's folder.
    text = DEPS.read_text(encoding="utf-8")
    assert re.search(r"set\(_coney_deps_dir [^)]*CACHE", text) is None
    assert re.search(r"set\(_coney_deps_hash [^)]*CACHE", text) is None


def _cmake() -> str | None:
    """cmake on PATH, else the copy Visual Studio ships (the one `coney-tools build` uses on Windows)."""
    found = shutil.which("cmake")
    if found:
        return found
    studio = "C:/Program Files*/Microsoft Visual Studio/*/*"
    copies = sorted(glob.glob(studio + "/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"))
    return copies[-1] if copies else None


def _version_folder(cmake: str, deps_file: Path, work: Path) -> str:
    """Run deps.cmake's own version lines (from its pins up to the folder) as a script on `deps_file`."""
    text = DEPS.read_text(encoding="utf-8")
    start = text.index('file(STRINGS "${CMAKE_CURRENT_LIST_FILE}" _coney_pins')
    end = text.index("endif()", text.index('set(_coney_deps_dir "")')) + len("endif()")
    snippet = text[start:end].replace("${CMAKE_CURRENT_LIST_FILE}", deps_file.as_posix())
    script = work / "version.cmake"
    script.write_text(
        f'set(CONEY_DEPS_SOURCES "{work.as_posix()}/shared")\n{snippet}\nmessage("${{_coney_deps_dir}}")\n',
        encoding="utf-8",
    )
    done = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True, check=True)
    return done.stderr.strip()


def test_a_pin_change_moves_to_a_new_folder_and_a_comment_does_not(tmp_path: Path) -> None:
    cmake = _cmake()
    if cmake is None:
        pytest.skip("no cmake")
    deps_file = tmp_path / "deps.cmake"
    original = DEPS.read_text(encoding="utf-8")
    deps_file.write_text(original, encoding="utf-8")
    first = _version_folder(cmake, deps_file, tmp_path)
    assert first.startswith(f"{tmp_path.as_posix()}/shared/") and len(first.rsplit("/", 1)[1]) == 16
    deps_file.write_text("# a new comment\n" + original.replace("SDL3 SDL3_SHARED", "SDL3 SDL3_SHARED "), "utf-8")
    assert _version_folder(cmake, deps_file, tmp_path) == first
    tag = re.search(r"GIT_TAG ([0-9a-f]{40})", original)
    assert tag is not None
    deps_file.write_text(original.replace(tag.group(1), "0" * 40), encoding="utf-8")
    assert _version_folder(cmake, deps_file, tmp_path) != first
