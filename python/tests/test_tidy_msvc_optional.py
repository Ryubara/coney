# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the copy of Microsoft's `<optional>` that lets the local clang-tidy see an unchecked `*optional`
(tidy_msvc_optional.py): its text, the include list it is found by, and, where a real clang-tidy and Microsoft's library
are installed, clang-tidy itself reading planted snippets."""

import json
import re
import sys
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import ci_tidy, tidy_msvc_optional
from coney_tools.conventions import load_conventions
from coney_tools.local_tidy import TidyUnavailable, find_program, run_local
from coney_tools.tidy_msvc_optional import include_dirs, patched_header, prepare

#: The tests copy and read the repository's .clang-tidy.
pytestmark = pytest.mark.reads("engine")

CONVENTIONS = load_conventions(REPO)

#: The part of Microsoft's `<optional>` the copy changes, as the library writes it.
MICROSOFT = """\
template <class _Ty>
class optional : private _SMF_control<_Optional_construct_base<_Ty>, _Ty> {
    using _Mybase = _SMF_control<_Optional_construct_base<_Ty>, _Ty>;

    using _Mybase::operator*;

    constexpr explicit operator bool() const noexcept {
"""

#: What `clang-tidy -v` prints for the compiler of the build.
VERBOSE = """\
clang version 19.1.1
#include "..." search starts here:
#include <...> search starts here:
 C:\\llvm\\lib\\clang\\19\\include
 C:\\Program Files\\MSVC\\14.44\\include
 C:\\Program Files\\MSVC\\14.44\\atlmfc\\include
End of search list.
1 warning generated.
"""


def write(path: Path, text: str) -> Path:
    """Write a file, making its folders; the file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def database(build: Path, command: str = '"C:\\VS\\bin\\cl.exe" /nologo /c a.cpp') -> None:
    """A compile database in `build` with one command."""
    write(build / "compile_commands.json", json.dumps([{"directory": str(build), "command": command, "file": "a.cpp"}]))


def test_the_copy_declares_the_four_overloads_in_optional_and_changes_nothing_else() -> None:
    patched = patched_header(MICROSOFT)
    assert patched is not None
    assert "using _Mybase::operator*;" not in patched
    for qualifier in ("&", "const&", "&&", "const&&"):
        assert f"operator*() {qualifier} noexcept" in patched
    assert patched.replace(tidy_msvc_optional.OVERLOADS, tidy_msvc_optional.USING_LINE) == MICROSOFT


def test_a_header_without_the_line_or_with_it_twice_is_not_copied() -> None:
    assert patched_header("class optional {};\n") is None
    assert patched_header(MICROSOFT + MICROSOFT) is None


def test_the_include_list_is_read_from_clang_tidys_verbose_output() -> None:
    assert include_dirs(VERBOSE) == [
        Path("C:\\llvm\\lib\\clang\\19\\include"),
        Path("C:\\Program Files\\MSVC\\14.44\\include"),
        Path("C:\\Program Files\\MSVC\\14.44\\atlmfc\\include"),
    ]
    assert include_dirs("no list here\n") == []


def test_the_compiler_is_the_first_command_of_the_database(tmp_path: Path) -> None:
    database(tmp_path / "quoted")
    assert tidy_msvc_optional.compiler_of(tmp_path / "quoted") == "C:\\VS\\bin\\cl.exe"
    database(tmp_path / "plain", "cl.exe /c a.cpp")
    assert tidy_msvc_optional.compiler_of(tmp_path / "plain") == "cl.exe"
    write(tmp_path / "args" / "compile_commands.json", json.dumps([{"arguments": ["cl.exe", "/c"], "file": "a.cpp"}]))
    assert tidy_msvc_optional.compiler_of(tmp_path / "args") == "cl.exe"
    assert tidy_msvc_optional.compiler_of(tmp_path / "none") is None


class Probe:
    """A clang-tidy stand-in that prints `output` and records its commands."""

    def __init__(self, output: str) -> None:
        self.output = output
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], cwd: Path) -> tuple[int, str]:
        self.commands.append(list(command))
        return 0, self.output


def toolset(tmp_path: Path, header: str = MICROSOFT) -> str:
    """A library folder holding `<optional>` and the verbose output listing it."""
    include = tmp_path / "msvc" / "include"
    write(include / "optional", header)
    return VERBOSE.replace("C:\\Program Files\\MSVC\\14.44\\include", str(include))


def test_prepare_writes_the_copy_into_the_build_folder_and_puts_it_on_the_include_path(tmp_path: Path) -> None:
    build = tmp_path / "build"
    database(build)
    probe = Probe(toolset(tmp_path))
    model = prepare("clang-tidy", build, probe)
    assert model.notes == []
    assert model.include == build / "tidy-optional"
    assert model.arguments() == (f"--extra-arg=-external:I{build / 'tidy-optional'}",)
    assert (build / "tidy-optional" / "optional").read_text(encoding="utf-8") == patched_header(MICROSOFT)
    # the probe asks the compiler of the build for its include list, with no source of the project
    command = probe.commands[0]
    assert command[0] == "clang-tidy" and "--extra-arg=-v" in command
    entry = json.loads((build / "tidy-optional" / "probe" / "compile_commands.json").read_text(encoding="utf-8"))
    assert entry[0]["command"].startswith('"C:\\VS\\bin\\cl.exe" ')


def test_a_copy_that_is_current_is_not_written_again(tmp_path: Path) -> None:
    build = tmp_path / "build"
    database(build)
    probe = Probe(toolset(tmp_path))
    prepare("clang-tidy", build, probe)
    copy = build / "tidy-optional" / "optional"
    before = copy.stat().st_mtime_ns
    prepare("clang-tidy", build, probe)
    assert copy.stat().st_mtime_ns == before


@pytest.mark.parametrize(
    ("header", "output", "reason"),
    [
        ("class optional {};\n", None, "has no `using _Mybase::operator*;`"),
        (MICROSOFT, "no include list\n", "lists no include folder"),
    ],
)
def test_a_toolset_it_cannot_follow_runs_as_before_and_says_so(
    tmp_path: Path, header: str, output: str | None, reason: str
) -> None:
    build = tmp_path / "build"
    database(build)
    model = prepare("clang-tidy", build, Probe(output if output is not None else toolset(tmp_path, header)))
    assert model.include is None and model.arguments() == ()
    assert len(model.notes) == 1
    assert "does not see an unchecked `*`" in model.notes[0] and reason in model.notes[0]


def test_no_compile_database_means_no_copy_and_a_note(tmp_path: Path) -> None:
    model = prepare("clang-tidy", tmp_path / "build", Probe(""))
    assert model.include is None and "names no compiler" in model.notes[0]


# --- in the local run -----------------------------------------------------------------------------------------------

DIFF = "--- a/src/a.cpp\n+++ b/src/a.cpp\n@@ -1 +1 @@\n-old\n+new\n"


@pytest.fixture
def tree(tmp_path: Path) -> Path:
    """A checkout with one changed source and the compile database of its build."""
    write(tmp_path / "src", "a.cpp", "new\n") if False else write(tmp_path / "src" / "a.cpp", "new\n")
    write(tmp_path / ".clang-tidy", (REPO / ".clang-tidy").read_text(encoding="utf-8"))
    entry = {"directory": str(tmp_path / "build"), "command": "cl.exe /c a.cpp", "file": str(tmp_path / "src/a.cpp")}
    write(tmp_path / "build" / "compile_commands.json", json.dumps([entry]))
    return tmp_path


class Tidy:
    """A clang-tidy stand-in that finds nothing and records its commands."""

    def __init__(self) -> None:
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], cwd: Path) -> tuple[int, str]:
        self.commands.append(list(command))
        return 0, ""


def local(tree: Path, windows: bool, probe: ci_tidy.Runner | None) -> tuple[Tidy, list[str]]:
    """A local run over the tree's one source: the stand-in that ran the source and the run's notes."""
    tidy = Tidy()
    result = run_local(
        tree,
        "main",
        CONVENTIONS,
        tidy,
        program="clang-tidy",
        build_dir=tree / "build",
        jobs=1,
        diff=DIFF,
        windows=windows,
        probe=probe,
    )
    return tidy, result.notes


def test_on_windows_the_local_run_reads_the_copy_first(tree: Path) -> None:
    tidy, notes = local(tree, True, Probe(toolset(tree)))
    assert notes == []
    expected = f"--extra-arg=-external:I{tree / 'build' / 'tidy-optional'}"
    assert expected in tidy.commands[0] and tidy.commands[0].index(expected) < tidy.commands[0].index("src/a.cpp")


def test_without_a_probe_or_off_windows_the_run_is_unchanged(tree: Path) -> None:
    for windows, probe in ((True, None), (False, Probe(toolset(tree)))):
        tidy, notes = local(tree, windows, probe)
        assert not any(word.startswith("--extra-arg") for word in tidy.commands[0])
        assert notes == []


def test_a_toolset_it_cannot_follow_adds_its_note_to_the_run(tree: Path) -> None:
    _, notes = local(tree, True, Probe("no include list\n"))
    assert len(notes) == 1 and "does not see an unchecked `*`" in notes[0]


# --- clang-tidy itself ----------------------------------------------------------------------------------------------

#: Catch2's REQUIRE does not end the path the analysis follows when the condition is false (it reports to the runner and
#: goes on), which is what a stand-in that only calls a function does too; the pull requests of #1620 and #1627 were
#: refused in CI for six and two `*opt` after one.
SNIPPET = """\
#include <optional>

void handle(bool ok);
#define REQUIRE(condition) handle(condition)

struct Rail { int length; };

int bare(std::optional<int> value) { return *value; }
int arrow(std::optional<Rail> value) { return value->length; }
int member(std::optional<int> value) { return value.value(); }
int after_if(std::optional<int> value) {
    if (!value.has_value()) {
        return 0;
    }
    return *value;
}
int after_require(std::optional<int> value) {
    REQUIRE(value.has_value());
    return *value;
}
int suppressed(std::optional<int> value) {
    REQUIRE(value.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    return *value;
}
int moved(std::optional<int> value) { return *std::move(value); }
const int& constant(const std::optional<int>& value) { return *value; }
"""


def line_of(text: str) -> int:
    """The line of SNIPPET holding `text`."""
    return next(number for number, line in enumerate(SNIPPET.splitlines(), 1) if text in line)


#: The line of the access in each function of SNIPPET, by name.
LINES = {
    "bare": line_of("int bare"),
    "arrow": line_of("int arrow"),
    "member": line_of("int member"),
    "after_require": line_of("int after_require") + 2,
    "moved": line_of("int moved"),
    "constant": line_of("const int& constant"),
}


def tidy_program() -> str:
    """The clang-tidy of CI's major version of this machine; skips the test without one, or off Windows (elsewhere the
    library declares operator* in std::optional itself and nothing is copied)."""
    if sys.platform != "win32":
        pytest.skip("Microsoft's standard library is on Windows")
    try:
        return find_program(REPO, CONVENTIONS.ci.tidy.llvm_major)
    except TidyUnavailable as error:
        pytest.skip(str(error))


def probe_build(tmp_path: Path) -> tuple[str, Path, Path]:
    """The clang-tidy of this machine, a build folder whose database has one command, and the source it compiles."""
    program = tidy_program()
    build = tmp_path / "build"
    source = write(tmp_path / "probe.cpp", SNIPPET)
    command = f"cl.exe /nologo /TP /EHsc -std:c++latest -c {source}"
    write(
        build / "compile_commands.json",
        json.dumps([{"directory": str(tmp_path), "command": command, "file": str(source)}]),
    )
    return program, build, source


def findings(tmp_path: Path, model: bool) -> list[int]:
    """The lines of SNIPPET `bugprone-unchecked-optional-access` reports, with or without the copy of `<optional>`."""
    program, build, source = probe_build(tmp_path)
    extra = prepare(program, build, ci_tidy.run_tool).arguments() if model else ()
    code, output = ci_tidy.run_tool(
        [program, "-p", str(build), "--quiet", "--checks=-*,bugprone-unchecked-optional-access", *extra, str(source)],
        tmp_path,
    )
    assert code in (0, 1), output
    assert "tidy-optional" not in output, output  # nothing inside the copy is reported (it is a system header)
    return sorted({int(found.group(1)) for found in re.finditer(r"probe\.cpp:(\d+):\d+: (?:warning|error):", output)})


def test_clang_tidy_without_the_copy_misses_the_unchecked_deref_microsoft_hides(tmp_path: Path) -> None:
    """The reason for the copy, measured: `.value()` and `->` are reported on Microsoft's library, `*` is not."""
    seen = findings(tmp_path, model=False)
    assert LINES["arrow"] in seen and LINES["member"] in seen
    assert LINES["bare"] not in seen and LINES["after_require"] not in seen


def test_clang_tidy_with_the_copy_reports_what_ci_reports_and_not_a_guarded_access(tmp_path: Path) -> None:
    """Every unguarded access is reported, a REQUIRE (#1620) is not a guard, an `if` and a NOLINT are."""
    assert findings(tmp_path, model=True) == sorted(LINES.values())


def test_the_real_toolset_has_the_line_the_copy_changes(tmp_path: Path) -> None:
    """Fails on a machine whose Microsoft library has moved on, so the copy is brought up to date."""
    program, build, _ = probe_build(tmp_path)
    model = prepare(program, build, ci_tidy.run_tool)
    assert model.notes == [] and model.include is not None
