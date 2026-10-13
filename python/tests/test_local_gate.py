# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools check changed`: file selection (against CI's tidy-files.sh), the clang-tidy command and the
step logic, on synthetic git repositories and compile databases. No game data, no real clang-tidy."""

import json
import os
import re
import shutil
import subprocess
import tomllib
from pathlib import Path

import pytest
import yaml
from conventions_support import write_checkout

from coney_tools import check_cli, local_gate
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions

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


def bash_major(bash: str) -> int:
    """The major version a bash reports (0 when it will not say); tidy-files.sh needs 4 for `declare -A`, `mapfile`."""
    try:
        done = subprocess.run([bash, "--version"], capture_output=True, text=True, check=False)
    except OSError:
        return 0
    found = re.search(r"version (\d+)", done.stdout)
    return int(found.group(1)) if found else 0


def find_git_bash() -> str | None:
    """Git's own bash (never the WSL launcher in System32) or the system bash on Linux and macOS, if it is bash 4+.

    macOS ships bash 3.2, which lacks what tidy-files.sh uses; the test then skips instead of failing.
    """
    candidates: list[str] = []
    git_exe = shutil.which("git")
    if git_exe is not None:
        for relative in ("bin/bash.exe", "usr/bin/bash.exe"):
            candidate = Path(git_exe).resolve().parent.parent / relative
            if candidate.is_file():
                candidates.append(str(candidate))
    found = shutil.which("bash")
    if found and "system32" not in found.lower():
        candidates.append(found)
    return next((bash for bash in candidates if bash_major(bash) >= 4), None)


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


def edit_and_commit(repo: Path, edit: list[str]) -> None:
    """Append a comment to each named file and commit the edit."""
    for name in edit:
        write(repo, name, Path(repo / name).read_text(encoding="utf-8") + "// edited\n")
    commit_all(repo, "edit")


def script_selection(repo: Path) -> list[str]:
    """What .github/scripts/tidy-files.sh prints for the committed change since `main`."""
    bash = find_git_bash()
    assert bash is not None
    done = subprocess.run(
        [bash, SCRIPT.as_posix(), "main"], cwd=repo, check=True, capture_output=True, text=True, env=os.environ
    )
    return [line for line in done.stdout.splitlines() if line]


@pytest.mark.skipif(find_git_bash() is None, reason="no bash 4+ to run tidy-files.sh")
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
    edit_and_commit(repo, edit)
    from_script = script_selection(repo)
    assert from_script != ["ALL"]
    assert local_gate.tidy_sources(repo, local_gate.changed_files(repo, "main")) == from_script


@pytest.mark.skipif(find_git_bash() is None, reason="no bash 4+ to run tidy-files.sh")
def test_selection_agrees_through_a_test_header(repo: Path) -> None:
    # Added on main before the branch point: tests/support/helper.h includes walk.h, and a test includes the helper.
    git(repo, "switch", "-q", "main")
    write(repo, "tests/support/helper.h", '#include "ai/walk.h"\n')
    write(repo, "tests/ai/helper_test.cpp", '#include "support/helper.h"\n')
    commit_all(repo, "test helper")
    git(repo, "switch", "-q", "work")
    git(repo, "merge", "-q", "--ff-only", "main")
    edit_and_commit(repo, ["src/ai/brain.h"])
    from_script = script_selection(repo)
    assert "tests/ai/helper_test.cpp" in from_script
    assert local_gate.tidy_sources(repo, local_gate.changed_files(repo, "main")) == from_script


@pytest.mark.skipif(find_git_bash() is None, reason="no bash 4+ to run tidy-files.sh")
def test_an_untracked_includer_is_the_one_deliberate_difference(repo: Path) -> None:
    edit_and_commit(repo, ["src/ai/brain.h"])
    write(repo, "tests/ai/new_test.cpp", '#include "ai/brain.h"\n')
    # CI only ever sees committed files; the local run also checks a file you have not committed yet.
    from_script = script_selection(repo)
    assert "tests/ai/new_test.cpp" not in from_script
    assert local_gate.tidy_sources(repo, local_gate.changed_files(repo, "main")) == sorted(
        [*from_script, "tests/ai/new_test.cpp"]
    )


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
    with pytest.raises(ConfigError, match="build --configure-only"):
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
    monkeypatch.setattr(local_gate, "HOMEBREW_PREFIXES", ())
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
        (["mkdocs_hooks.py"], True),
        (["requirements-docs.txt"], True),
        (["README.md"], True),
        (["src/other.cpp"], False),
        (["python/src/coney_tools/cli.py"], False),
        (["src/README.md"], False),
        # The docs build imports the progress tracker (and its config errors) from python/src.
        (["python/src/coney_tools/progress.py"], True),
        (["python/src/coney_tools/progress_render.py"], True),
        (["python/src/coney_tools/progress_research.py"], True),
        (["python/src/coney_tools/progress_cli.py"], True),
        (["python/src/coney_tools/config.py"], True),
        (["python/tests/test_progress.py"], False),
    ],
)
def test_docs_build_runs_only_for_docs(changed: list[str], wanted: bool) -> None:
    assert check_cli._docs_changed(changed, load_conventions(REPO).paths) is wanted


def test_python_step_skips_without_python_changes() -> None:
    assert check_cli._step_python(REPO, ["docs/page.md"]).skipped


def test_mypy_command_reads_the_projects_strict_config() -> None:
    """mypy looks for its config in the working directory, and the gate runs from the repository root, where there is
    none: without `--config-file` the `[tool.mypy] strict = true` of python/pyproject.toml is silently ignored."""
    command = check_cli.MYPY_COMMAND
    assert command[command.index("--config-file") + 1] == "python/pyproject.toml"
    config = tomllib.loads((REPO / "python" / "pyproject.toml").read_text(encoding="utf-8"))
    assert config["tool"]["mypy"]["strict"] is True


def test_ci_and_docs_run_the_same_mypy_command_as_the_gate() -> None:
    """CI's python job and the guides that name the command agree with `check changed`, so no place checks less."""
    wanted = " ".join(check_cli.MYPY_COMMAND)
    workflow = (REPO / ".github" / "workflows" / "python.yml").read_text(encoding="utf-8")
    assert f"- run: {wanted}\n" in workflow.replace("\r\n", "\n")
    # building.md shows the whole command; coney-tools.md's table shows it after `uv run --project python`.
    short = " ".join(check_cli.MYPY_COMMAND[4:])
    for guide, words in (("building.md", wanted), ("coney-tools.md", f"`{short}`")):
        text = (REPO / "docs" / "guides" / guide).read_text(encoding="utf-8")
        assert words in text, guide
    # conventions.md only names the flag, but it must be the one the command uses.
    conventions = (REPO / "docs" / "guides" / "conventions.md").read_text(encoding="utf-8")
    assert "`--config-file python/pyproject.toml`" in conventions


@pytest.mark.parametrize(
    ("changed", "wanted"),
    [
        (["python/src/coney_tools/cli.py"], True),
        ([".claude/hooks/block_raw_build.py"], True),
        ([".claude/settings.json"], True),
        (["docs/page.md"], False),
        (["src/other.cpp"], False),
        (["claude/x.py"], False),
    ],
)
def test_pytest_runs_for_python_and_the_claude_hooks(changed: list[str], wanted: bool) -> None:
    """pytest also tests the .claude/ hooks, so a change there runs it too."""
    assert check_cli._python_changed(changed) is wanted


def test_long_output_is_cut_with_a_count() -> None:
    lines = [f"line {number}" for number in range(local_gate.MAX_OUTPUT_LINES + 5)]
    cut = check_cli._tail(lines)
    assert len(cut) == local_gate.MAX_OUTPUT_LINES + 1 and "5 more lines" in cut[-1]


def test_repo_step_reports_missing_pointer_files(tmp_path: Path) -> None:
    result = check_cli._step_repo(write_checkout(tmp_path))
    assert not result.ok and result.output


def test_repo_step_passes_for_this_checkout() -> None:
    assert check_cli._step_repo(REPO).ok


def test_repo_step_without_the_conventions_file_is_a_config_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match=r"conventions\.yml"):
        check_cli._step_repo(tmp_path)


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


def test_tidy_step_shows_the_raw_tail_when_nothing_looks_like_a_diagnostic(
    repo: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    crash = "\n".join(f"stack frame {number}" for number in range(40))
    build = fake_tidy_setup(monkeypatch, repo, {"src/other.cpp": (139, f"PLEASE submit a bug report\n{crash}\n")})
    result = check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None)
    assert not result.ok
    assert any("stack frame 39" in line for line in result.output)
    assert not any("stack frame 9" in line for line in result.output)  # only the tail, not all 40 lines


def test_tidy_step_warns_when_the_database_is_gccs(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = fake_tidy_setup(monkeypatch, repo, {})
    database = json.loads((build / "compile_commands.json").read_text())
    for entry in database:
        entry["command"] = entry["command"].replace("C:\\VS\\cl.exe", "/usr/bin/g++-13")
    (build / "compile_commands.json").write_text(json.dumps(database), encoding="utf-8")
    result = check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None)
    assert result.ok
    hint = next(line for line in result.output if "GCC compile database" in line)
    # The hint is a command an agent may run: the build tool configures the second folder (a raw `cmake --preset` is
    # refused by the build guard hook).
    assert "coney-tools build" in hint and "--build-dir build/tidy" in hint
    assert "--env CC=clang-19" in hint and "--env CXX=clang++-19" in hint and "--configure-only" in hint
    assert "cmake --preset" not in hint


@pytest.mark.parametrize(
    ("command", "expected"),
    [
        ("/usr/bin/g++-13 -c a.cpp", True),
        ("/usr/bin/gcc -c a.c", True),
        ("x86_64-linux-gnu-g++-13 -c a.cpp", True),
        ("ccache /usr/bin/g++ -c a.cpp", True),
        ("/usr/bin/clang++-19 -c a.cpp", False),
        ("/usr/bin/c++ -c a.cpp", False),
        ("C:\\VS\\cl.exe /c a.cpp", False),
    ],
)
def test_gcc_driver_detection(command: str, expected: bool) -> None:
    assert local_gate.uses_gcc_driver({"command": command}) is expected


def test_a_compiler_launcher_does_not_hide_the_compiler() -> None:
    assert local_gate.uses_clang_cl_driver({"arguments": ["sccache", "clang-cl", "/c", "a.cpp"]})


def test_the_launcher_is_stripped_from_the_copied_database(tmp_path: Path) -> None:
    build = tmp_path / "build"
    build.mkdir()
    entries = [
        {
            "directory": build.as_posix(),
            "command": "ccache /usr/bin/clang++-19 -c a.cpp",
            "file": str(tmp_path / "a.cpp"),
        },
        {
            "directory": build.as_posix(),
            "arguments": ["/usr/bin/sccache", "clang++-19", "-c", "b.cpp"],
            "file": str(tmp_path / "b.cpp"),
        },
        {"directory": build.as_posix(), "command": "/usr/bin/clang++-19 -c c.cpp", "file": str(tmp_path / "c.cpp")},
    ]
    (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
    out = tmp_path / "copy"
    out.mkdir()
    local_gate.without_missing_response_files(local_gate.load_compile_db(build), out)
    by_file = {Path(entry["file"]).name: entry for entry in json.loads((out / "compile_commands.json").read_text())}
    assert by_file["a.cpp"]["command"] == "/usr/bin/clang++-19 -c a.cpp"
    assert by_file["b.cpp"]["arguments"] == ["clang++-19", "-c", "b.cpp"]
    assert by_file["c.cpp"]["command"] == "/usr/bin/clang++-19 -c c.cpp"


def test_a_quoted_module_map_path_with_spaces_is_one_argument(tmp_path: Path) -> None:
    build = tmp_path / "build"
    build.mkdir()
    (build / "has space").mkdir()
    (build / "has space" / "kept.modmap").write_text("", encoding="utf-8")
    command = 'clang++ -DNAME="two  words" @"gone dir/a.modmap" @"has space/kept.modmap" "@gone dir/b.modmap" -c x.cpp'
    entry = {"directory": build.as_posix(), "command": command, "file": str(tmp_path / "x.cpp")}
    (build / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    out = tmp_path / "copy"
    out.mkdir()
    local_gate.without_missing_response_files(local_gate.load_compile_db(build), out)
    (rewritten,) = json.loads((out / "compile_commands.json").read_text())
    assert rewritten["command"] == 'clang++ -DNAME="two  words" @"has space/kept.modmap" -c x.cpp'


def test_clang_tidy_is_looked_for_in_homebrew_llvm_19_before_an_unversioned_one(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    for formula in ("llvm", "llvm@19"):
        tool = tmp_path / "opt" / formula / "bin" / "clang-tidy"
        tool.parent.mkdir(parents=True)
        tool.write_text("", encoding="utf-8")
    monkeypatch.setattr(local_gate, "HOMEBREW_PREFIXES", (str(tmp_path),))
    monkeypatch.setattr(shutil, "which", lambda _name: None)
    monkeypatch.delenv("PROGRAMFILES(X86)", raising=False)
    # The unversioned formula reports 20 and llvm@19 reports 19: the version CI runs wins.
    monkeypatch.setattr(local_gate, "_tidy_major", lambda tool: 19 if "llvm@19" in tool.as_posix() else 20)
    path, major = local_gate.find_clang_tidy()
    assert path == tmp_path / "opt" / "llvm@19" / "bin" / "clang-tidy" and major == 19
    # Without llvm@19 the unversioned one is the fallback, and the caller notes its version.
    (tmp_path / "opt" / "llvm@19" / "bin" / "clang-tidy").unlink()
    path, major = local_gate.find_clang_tidy()
    assert path == tmp_path / "opt" / "llvm" / "bin" / "clang-tidy" and major == 20


# --- pre-commit in groups, and the exit code ---------------------------------------------------------------------


def test_file_groups_stay_within_the_budget_and_keep_every_file_in_order() -> None:
    names = [f"src/some/where/file_{number:04}.cpp" for number in range(500)]
    groups = check_cli._chunks(names, budget=1000)
    assert [name for group in groups for name in group] == names
    assert len(groups) > 1
    assert all(sum(len(name) + 1 for name in group) <= 1000 for group in groups)
    assert check_cli._chunks([]) == []


def test_pre_commit_runs_a_big_change_in_several_commands(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    names = [f"docs/{'long_name_' * 8}{number:04}.md" for number in range(600)]
    calls: list[list[str]] = []

    def fake_run(command: list[str], cwd: Path) -> tuple[int, str]:
        calls.append(command)
        return 0, ""

    monkeypatch.setattr(local_gate, "run_command", fake_run)
    result = check_cli._step_pre_commit(repo, names)
    assert result.ok and len(calls) > 1
    assert all(sum(len(part) + 1 for part in command) < 32000 for command in calls)
    assert [name for command in calls for name in command[command.index("--files") + 1 :]] == names


def test_pre_commit_reports_a_failure_from_any_group(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    names = [f"docs/{'long_name_' * 8}{number:04}.md" for number in range(600)]
    seen = {"count": 0}

    def fake_run(command: list[str], cwd: Path) -> tuple[int, str]:
        seen["count"] += 1
        return (1, "trim trailing whitespace...Failed") if seen["count"] == 2 else (0, "")

    monkeypatch.setattr(local_gate, "run_command", fake_run)
    result = check_cli._step_pre_commit(repo, names)
    assert not result.ok and any("Failed" in line for line in result.output)


def test_run_changed_exits_1_when_a_step_fails_and_0_when_all_pass(
    repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    write(repo, "coney.local.example.toml", "# marks the checkout root\n")
    write(repo, "docs/page.md", "# changed\n")
    monkeypatch.chdir(repo)
    real_run = local_gate.run_command
    outcome = {"code": 1}

    def fake_run(command: list[str], cwd: Path) -> tuple[int, str]:
        if command[0] == "git":
            return real_run(command, cwd)
        return outcome["code"], "hook failed\n"

    monkeypatch.setattr(local_gate, "run_command", fake_run)
    assert check_cli.run_changed("main", "build/dev", ["pre-commit"], None, 1, None) == 1
    assert "FAIL pre-commit" in capsys.readouterr().out
    outcome["code"] = 0
    assert check_cli.run_changed("main", "build/dev", ["pre-commit"], None, 1, None) == 0
    assert "ok   pre-commit" in capsys.readouterr().out


# --- the gate on every OS ---------------------------------------------------------------------------------------


def test_the_python_workflow_runs_the_local_gate_on_every_os_it_tests() -> None:
    """Nobody runs `check changed` on Linux or macOS by hand, so CI's python job does, on its whole OS matrix."""
    workflow = yaml.safe_load((REPO / ".github" / "workflows" / "python.yml").read_text(encoding="utf-8"))
    job = workflow["jobs"]["python"]
    assert job["strategy"]["matrix"]["os"]  # the OSes come from the scope script: all three, or Linux for docs only
    steps = job["steps"]
    checkout = next(step for step in steps if str(step.get("uses", "")).startswith("actions/checkout"))
    # `--base HEAD^1` is the branch a pull request's merge commit joins, so the checkout needs a second commit.
    assert int(checkout.get("with", {}).get("fetch-depth", 1)) >= 2
    gate = [step["run"] for step in steps if "coney-tools check changed" in step.get("run", "")]
    assert len(gate) == 1 and "--base HEAD^1" in gate[0]
    assert "--only pre-commit" in gate[0] and "--only repo" in gate[0]
