# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the shared dependency sources (docs/guides/building.md#shared-deps): coney_tools.shared_deps names the
base folder, and cmake/deps.cmake picks the version's folder under it on every configure from the pins alone, and
locks it before it fetches."""

import hashlib
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
    inside = "Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    copies = sorted(str(path) for folder in Path("C:/").glob("Program Files*") for path in folder.glob(inside))
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


# --- A pin change in a build folder that was configured before it (issue #1147) ---------------------------------------

#: A tiny project around deps.cmake's own loop that picks each dependency's source folder. Its one dependency, alpha,
#: is declared as deps.cmake declares its own (the option for the version's folder in the declaration, a custom
#: download that copies a seed folder into it), and it records where FetchContent put the sources.
_PROJECT = """cmake_minimum_required(VERSION 3.28)
project(pin_change NONE)
include(FetchContent)
set(CONEY_DEPS_SOURCES "${SHARED}" CACHE PATH "")
set(_coney_deps_hash "${HASH}")
set(_coney_deps alpha)
@@PICK@@
FetchContent_Declare(alpha DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory "${SEED}" "${_coney_deps_dir}/alpha"
                           ${_coney_alpha_where})
FetchContent_MakeAvailable(alpha)
file(WRITE "${CMAKE_BINARY_DIR}/where.txt" "${alpha_SOURCE_DIR}")
"""


def _pick_lines() -> str:
    """deps.cmake's own lines from the version folder to the end of the loop that picks each source folder."""
    text = DEPS.read_text(encoding="utf-8")
    start = text.index('set(_coney_deps_dir "")')
    loop = text.index("foreach(_dep IN LISTS _coney_deps)\n    # _coney_<name>_where")
    return text[start : text.index("endforeach()", loop) + len("endforeach()")]


def _generator(cmake: str) -> list[str]:
    """Ninja if there is one (on PATH, else beside Visual Studio's cmake); else cmake's own default, which a project
    with no language needs no compiler for."""
    ninja = shutil.which("ninja")
    if ninja is None:
        beside = Path(cmake).parents[2] / "Ninja" / "ninja.exe"
        ninja = str(beside) if beside.is_file() else None
    return ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={Path(ninja).as_posix()}"] if ninja else []


class _Project:
    """A build folder of the tiny project: configure it under one pin after another, as a worktree's would be."""

    def __init__(self, cmake: str, work: Path) -> None:
        self.cmake = cmake
        self.work = work
        self.shared = work / "shared"
        self.seed = work / "seed"
        self.seed.mkdir()
        (self.seed / "CMakeLists.txt").write_text("# a source folder\n", encoding="utf-8")
        (work / "src").mkdir()
        (work / "src" / "CMakeLists.txt").write_text(_PROJECT.replace("@@PICK@@", _pick_lines()), encoding="utf-8")

    @staticmethod
    def hash(pin: str) -> str:
        return hashlib.sha256(pin.encode()).hexdigest()

    def folder(self, pin: str) -> str:
        """The folder alpha's sources are in under `pin`, as deps.cmake names it."""
        return f"{self.shared.as_posix()}/{self.hash(pin)[:16]}/alpha"

    def fetched(self, pin: str) -> None:
        """Put a complete, stamped copy of alpha where `pin`'s version keeps it, as an earlier configure leaves it."""
        folder = Path(self.folder(pin))
        shutil.copytree(self.seed, folder)
        (folder / ".coney-deps-hash").write_text(self.hash(pin), encoding="utf-8")

    def configure(self, pin: str, *options: str) -> str:
        """Configure the build folder under `pin` and return where FetchContent took alpha's sources from."""
        command = [
            self.cmake, "-S", str(self.work / "src"), "-B", str(self.work / "build"), *_generator(self.cmake),
            f"-DSHARED={self.shared.as_posix()}", f"-DHASH={self.hash(pin)}", f"-DSEED={self.seed.as_posix()}",
            *options,
        ]  # fmt: skip
        done = subprocess.run(command, capture_output=True, text=True, check=False)
        assert done.returncode == 0, done.stdout + done.stderr
        return (self.work / "build" / "where.txt").read_text(encoding="utf-8")

    def cache(self) -> str:
        """The build folder's CMakeCache.txt: what a later configure finds."""
        return (self.work / "build" / "CMakeCache.txt").read_text(encoding="utf-8")


@pytest.fixture
def project(tmp_path: Path) -> _Project:
    cmake = _cmake()
    if cmake is None:
        pytest.skip("no cmake")
    return _Project(cmake, tmp_path)


def test_a_reconfigure_after_a_pin_change_takes_the_new_pins_folder(project: _Project) -> None:
    project.fetched("one")
    project.fetched("two")
    assert project.configure("one") == project.folder("one")
    assert project.configure("two") == project.folder("two")
    assert project.folder("two") in project.cache()  # and the cache no longer names the old folder
    assert project.folder("one") not in project.cache()


def test_a_reconfigure_after_a_pin_change_fetches_into_the_new_folder_not_the_old_one(project: _Project) -> None:
    project.fetched("one")
    assert project.configure("one") == project.folder("one")
    assert project.configure("two") == project.folder("two")  # nothing fetched for "two" yet: it fetches there
    assert (Path(project.folder("two")) / "CMakeLists.txt").is_file()


def test_a_reconfigure_with_the_same_pins_keeps_the_folder(project: _Project) -> None:
    project.fetched("one")
    assert project.configure("one") == project.folder("one")
    assert project.configure("one") == project.folder("one")
    assert project.configure("one") == project.folder("one")


def test_the_users_own_source_folder_wins_before_and_after_a_pin_change(project: _Project) -> None:
    project.fetched("one")
    project.fetched("two")
    project.fetched("three")
    mine = project.work / "mine"
    shutil.copytree(project.seed, mine)
    assert project.configure("one") == project.folder("one")
    assert project.configure("two", f"-DFETCHCONTENT_SOURCE_DIR_ALPHA={mine.as_posix()}") == mine.as_posix()
    assert project.configure("three") == mine.as_posix()  # the cache keeps their choice, which is not ours to replace


def test_the_users_own_source_folder_wins_in_a_fresh_build_folder(project: _Project) -> None:
    project.fetched("one")
    project.fetched("two")
    mine = project.work / "mine"
    shutil.copytree(project.seed, mine)
    assert project.configure("one", f"-DFETCHCONTENT_SOURCE_DIR_ALPHA={mine.as_posix()}") == mine.as_posix()
    assert project.configure("two") == mine.as_posix()


def test_a_build_folder_configured_before_the_fix_moves_on_its_next_configure(project: _Project) -> None:
    project.fetched("one")
    project.fetched("two")
    assert project.configure("one") == project.folder("one")
    # What the cache of a folder configured by the earlier deps.cmake holds: the entry, and no memory of who set it.
    cache = project.work / "build" / "CMakeCache.txt"
    entry = r"(//[^\n]*\n)?CONEY_DEPS_PICKED_ALPHA:INTERNAL=[^\n]*\n"
    cache.write_text(re.sub(entry, "", cache.read_text(encoding="utf-8")), encoding="utf-8")
    assert "CONEY_DEPS_PICKED" not in project.cache()
    assert project.folder("one") in project.cache()
    assert project.configure("two") == project.folder("two")


def test_a_source_folder_outside_the_shared_one_is_the_users_even_with_the_look_of_a_version(project: _Project) -> None:
    project.fetched("one")
    project.fetched("two")
    mine = project.work / "elsewhere" / "0123456789abcdef" / "alpha"
    shutil.copytree(project.seed, mine)
    assert project.configure("one", f"-DFETCHCONTENT_SOURCE_DIR_ALPHA={mine.as_posix()}") == mine.as_posix()
    assert project.configure("two") == mine.as_posix()
