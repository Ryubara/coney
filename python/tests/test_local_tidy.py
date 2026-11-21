# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for clang-tidy on this machine (`task check`'s clang-tidy check, `ci tidy-local`): made-up trees, diffs and
clang-tidy runs."""

import json
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import local_tidy
from coney_tools.conventions import load_conventions
from coney_tools.local_tidy import TidyUnavailable, find_program, header_filter, run_local

#: The tests copy and read the repository's .clang-tidy.
pytestmark = pytest.mark.reads("engine")

CONVENTIONS = load_conventions(REPO)
MAJOR = CONVENTIONS.ci.tidy.llvm_major


def write(root: Path, name: str, text: str) -> None:
    """Write a file below `root`, making its folders."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


DIFF = """\
--- a/src/a.cpp
+++ b/src/a.cpp
@@ -1 +1,2 @@
-old
+new
+newer
--- a/src/b.cpp
+++ b/src/b.cpp
@@ -0,0 +1 @@
+added
"""


@pytest.fixture
def tree(tmp_path: Path) -> Path:
    """A checkout with two changed sources, the repository's .clang-tidy and a compile database knowing only a.cpp."""
    write(tmp_path, "src/a.cpp", "new\nnewer\n")
    write(tmp_path, "src/b.cpp", "added\n")
    write(tmp_path, ".clang-tidy", (REPO / ".clang-tidy").read_text(encoding="utf-8"))
    entry = {"directory": str(tmp_path / "build"), "command": "cl.exe /c a.cpp", "file": str(tmp_path / "src/a.cpp")}
    write(tmp_path, "build/compile_commands.json", json.dumps([entry]))
    return tmp_path


class Tidy:
    """A clang-tidy stand-in answering per source, recording each command."""

    def __init__(self, outputs: dict[str, tuple[int, str]]) -> None:
        self.outputs = outputs
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], cwd: Path) -> tuple[int, str]:
        self.commands.append(list(command))
        return self.outputs.get(command[-1], (0, ""))


def test_the_header_filter_takes_either_slash_on_windows_only() -> None:
    config = "Checks: x\nHeaderFilterRegex: '.*/(src|tests)/.*'\n"
    widened = header_filter(config, windows=True)
    assert widened == r".*[/\\](src|tests)[/\\].*"
    assert header_filter(config, windows=False) is None
    assert header_filter("Checks: x\n", windows=True) is None


def test_the_widened_filter_matches_the_paths_clang_tidy_sees_on_windows() -> None:
    import re

    widened = header_filter((REPO / ".clang-tidy").read_text(encoding="utf-8"), windows=True)
    assert widened is not None
    assert re.fullmatch(widened, r"C:\w\coney\src\core/pad.h")
    assert re.fullmatch(widened, "/home/runner/coney/tests/a.h")
    assert not re.fullmatch(widened, r"C:\w\build\_deps\sdl3-src\include\SDL3\SDL.h")


def test_findings_on_the_changed_lines_fail_and_a_source_the_database_lacks_is_not_run(tree: Path) -> None:
    tidy = Tidy(
        {
            "src/a.cpp": (
                1,
                f"{tree}\\src\\a.cpp:2:5: error: bad [bugprone-x,-warnings-as-errors]\n"
                f"{tree}\\src\\a.cpp:9:5: error: off the change [bugprone-y,-warnings-as-errors]\n",
            )
        }
    )
    result = run_local(
        tree, "main", CONVENTIONS, tidy, program="clang-tidy", build_dir=tree / "build", jobs=1, diff=DIFF, windows=True
    )
    assert not result.ok
    assert [command[-1] for command in tidy.commands] == ["src/a.cpp"]
    assert result.lines()[0] == "src/a.cpp:2:5 [bugprone-x] bad"
    assert len(result.lines()) == 2 and "src/b.cpp: not in the compile database" in result.lines()[1]
    # Windows: the header filter is widened and the line filter is applied here, not by clang-tidy
    command = tidy.commands[0]
    assert any(word.startswith("--header-filter=") for word in command)
    assert not any(word.startswith("--line-filter=") for word in command)


def test_elsewhere_clang_tidy_gets_the_line_filter_and_keeps_its_own_header_filter(tree: Path) -> None:
    tidy = Tidy({})
    result = run_local(
        tree,
        "main",
        CONVENTIONS,
        tidy,
        program="clang-tidy",
        build_dir=tree / "build",
        jobs=1,
        diff=DIFF,
        windows=False,
    )
    command = tidy.commands[0]
    assert any(word.startswith("--line-filter=") for word in command)
    assert not any(word.startswith("--header-filter=") for word in command)
    assert result.result.findings == []


def test_a_build_setup_change_checks_only_the_changes_own_sources_and_says_so(tree: Path) -> None:
    diff = DIFF + "--- a/CMakeLists.txt\n+++ b/CMakeLists.txt\n@@ -1 +1 @@\n-a\n+b\n"
    result = run_local(
        tree, "main", CONVENTIONS, Tidy({}), program="t", build_dir=tree / "build", jobs=1, diff=diff, windows=True
    )
    assert not result.plan.everything
    assert any("CI checks every file" in note for note in result.notes)


def configured(root: Path, program: Path) -> None:
    """A coney.local.toml at `root` naming `program` as clang_tidy_program."""
    write(root, "coney.local.example.toml", "")
    write(root, "coney.local.toml", f'clang_tidy_program = "{program.as_posix()}"\n')


def test_the_configured_program_is_used_when_its_major_version_is_cis(tmp_path: Path) -> None:
    program = tmp_path / "llvm" / "clang-tidy.exe"
    write(tmp_path, "llvm/clang-tidy.exe", "")
    configured(tmp_path, program)
    found = find_program(tmp_path, MAJOR, which=lambda name: None, version_of=lambda path: f"{MAJOR}.1.1")
    assert Path(found) == program


def test_another_major_version_cannot_run(tmp_path: Path) -> None:
    write(tmp_path, "llvm/clang-tidy.exe", "")
    configured(tmp_path, tmp_path / "llvm" / "clang-tidy.exe")
    with pytest.raises(TidyUnavailable, match=f"CI runs {MAJOR}"):
        find_program(tmp_path, MAJOR, which=lambda name: None, version_of=lambda path: "21.1.0")


def test_a_configured_program_that_is_not_there_cannot_run(tmp_path: Path) -> None:
    configured(tmp_path, tmp_path / "missing.exe")
    with pytest.raises(TidyUnavailable, match="not a file"):
        find_program(tmp_path, MAJOR, which=lambda name: None, version_of=lambda path: None)


def test_without_any_program_it_says_how_to_get_one(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(local_tidy, "workspace_root", lambda root: tmp_path / "workspace")
    with pytest.raises(TidyUnavailable, match="not installed"):
        find_program(tmp_path, MAJOR, which=lambda name: None, version_of=lambda path: None)


def test_the_versioned_name_on_path_comes_before_the_plain_one(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(local_tidy, "workspace_root", lambda root: tmp_path / "workspace")
    on_path = {f"clang-tidy-{MAJOR}": "/usr/bin/clang-tidy-19", "clang-tidy": "/usr/bin/clang-tidy"}
    found = find_program(tmp_path, MAJOR, which=on_path.get, version_of=lambda path: f"{MAJOR}.1.1")
    assert found == "/usr/bin/clang-tidy-19"


def test_no_compile_database_cannot_run(tmp_path: Path) -> None:
    with pytest.raises(TidyUnavailable, match="no compile database"):
        local_tidy.compile_database(tmp_path, "ci")


def test_on_windows_a_check_that_follows_the_standard_library_is_advice_not_a_failure(tree: Path) -> None:
    advisory = CONVENTIONS.ci.tidy.local_advisory[0]
    output = f"{tree}/src/a.cpp:1:1: error: may throw [{advisory},-warnings-as-errors]\n"
    database = tree / "build" / "compile_commands.json"
    entries = json.loads(database.read_text(encoding="utf-8"))
    entries.append({**entries[0], "file": str(tree / "src/b.cpp")})
    database.write_text(json.dumps(entries), encoding="utf-8")

    def run(windows: bool) -> local_tidy.LocalResult:
        tidy = Tidy({"src/a.cpp": (1, output)})
        return run_local(
            tree, "main", CONVENTIONS, tidy, program="t", build_dir=tree / "build", jobs=1, diff=DIFF, windows=windows
        )

    here = run(windows=True)
    assert here.ok and here.result.findings == [] and [item.check for item in here.advisory] == [advisory]
    assert here.lines() == [f"advice, not a failure (may differ from CI): src/a.cpp:1:1 [{advisory}] may throw"]
    # on Linux the library is CI's, so the same finding fails
    assert not run(windows=False).ok
