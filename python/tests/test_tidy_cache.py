# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the clang-tidy result cache (`ci tidy-run --cache`): made-up compile databases, files and outcomes."""

import json
import subprocess
import sys
from pathlib import Path

import pytest

from coney_tools.ci_tidy import Finding
from coney_tools.tidy_cache import Entry, TidyCache, depfile_arguments, load_commands, read_depfile


def write(root: Path, name: str, text: str) -> Path:
    """Write a file below `root`, making its folders."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


@pytest.fixture
def checkout(tmp_path: Path) -> Path:
    """A checkout with one source, its header and a compile database in build/ci naming the source absolutely."""
    root = tmp_path / "coney"
    write(root, "src/a.cpp", '#include "a.h"\n')
    write(root, "src/a.h", "// a\n")
    database = [
        {"directory": str(root / "build" / "ci"), "command": "clang++ -c src/a.cpp", "file": str(root / "src/a.cpp")}
    ]
    write(root, "build/ci/compile_commands.json", json.dumps(database))
    return root


def depfile(root: Path, folder: Path, *names: str) -> Path:
    """A dependency file naming `names` (below `root`) as clang writes one."""
    lines = " \\\n  ".join(str(root / name) for name in names)
    return write(folder, "deps.d", f"tidy: \\\n  {lines}\n")


FINDING = Finding("src/a.h", 1, 1, "error", "bugprone-x", "bad")


def test_the_dependency_arguments_are_joined_so_the_tooling_cannot_strip_them() -> None:
    # clang tooling drops every word starting with -M (and the word after -MT) from a GCC/Clang-style command, so a
    # separate `-Xclang -MT` pair would leave `-Xclang tidy` behind; `-Xclang=<word>` starts with neither
    words = depfile_arguments(Path("d/deps.d"))
    assert words == [
        f"-Xclang={word}" for word in ("-dependency-file", str(Path("d/deps.d")), "-MT", "tidy", "-sys-header-deps")
    ]
    assert not any(word.startswith("-M") for word in words)


def pinned_clang_tidy() -> str | None:
    """The clang-tidy of CI's major version that `ci tidy-local` would run on this machine, or None without one."""
    from coney_tools.conventions import load_conventions
    from coney_tools.local_tidy import TidyUnavailable, find_program

    root = Path(__file__).parents[2]
    try:
        return find_program(root, load_conventions(root).ci.tidy.llvm_major)
    except TidyUnavailable:
        return None


@pytest.mark.parametrize(
    "compiler",
    [
        "/usr/bin/clang++-19 -std=c++20",
        "clang++ -std=c++20",
        # clang-cl mode reads MSVC's headers, which only a Windows machine has.
        pytest.param(
            "cl.exe /nologo /std:c++latest /showIncludes",
            marks=pytest.mark.skipif(sys.platform != "win32", reason="clang-cl mode needs MSVC's headers"),
        ),
    ],
)
def test_the_real_clang_tidy_writes_the_dependency_file_for_either_driver(tmp_path: Path, compiler: str) -> None:
    program = pinned_clang_tidy()
    if program is None:
        pytest.skip("no clang-tidy of CI's major version on this machine")
    root = tmp_path / "coney"
    source = write(root, "src/a.cpp", '#include "a.h"\n#include <cstddef>\nint f() { return g(); }\n')
    write(root, "src/a.h", "inline int g() { return 1; }\n")
    folder = root / "build" / "ci"
    output = "/Foa.obj" if compiler.startswith("cl.exe") else "-o a.o -MD -MT a.o -MF a.o.d"
    command = f"{compiler} -c {source.as_posix()} {output}"
    database = [{"directory": folder.as_posix(), "command": command, "file": source.as_posix()}]
    write(root, "build/ci/compile_commands.json", json.dumps(database))
    deps = tmp_path / "deps.d"
    arguments = [f"--extra-arg={word}" for word in depfile_arguments(deps)]
    done = subprocess.run(
        [program, "-p", str(folder), "--quiet", "--checks=-*,misc-unused-parameters", *arguments, str(source)],
        capture_output=True,
        text=True,
        check=False,
        timeout=300,
    )
    assert done.returncode == 0, done.stdout + done.stderr
    names = [Path(path).name for path in read_depfile(deps.read_text(encoding="utf-8"))]
    assert names[:2] == ["a.cpp", "a.h"]
    assert "cstddef" in names  # system headers are named too


def test_a_dependency_file_is_read_with_continuations_escaped_spaces_and_no_repeats() -> None:
    text = "tidy: /r/src/a.cpp \\\n  /r/src/a.h /usr/include/c++/x\\ y.h \\\n  /r/src/a.h\n"
    assert read_depfile(text) == ["/r/src/a.cpp", "/r/src/a.h", "/usr/include/c++/x y.h"]


def test_windows_paths_in_a_dependency_file_keep_their_backslashes() -> None:
    text = "tidy: \\\n  C:\\r\\src\\a.cpp \\\n  C:\\Program\\ Files\\x.h\n"
    assert read_depfile(text) == ["C:\\r\\src\\a.cpp", "C:\\Program Files\\x.h"]


def test_the_compile_database_is_read_by_source_relative_to_the_checkout(checkout: Path) -> None:
    assert list(load_commands(checkout / "build" / "ci", checkout)) == ["src/a.cpp"]
    assert load_commands(checkout / "nowhere", checkout) == {}


def test_an_outcome_is_kept_and_given_back_while_its_files_are_unchanged(checkout: Path, tmp_path: Path) -> None:
    cache = TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x")
    assert cache.get("src/a.cpp") is None
    cache.put("src/a.cpp", Entry(1, (FINDING,)), depfile(checkout, tmp_path, "src/a.cpp", "src/a.h"))
    again = TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x")
    assert again.get("src/a.cpp") == Entry(1, (FINDING,))
    assert (again.hits, again.misses) == (1, 0)
    # the entry names the files inside the checkout relative to it, so another checkout reads it the same
    (entry,) = (tmp_path / "cache").glob("*.json")
    assert set(json.loads(entry.read_text(encoding="utf-8"))["files"]) == {"src/a.cpp", "src/a.h"}


def test_a_changed_header_is_a_miss(checkout: Path, tmp_path: Path) -> None:
    cache = TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x")
    cache.put("src/a.cpp", Entry(0, ()), depfile(checkout, tmp_path, "src/a.cpp", "src/a.h"))
    write(checkout, "src/a.h", "// a, changed\n")
    again = TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x")
    assert again.get("src/a.cpp") is None and again.misses == 1


@pytest.mark.parametrize(("version", "config"), [("19.1.2", "Checks: x"), ("19.1.1", "Checks: y")])
def test_another_clang_tidy_or_configuration_is_a_miss(
    checkout: Path, tmp_path: Path, version: str, config: str
) -> None:
    cache = TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x")
    cache.put("src/a.cpp", Entry(0, ()), depfile(checkout, tmp_path, "src/a.cpp"))
    assert (
        TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", version, config).get("src/a.cpp")
        is None
    )


def test_another_compile_command_is_a_miss(checkout: Path, tmp_path: Path) -> None:
    cache = TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x")
    cache.put("src/a.cpp", Entry(0, ()), depfile(checkout, tmp_path, "src/a.cpp"))
    database = checkout / "build" / "ci" / "compile_commands.json"
    database.write_text(database.read_text(encoding="utf-8").replace("-c", "-DNEW -c"), encoding="utf-8")
    assert (
        TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x").get("src/a.cpp")
        is None
    )


def test_nothing_is_kept_without_a_dependency_file_or_a_compile_command(checkout: Path, tmp_path: Path) -> None:
    cache = TidyCache.open(tmp_path / "cache", checkout, checkout / "build" / "ci", "19.1.1", "Checks: x")
    cache.put("src/a.cpp", Entry(0, ()), tmp_path / "missing.d")
    cache.put("src/other.cpp", Entry(0, ()), depfile(checkout, tmp_path, "src/a.cpp"))
    assert list((tmp_path / "cache").iterdir()) == []
