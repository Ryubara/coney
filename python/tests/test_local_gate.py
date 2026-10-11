# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools check changed`: file selection (against CI's tidy-files.sh), the clang-tidy command and the
step logic, on synthetic git repositories and compile databases. No game data, no real clang-tidy."""

import json
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from coney_tools import check_cli, local_gate
from coney_tools.config import ConfigError

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / ".github" / "scripts" / "tidy-files.sh"


def git(root: Path, *args: str) -> str:
    """Run git in `root` with a fixed identity and no signing; returns stdout."""
    done = subprocess.run(
        ["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", "-c", "commit.gpgsign=false", *args],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    return done.stdout


def write(root: Path, name: str, text: str = "x\n") -> None:
    """Write a file below `root`, making its folders."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def commit_all(root: Path, message: str) -> None:
    """Stage everything and commit it."""
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", message)


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A repository on `main` with a small include graph, then a branch `work` ready for changes.

    brain.h is included by brain.cpp and by walk.h; walk.h by walk.cpp; a test includes walk.h; other.cpp is apart.
    """
    root = tmp_path / "repo"
    root.mkdir()
    git(root, "init", "-q", "-b", "main")
    write(root, "src/ai/brain.h", "// brain\n")
    write(root, "src/ai/brain.cpp", '#include "ai/brain.h"\n')
    write(root, "src/ai/walk.h", '#include "ai/brain.h"\n')
    write(root, "src/ai/walk.cpp", '#include "ai/walk.h"\n')
    write(root, "src/other.cpp", "int x;\n")
    write(root, "tests/ai/walk_test.cpp", '#include "ai/walk.h"\n')
    write(root, "docs/page.md", "# page\n")
    commit_all(root, "base")
    git(root, "switch", "-q", "-c", "work")
    return root


def find_git_bash() -> str | None:
    """Git's own bash (never the WSL launcher in System32), or the system bash on Linux and macOS."""
    git_exe = shutil.which("git")
    if git_exe is not None:
        for relative in ("bin/bash.exe", "usr/bin/bash.exe"):
            candidate = Path(git_exe).resolve().parent.parent / relative
            if candidate.is_file():
                return str(candidate)
    found = shutil.which("bash")
    return found if found and "system32" not in found.lower() else None


# --- changed files ---------------------------------------------------------------------------------------------


def test_changed_files_has_committed_staged_unstaged_and_untracked(repo: Path) -> None:
    write(repo, "src/other.cpp", "int y;\n")
    commit_all(repo, "committed")
    write(repo, "docs/page.md", "# changed, unstaged\n")
    write(repo, "src/ai/brain.h", "// staged\n")
    git(repo, "add", "src/ai/brain.h")
    write(repo, "tests/new_test.cpp", "int z;\n")
    assert local_gate.changed_files(repo, "main") == [
        "docs/page.md",
        "src/ai/brain.h",
        "src/other.cpp",
        "tests/new_test.cpp",
    ]


def test_changed_files_leaves_out_deleted_files_and_work_only_on_the_base(repo: Path) -> None:
    (repo / "src/other.cpp").unlink()
    commit_all(repo, "delete")
    git(repo, "switch", "-q", "main")
    write(repo, "docs/later.md", "later on main\n")
    commit_all(repo, "main moved on")
    git(repo, "switch", "-q", "work")
    # The change is measured from where `work` left main, so main's later commit is not part of it.
    assert local_gate.changed_files(repo, "main") == []


def test_unknown_base_is_a_config_error(repo: Path) -> None:
    with pytest.raises(ConfigError):
        local_gate.changed_files(repo, "origin/nowhere")


# --- tidy file selection: mirrors tidy-files.sh ----------------------------------------------------------------


def test_a_changed_cpp_is_checked_itself(repo: Path) -> None:
    assert local_gate.tidy_sources(repo, ["src/other.cpp"]) == ["src/other.cpp"]


def test_a_changed_header_is_checked_through_every_includer_transitively(repo: Path) -> None:
    assert local_gate.tidy_sources(repo, ["src/ai/brain.h"]) == [
        "src/ai/brain.cpp",
        "src/ai/walk.cpp",
        "tests/ai/walk_test.cpp",
    ]


def test_docs_and_python_changes_need_no_tidy(repo: Path) -> None:
    assert local_gate.tidy_sources(repo, ["docs/page.md", "python/src/x.py"]) == []


def test_untracked_includers_are_found(repo: Path) -> None:
    write(repo, "tests/ai/new_test.cpp", '#include "ai/brain.h"\n')
    assert "tests/ai/new_test.cpp" in local_gate.tidy_sources(repo, ["src/ai/brain.h"])


@pytest.mark.parametrize(
    ("name", "full"),
    [
        (".clang-tidy", True),
        ("CMakeLists.txt", True),
        ("CMakePresets.json", True),
        ("cmake/deps.cmake", True),
        (".github/workflows/build.yml", True),
        (".github/scripts/tidy-files.sh", True),
        ("src/CMakeLists.txt", False),
        ("src/other.cpp", False),
        ("docs/guides/building.md", False),
    ],
)
def test_full_pass_triggers(name: str, full: bool) -> None:
    assert local_gate.needs_full_tidy_pass([name]) is full


def test_full_pass_triggers_are_the_scripts() -> None:
    script = SCRIPT.read_text(encoding="utf-8")
    found = re.search(r"grep -Eq '([^']+)'", script)
    assert found is not None
    assert found.group(1) == local_gate.TIDY_ALL_PATTERN


@pytest.mark.skipif(find_git_bash() is None, reason="no bash to run tidy-files.sh")
@pytest.mark.parametrize(
    "edit",
    [
        ["src/other.cpp"],
        ["src/ai/brain.h"],
        ["src/ai/walk.h", "src/other.cpp"],
        ["tests/ai/walk_test.cpp"],
        ["docs/page.md"],
    ],
)
def test_selection_agrees_with_tidy_files_script(repo: Path, edit: list[str]) -> None:
    for name in edit:
        write(repo, name, Path(repo / name).read_text(encoding="utf-8") + "// edited\n")
    commit_all(repo, "edit")
    bash = find_git_bash()
    assert bash is not None
    done = subprocess.run(
        [bash, SCRIPT.as_posix(), "main"], cwd=repo, check=True, capture_output=True, text=True, env=os.environ
    )
    from_script = [line for line in done.stdout.splitlines() if line]
    assert from_script != ["ALL"]
    assert local_gate.tidy_sources(repo, local_gate.changed_files(repo, "main")) == from_script


# --- warnings, checks, compile database ------------------------------------------------------------------------


def test_warning_flags_are_the_gcc_and_clang_build_flags() -> None:
    cmake = (REPO / "cmake" / "coney_target.cmake").read_text(encoding="utf-8")
    assert " ".join(local_gate.WARNING_FLAGS) in cmake


def test_warning_args_use_the_clang_prefix_only_for_cl_style_databases() -> None:
    assert local_gate.warning_args(True) == [f"--extra-arg=/clang:{flag}" for flag in local_gate.WARNING_FLAGS]
    assert local_gate.warning_args(False) == [f"--extra-arg={flag}" for flag in local_gate.WARNING_FLAGS]


@pytest.mark.parametrize(
    ("entry", "expected"),
    [
        ({"command": r"C:\VS\bin\Hostx64\x64\cl.exe /nologo /c a.cpp"}, True),
        ({"command": r"C:\PROGRA~2\MICROS~3\cl.exe /nologo"}, True),
        ({"arguments": ["/usr/bin/clang-cl", "/c", "a.cpp"]}, True),
        ({"command": "/usr/bin/clang++-19 -c a.cpp"}, False),
        ({"arguments": ["g++-13", "-c", "a.cpp"]}, False),
        ({"command": ""}, False),
    ],
)
def test_clang_cl_driver_detection(entry: dict[str, object], expected: bool) -> None:
    assert local_gate.uses_clang_cl_driver(entry) is expected


def test_checks_are_read_and_joined_from_clang_tidy_yaml(tmp_path: Path) -> None:
    (tmp_path / ".clang-tidy").write_text("Checks: >\n  -*,bugprone-*,\n  performance-*\nWarningsAsErrors: '*'\n")
    assert local_gate.load_tidy_checks(tmp_path) == "-*,bugprone-*,performance-*"


def test_the_repositorys_checks_include_the_one_that_failed_in_ci() -> None:
    assert "bugprone-*" in local_gate.load_tidy_checks(REPO).split(",")


def test_missing_clang_tidy_yaml_is_a_config_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        local_gate.load_tidy_checks(tmp_path)


def test_tidy_command_lets_compiler_warnings_through() -> None:
    command = local_gate.clean_tidy_command(Path("ct"), Path("db"), "-*,bugprone-*", ["--extra-arg=-Wall"], "a.cpp")
    assert "--checks=-*,bugprone-*,clang-diagnostic-*" in command
    assert command[-1] == "a.cpp" and "--extra-arg=-Wall" in command


def test_tidy_output_drops_the_tools_own_chatter() -> None:
    raw = (
        "43566 warnings generated.\n"
        "Suppressed 43566 warnings (43566 in non-user code).\n"
        "Use -header-filter=.* to display errors from all non-system headers.\n"
        "a.cpp:3:1: error: unchecked access [bugprone-unchecked-optional-access,-warnings-as-errors]\n"
        "    3 | x->y;\n"
    )
    assert local_gate.tidy_output(raw) == [
        "a.cpp:3:1: error: unchecked access [bugprone-unchecked-optional-access,-warnings-as-errors]",
        "    3 | x->y;",
    ]


def write_database(build: Path, root: Path, names: list[str], modmap: bool = False) -> None:
    """Write a cl.exe-style compile database for `names` (paths under `root`) into `build`."""
    build.mkdir(parents=True, exist_ok=True)
    entries = []
    for name in names:
        extra = f" @{name}.modmap" if modmap else ""
        entries.append(
            {
                "directory": build.as_posix(),
                "command": f"C:\\VS\\cl.exe /nologo{extra} /c {(root / name).as_posix()}",
                "file": str(root / name),
            }
        )
    (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")


def test_compile_database_lookup_and_missing_files(tmp_path: Path) -> None:
    write_database(tmp_path / "build", tmp_path, ["src/a.cpp"])
    database = local_gate.load_compile_db(tmp_path / "build")
    assert local_gate.missing_from_database(tmp_path, ["src/a.cpp", "tests/new_test.cpp"], database) == [
        "tests/new_test.cpp"
    ]


def test_missing_compile_database_says_how_to_make_it(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="cmake --preset dev"):
        local_gate.load_compile_db(tmp_path / "nothing")


def test_unreadable_module_maps_are_dropped_and_present_ones_kept(tmp_path: Path) -> None:
    build = tmp_path / "build"
    write_database(build, tmp_path, ["src/a.cpp", "src/b.cpp"], modmap=True)
    (build / "src").mkdir()
    (build / "src/b.cpp.modmap").write_text("", encoding="utf-8")
    database = local_gate.load_compile_db(build)
    out = tmp_path / "copy"
    out.mkdir()
    local_gate.without_missing_response_files(database, out)
    rewritten = {
        Path(entry["file"]).name: entry["command"] for entry in json.loads((out / "compile_commands.json").read_text())
    }
    assert "@" not in rewritten["a.cpp"]
    assert "@src/b.cpp.modmap" in rewritten["b.cpp"]


def test_rewriting_the_database_keeps_the_command_strings_quoting(tmp_path: Path) -> None:
    build = tmp_path / "build"
    build.mkdir()
    command = r"C:\VS\cl.exe /nologo -DASSETS=\"C:/a b/assets\" @gone.modmap /c x.cpp"
    entry = {"directory": build.as_posix(), "command": command, "file": str(tmp_path / "x.cpp")}
    (build / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    out = tmp_path / "copy"
    out.mkdir()
    local_gate.without_missing_response_files(local_gate.load_compile_db(build), out)
    (rewritten,) = json.loads((out / "compile_commands.json").read_text())
    assert r"-DASSETS=\"C:/a b/assets\"" in rewritten["command"] and "@gone" not in rewritten["command"]


def test_no_clang_tidy_anywhere_is_a_config_error(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", lambda _name: None)
    monkeypatch.delenv("PROGRAMFILES(X86)", raising=False)
    with pytest.raises(ConfigError, match="no clang-tidy"):
        local_gate.find_clang_tidy()


def test_explicit_clang_tidy_must_exist(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        local_gate.find_clang_tidy(str(tmp_path / "nope"))


# --- the steps -------------------------------------------------------------------------------------------------


def fake_tidy_setup(monkeypatch: pytest.MonkeyPatch, repo: Path, outputs: dict[str, tuple[int, str]]) -> Path:
    """Point the tidy step at a fake clang-tidy whose result per source file comes from `outputs`."""
    build = repo / "build" / "dev"
    write_database(build, repo, ["src/other.cpp", "src/ai/brain.cpp", "src/ai/walk.cpp", "tests/ai/walk_test.cpp"])
    write(repo, ".clang-tidy", "Checks: '-*,bugprone-*'\n")
    monkeypatch.setattr(local_gate, "find_clang_tidy", lambda _explicit: (Path("fake-tidy"), local_gate.TIDY_MAJOR))

    real_run = local_gate.run_command

    def fake_run(command: list[str], cwd: Path) -> tuple[int, str]:
        if command[0] == "git":
            return real_run(command, cwd)
        return outputs.get(command[-1], (0, ""))

    monkeypatch.setattr(local_gate, "run_command", fake_run)
    return build


def test_tidy_step_passes_when_every_file_is_clean(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = fake_tidy_setup(monkeypatch, repo, {"src/other.cpp": (0, "5 warnings generated.\n")})
    result = check_cli._step_tidy(repo, ["src/other.cpp"], build, 2, None)
    assert result.ok and "1 files" in result.detail and result.output == ()


def test_tidy_step_fails_and_shows_the_diagnostic(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    message = (
        "a.cpp:9:3: error: missing field 'targets' initializer [clang-diagnostic-missing-designated-field-initializers]"
    )
    build = fake_tidy_setup(
        monkeypatch, repo, {"src/other.cpp": (1, f"1 warning generated.\n{message}\n"), "src/ai/brain.cpp": (0, "")}
    )
    result = check_cli._step_tidy(repo, ["src/other.cpp", "src/ai/brain.cpp"], build, 2, None)
    assert not result.ok and "1 of 2 files" in result.detail
    assert any("missing field 'targets'" in line for line in result.output)


def test_tidy_step_on_a_file_the_database_lacks_is_a_config_error(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = fake_tidy_setup(monkeypatch, repo, {})
    write(repo, "src/new.cpp", "int n;\n")
    with pytest.raises(ConfigError, match="no entry"):
        check_cli._step_tidy(repo, ["src/new.cpp"], build, 2, None)


def test_tidy_step_skips_when_no_cpp_changed(repo: Path) -> None:
    result = check_cli._step_tidy(repo, ["docs/page.md"], repo / "build" / "dev", 2, None)
    assert result.ok and result.skipped


@pytest.mark.parametrize(
    ("changed", "wanted"),
    [
        (["docs/page.md"], True),
        (["mkdocs.yml"], True),
        (["research/missions.yaml"], True),
        (["src/other.cpp"], False),
        (["python/src/coney_tools/cli.py"], False),
    ],
)
def test_docs_build_runs_only_for_docs(changed: list[str], wanted: bool) -> None:
    assert check_cli._docs_changed(changed) is wanted


def test_python_step_skips_without_python_changes() -> None:
    assert check_cli._step_python(REPO, ["docs/page.md"]).skipped


def test_long_output_is_cut_with_a_count() -> None:
    lines = [f"line {number}" for number in range(local_gate.MAX_OUTPUT_LINES + 5)]
    cut = check_cli._tail(lines)
    assert len(cut) == local_gate.MAX_OUTPUT_LINES + 1 and "5 more lines" in cut[-1]


def test_repo_step_reports_missing_pointer_files(tmp_path: Path) -> None:
    result = check_cli._step_repo(tmp_path)
    assert not result.ok and result.output


def test_tidy_step_labels_a_failure_in_a_file_the_change_did_not_edit(
    repo: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build = fake_tidy_setup(
        monkeypatch, repo, {"src/ai/brain.cpp": (1, "src/ai/brain.cpp:1:1: error: x [bugprone-y]\n")}
    )
    result = check_cli._step_tidy(repo, ["src/ai/brain.h"], build, 1, None)
    assert not result.ok
    assert any("not edited" in line for line in result.output)
    assert any(line.startswith("note: a finding in a file you did not edit") for line in result.output)
