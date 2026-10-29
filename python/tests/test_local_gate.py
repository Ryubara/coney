# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools ci run`: the changed-file selection and the step logic, on synthetic git repositories.
No game data."""

import os
import re
import subprocess
import tomllib
from pathlib import Path

import pytest
import yaml
from conventions_support import write_checkout

from coney_tools import build_run, check_cli, local_gate
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions

REPO = Path(__file__).resolve().parents[2]


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


def test_renamed_and_removed_files_follow_git_and_its_similarity(repo: Path) -> None:
    prose = "".join(f"line {n} of a page that is long enough to be recognised\n" for n in range(40))
    write(repo, "docs/moved.md", prose)
    write(repo, "docs/rewritten.md", prose)
    write(repo, "docs/deleted.md", "gone\n")
    commit_all(repo, "pages")
    git(repo, "switch", "-q", "-c", "next")
    (repo / "docs/new").mkdir()
    git(repo, "mv", "docs/moved.md", "docs/new/moved.md")
    git(repo, "mv", "docs/rewritten.md", "docs/new/rewritten.md")
    write(repo, "docs/new/rewritten.md", prose[: len(prose) * 7 // 10] + "other text entirely, written new\n" * 5)
    git(repo, "rm", "-q", "docs/deleted.md")
    git(repo, "add", "-A")
    assert local_gate.renamed_files(repo, "work") == {
        "docs/new/moved.md": "docs/moved.md",
        "docs/new/rewritten.md": "docs/rewritten.md",
    }
    # A higher score keeps only the rename that is mostly the same text; the one rewritten is a deletion and a new file.
    assert local_gate.renamed_files(repo, "work", 90) == {"docs/new/moved.md": "docs/moved.md"}
    assert local_gate.removed_files(repo, "work") == {"docs/moved.md", "docs/rewritten.md", "docs/deleted.md"}
    assert local_gate.removed_files(repo, "work", 90) == {"docs/moved.md", "docs/rewritten.md", "docs/deleted.md"}


def test_unknown_base_is_a_config_error(repo: Path) -> None:
    with pytest.raises(ConfigError):
        local_gate.changed_files(repo, "origin/nowhere")


# --- the steps -------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("changed", "wanted"),
    [
        (["docs/page.md"], True),
        (["mkdocs.yml"], True),
        (["research/missions.yaml"], True),
        (["mkdocs_hooks.py"], True),
        (["requirements-docs.txt"], True),
        # The home page's template and the artwork the site serves from assets/.
        (["overrides/home.html"], True),
        (["assets/art/hero.webp"], True),
        (["assets/sandbox/default.layout"], False),
        (["README.md"], True),
        (["src/other.cpp"], False),
        (["python/src/coney_tools/cli.py"], False),
        (["src/README.md"], False),
        # The docs build imports the progress tracker, the function database (and the config errors) from python/src.
        (["python/src/coney_tools/progress.py"], True),
        (["python/src/coney_tools/progress_render.py"], True),
        (["python/src/coney_tools/progress_research.py"], True),
        (["python/src/coney_tools/progress_cli.py"], True),
        (["python/src/coney_tools/research_render.py"], True),
        (["python/src/coney_tools/research_functions.py"], True),
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
    """CI's python job and the guides that name the command agree with `ci run`, so no place checks less."""
    wanted = " ".join(check_cli.MYPY_COMMAND)
    workflow = (REPO / ".github" / "workflows" / "python.yml").read_text(encoding="utf-8")
    assert f"- run: {wanted}\n" in workflow.replace("\r\n", "\n")
    # building.md shows the whole command; coney-tools-ci.md's table shows it after `uv run --project python`.
    short = " ".join(check_cli.MYPY_COMMAND[4:])
    for guide, words in (("building.md", wanted), ("coney-tools-ci.md", f"`{short}`")):
        text = (REPO / "docs" / "guides" / guide).read_text(encoding="utf-8")
        assert words in text, guide
    # conventions.md only names the flag, but it must be the one the command uses.
    conventions = (REPO / "docs" / "guides" / "conventions.md").read_text(encoding="utf-8")
    assert "`--config-file python/pyproject.toml`" in conventions


def test_ruff_covers_the_hook_scripts() -> None:
    """The hook scripts under .claude/hooks run in every session on a machine, so they are linted like the package."""
    assert check_cli.RUFF_PATHS == ("python", ".claude/hooks")
    assert all((REPO / path).is_dir() for path in check_cli.RUFF_PATHS)
    # Ruff reads the nearest config of each file: the hooks' own file must inherit the package's rules.
    hook_config = tomllib.loads((REPO / ".claude" / "hooks" / "ruff.toml").read_text(encoding="utf-8"))
    assert hook_config == {"extend": "../../python/pyproject.toml"}
    assert (REPO / ".claude" / "hooks" / hook_config["extend"]).resolve() == (
        REPO / "python" / "pyproject.toml"
    ).resolve()


def test_ci_and_the_gate_run_ruff_over_the_same_paths() -> None:
    """CI's python job runs the commands of `ci run` word for word, so the two cannot lint different folders."""
    workflow = (REPO / ".github" / "workflows" / "python.yml").read_text(encoding="utf-8").replace("\r\n", "\n")
    assert len(check_cli.RUFF_COMMANDS) == 2  # check and format --check
    for command in check_cli.RUFF_COMMANDS:
        assert f"- run: {' '.join(command)}\n" in workflow
        assert command[-len(check_cli.RUFF_PATHS) :] == check_cli.RUFF_PATHS
    building = (REPO / "docs" / "guides" / "building.md").read_text(encoding="utf-8").replace("\r\n", "\n")
    for command in check_cli.RUFF_COMMANDS:
        assert " ".join(command) in building, "building.md shows each ruff command"
    # No other ruff line may lint fewer paths (an old `ruff check python` left behind would pass the check above).
    ruff_lines = [line.strip() for line in workflow.splitlines() if "ruff" in line and "- run:" in line]
    assert sorted(ruff_lines) == sorted(f"- run: {' '.join(command)}" for command in check_cli.RUFF_COMMANDS)


def test_pre_commit_runs_ruff_over_the_same_paths_as_the_gate() -> None:
    """pre-commit lints the changed files of exactly the gate's folders."""
    config = yaml.safe_load((REPO / ".pre-commit-config.yaml").read_text(encoding="utf-8"))
    hooks = {
        hook["id"]: hook
        for repo_entry in config["repos"]
        if "ruff-pre-commit" in repo_entry["repo"]
        for hook in repo_entry["hooks"]
    }
    assert set(hooks) == {"ruff-check", "ruff-format"}
    inside = [f"{path}/some_file.py" for path in check_cli.RUFF_PATHS]
    for hook in hooks.values():
        pattern = re.compile(hook["files"])
        assert all(pattern.search(name) for name in inside), hook["id"]
        assert not any(pattern.search(name) for name in ("src/x.py", "mkdocs_hooks.py", ".claude/other/x.py"))


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
    assert check_cli.run_changed("main", ["pre-commit"], None) == 1
    assert "FAIL pre-commit" in capsys.readouterr().out
    outcome["code"] = 0
    assert check_cli.run_changed("main", ["pre-commit"], None) == 0
    assert "ok   pre-commit" in capsys.readouterr().out


# --- the gate on every OS ---------------------------------------------------------------------------------------


def test_the_python_workflow_runs_the_local_gate_on_every_os_it_tests() -> None:
    """Nobody runs `ci run` on Linux or macOS by hand, so CI's python job does, on its whole OS matrix."""
    workflow = yaml.safe_load((REPO / ".github" / "workflows" / "python.yml").read_text(encoding="utf-8"))
    job = workflow["jobs"]["python"]
    assert job["strategy"]["matrix"]["os"]  # the OSes come from the scope script: all three, or Linux for docs only
    steps = job["steps"]
    checkout = next(step for step in steps if str(step.get("uses", "")).startswith("actions/checkout"))
    # `--base HEAD^1` is the branch a pull request's merge commit joins, so the checkout needs a second commit.
    assert int(checkout.get("with", {}).get("fetch-depth", 1)) >= 2
    gate = [step["run"] for step in steps if "coney-tools ci run" in step.get("run", "")]
    assert len(gate) == 1 and "--base HEAD^1" in gate[0]
    assert "--only pre-commit" in gate[0] and "--only repo" in gate[0]


def test_the_gates_commands_run_below_normal_priority(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    seen: list[tuple[list[str], dict[str, object]]] = []

    def fake_run(command: list[str], **options: object) -> subprocess.CompletedProcess[str]:
        seen.append((command, options))
        return subprocess.CompletedProcess(command, 0, "out", "")

    monkeypatch.setattr(subprocess, "run", fake_run)
    assert local_gate.run_command(["pytest", "a.py"], tmp_path) == (0, "out")
    command, options = seen[0]
    assert "preexec_fn" not in options  # unsafe in threads
    if os.name == "nt":
        assert command == ["pytest", "a.py"] and options["creationflags"] == 0x00004000 | 0x08000000
    else:
        assert command[-2:] == ["pytest", "a.py"] and (len(command) == 2 or command[1:3] == ["-n", "5"])


# --- check slots and live progress (issue 1135) ------------------------------------------------------------------


def test_the_python_step_holds_a_check_slot_for_pytest_only_and_runs_every_command_live(tmp_path: Path) -> None:
    from collections.abc import Iterator
    from contextlib import contextmanager

    ran: list[str] = []
    held: list[str] = []

    @contextmanager
    def hold(root: Path) -> Iterator[None]:
        held.append("enter")
        yield
        held.append("leave")

    def run(command: list[str], root: Path) -> tuple[int, str]:
        label = next(word for word in command if word in ("pytest", "mypy", "ruff"))
        ran.append(label)
        if label == "pytest":
            assert held == ["enter"]  # pytest runs inside the slot
        else:
            assert held in ([], ["enter", "leave"])  # the others never hold one
        return 0, ""

    result = check_cli._step_python(REPO, ["python/src/coney_tools/x.py"], hold=hold, run=run)
    assert result.ok and "pytest" in ran and held == ["enter", "leave"]


def test_run_changed_says_each_step_start_and_exits_75_when_pytest_waited_too_long(
    repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    from coney_tools.build_claims import SlotTimeout

    write(repo, "coney.local.example.toml", "# marks the checkout root\n")
    write(repo, "docs/page.md", "# changed\n")
    monkeypatch.chdir(repo)

    def waiting(root: Path, changed: list[str]) -> local_gate.StepResult:
        raise SlotTimeout("no check slot free after 180 minutes", 2, 4)

    monkeypatch.setattr(check_cli, "_step_python", waiting)
    assert check_cli.run_changed("main", ["python"], None) == build_run.EXIT_QUEUED
    out = capsys.readouterr().out
    assert "] start python" in out and "gave up waiting for a check slot (place 2 of 4)" in out
