# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools ci pr`: the checks on the title and the description, the docs rule, the engine step's scope and the
command's exit code and its steps."""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest
from conventions_support import REPO, write_checkout
from test_repo_checks import body

from coney_tools import check_cli, check_pr, local_gate
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.local_gate import StepResult

CONVENTIONS = load_conventions(REPO)


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


@pytest.fixture
def checkout(tmp_path: Path) -> Path:
    """A checkout holding the real conventions on `main`, then a branch `work`."""
    (tmp_path / "repo").mkdir()
    root = write_checkout(tmp_path / "repo")
    git(root, "init", "-q", "-b", "main")
    (root / "docs").mkdir()
    (root / "docs" / "page.md").write_text("# page\n", encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    git(root, "switch", "-q", "-c", "work")
    return root


def commit(root: Path, name: str, message: str = "work") -> None:
    """Write a file below `root` and commit it with `message`."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("x\n", encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", message)


def write_body(root: Path, text: str | None = None) -> None:
    """Write the description file `body.md` in `root`."""
    (root / "body.md").write_text(body() if text is None else text, encoding="utf-8")


# --- the title ---------------------------------------------------------------------------------------------------


def test_a_title_in_the_form_passes() -> None:
    assert check_pr.step_title("tools: Add the pull request check", CONVENTIONS).ok


@pytest.mark.parametrize("title", ["Codex/tools noclip 243", "tools: Added the check.", "web: Add a page", ""])
def test_a_title_that_breaks_the_rules_is_refused_with_the_reason(title: str) -> None:
    result = check_pr.step_title(title, CONVENTIONS)
    assert not result.ok and result.output


# --- the description ---------------------------------------------------------------------------------------------


def test_a_filled_in_description_passes(checkout: Path) -> None:
    result = check_pr.step_body(checkout, body(), "tools: Add the check", "main", CONVENTIONS)
    assert result.ok, result.output


def test_the_unfilled_template_is_refused(checkout: Path) -> None:
    template = (REPO / ".github" / "pull_request_template.md").read_text(encoding="utf-8")
    result = check_pr.step_body(checkout, template, "tools: Add the check", "main", CONVENTIONS)
    assert not result.ok and any("Summary" in line for line in result.output)


def test_a_model_line_must_name_the_model_a_commit_trailer_names(checkout: Path) -> None:
    commit(checkout, "a.txt", "tools: Add a\n\nCo-Authored-By: GPT-6.1 Sol <noreply@openai.com>")
    refused = check_pr.step_body(checkout, body(model="Claude Sonnet 5.5"), "tools: Add a", "main", CONVENTIONS)
    assert not refused.ok and any("GPT-6.1 Sol" in line for line in refused.output)
    named = check_pr.step_body(checkout, body(model="GPT-6.1 Sol"), "tools: Add a", "main", CONVENTIONS)
    assert named.ok, named.output


def test_a_title_a_player_sees_needs_the_runtime_line(checkout: Path) -> None:
    result = check_pr.step_body(checkout, body(), "gameplay: Add a thing", "main", CONVENTIONS)
    assert not result.ok and any("Runtime:" in line for line in result.output)


# --- the docs rule -----------------------------------------------------------------------------------------------


def test_a_code_change_needs_docs_or_the_docs_none_line() -> None:
    changed = ["python/src/coney_tools/x.py"]
    assert not check_pr.step_docs_rule(changed, body(), CONVENTIONS).ok
    assert check_pr.step_docs_rule([*changed, "docs/guides/x.md"], body(), CONVENTIONS).ok
    assert check_pr.step_docs_rule(changed, body(summary="Adds it.\n\nDocs: none"), CONVENTIONS).ok


# --- the files of the pull request ---------------------------------------------------------------------------------


def test_the_files_are_the_committed_ones_with_deletions_as_the_pull_request_lists_them(checkout: Path) -> None:
    commit(checkout, "python/src/coney_tools/gone.py", "tools: Add gone")
    git(checkout, "switch", "-q", "-c", "removal")
    git(checkout, "rm", "-q", "python/src/coney_tools/gone.py")
    git(checkout, "commit", "-q", "-m", "tools: Remove gone")
    (checkout / "docs" / "uncommitted.md").write_text("x" + chr(10), encoding="utf-8")
    listed = local_gate.pull_request_files(checkout, "work")
    # The deleted file is listed (changed_files leaves it out) and the file not committed is not (changed_files has it).
    assert listed == ["python/src/coney_tools/gone.py"]
    assert "docs/uncommitted.md" in local_gate.changed_files(checkout, "work")
    assert not check_pr.step_docs_rule(listed, body(), CONVENTIONS).ok


def test_the_docs_rule_and_the_engine_step_read_the_pull_request_files(checkout: Path) -> None:
    commit(checkout, "src/core/x.cpp")
    (checkout / "docs" / "uncommitted.md").write_text("x" + chr(10), encoding="utf-8")
    steps = check_pr.pr_steps(checkout, "main", "core: Add x", body(), "dev", "alice")
    assert not steps["docs-rule"]().ok


# --- the engine step ---------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("changed", "wanted"),
    [
        (["src/core/x.cpp"], True),
        (["tests/a_test.cpp"], True),
        (["CMakeLists.txt"], True),
        (["docs/guides/x.md", "python/src/coney_tools/cli.py"], False),
        (["README.md", ".claude/hooks/dispatch.py"], False),
        (["a-file-in-no-class.xyz"], True),
        ([], False),
    ],
)
def test_the_engine_is_built_when_a_file_it_reads_changed(changed: list[str], wanted: bool) -> None:
    assert check_pr.engine_needed(changed, CONVENTIONS) is wanted


def test_the_engine_step_skips_without_an_engine_change(checkout: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(local_gate, "run_command", lambda *_: pytest.fail("a build ran"))
    result = check_pr.step_engine(checkout, ["docs/guides/x.md"], CONVENTIONS, "dev", None)
    assert result.ok and result.skipped


def test_the_engine_step_runs_coney_tools_build_with_the_tests(checkout: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    seen: list[list[str]] = []

    def fake(command: list[str], cwd: Path) -> tuple[int, str]:
        seen.append(command)
        return 0, ""

    monkeypatch.setattr(local_gate, "run_command", fake)
    result = check_pr.step_engine(checkout, ["src/core/x.cpp"], CONVENTIONS, "asan", "alice")
    assert result.ok and not result.skipped
    command = seen[0]
    assert command[1:4] == ["-m", "coney_tools.cli", "build"]
    assert command[4:] == ["--preset", "asan", "--test", "--agent", "alice"]
    assert "cmake" not in command and "ctest" not in command and "ninja" not in command


def test_the_command_without_agent_gives_the_build_an_id_because_the_build_sees_no_terminal(
    checkout: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """`run_command` captures the build's output, so `build` is never typed in a terminal and refuses a run with no
    `--agent`; the id has to be resolved here, in the process that may be one."""
    monkeypatch.chdir(checkout)
    commit(checkout, "src/core/x.cpp")
    write_body(checkout)
    seen: list[list[str]] = []
    real_run = local_gate.run_command

    def fake(command: list[str], cwd: Path) -> tuple[int, str]:
        if command[0] == "git":
            return real_run(command, cwd)
        seen.append(command)
        return 0, ""

    monkeypatch.setattr(local_gate, "run_command", fake)
    argv = ["check", "pr", "--base", "main", "--body", "body.md", "--title", "core: Add x", "--only", "engine"]
    assert main(argv) == 0
    build = seen[0]
    assert "--agent" in build and build[build.index("--agent") + 1]
    # An id the person gave is passed as it is.
    seen.clear()
    assert main([*argv, "--agent", "alice"]) == 0
    assert seen[0][seen[0].index("--agent") + 1] == "alice"


def test_the_engine_step_shows_the_end_of_a_failed_build(checkout: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    output = "".join(f"line {number}\n" for number in range(100))
    monkeypatch.setattr(local_gate, "run_command", lambda *_: (1, output))
    result = check_pr.step_engine(checkout, ["src/core/x.cpp"], CONVENTIONS, "dev", None)
    assert not result.ok
    assert result.output[-1] == "line 99" and len(result.output) == local_gate.MAX_OUTPUT_LINES


# --- the command -------------------------------------------------------------------------------------------------


def test_the_command_has_every_step_of_the_gate_and_runs_the_text_first_and_the_engine_last() -> None:
    assert set(check_cli.STEPS) < set(check_pr.STEPS)
    assert check_pr.STEPS[:3] == ("title", "body", "docs-rule") and check_pr.STEPS[-1] == "engine"


def test_there_is_a_step_function_for_every_step_name(checkout: Path) -> None:
    steps = check_pr.pr_steps(checkout, "main", "tools: Add it", body(), "dev", None)
    assert set(steps) == set(check_pr.STEPS)


def test_the_exit_code_is_1_when_a_step_fails_and_0_when_all_pass(
    checkout: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(checkout)
    write_body(checkout)
    argv = ["check", "pr", "--base", "main", "--body", "body.md", "--only", "title", "--only", "body"]
    assert main([*argv, "--title", "tools: Add it"]) == 0
    assert "ok   title" in capsys.readouterr().out
    assert main([*argv, "--title", "Codex/tools noclip 243"]) == 1
    out = capsys.readouterr().out
    assert "FAIL title" in out and "CI would refuse this pull request too" in out


def test_only_and_skip_choose_the_steps(
    checkout: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(checkout)
    write_body(checkout)
    argv = ["check", "pr", "--base", "main", "--body", "body.md", "--title", "tools: Add it"]
    assert main([*argv, "--only", "title", "--only", "docs-rule"]) == 0
    printed = capsys.readouterr().out
    assert "title:" in printed and "docs-rule:" in printed and "body:" not in printed and "engine" not in printed
    assert main([*argv, "--only", "title", "--only", "body", "--skip", "title"]) == 0
    printed = capsys.readouterr().out
    assert "title:" not in printed and "body:" in printed


def test_an_unreadable_description_is_a_setup_problem(checkout: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(checkout)
    with pytest.raises(ConfigError, match="the description"):
        check_pr.run_pr("main", "tools: Add it", checkout / "missing.md", ["title"], None)
    assert main(["check", "pr", "--body", "missing.md", "--title", "tools: Add it"]) == 2


def test_the_engine_is_not_built_for_a_docs_only_pull_request(
    checkout: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(checkout)
    commit(checkout, "docs/new.md")
    write_body(checkout)
    real_run = local_gate.run_command

    def fake_run(command: list[str], cwd: Path) -> tuple[int, str]:
        if command[0] != "git":
            pytest.fail(f"ran {command}")
        return real_run(command, cwd)

    monkeypatch.setattr(local_gate, "run_command", fake_run)
    argv = ["check", "pr", "--base", "main", "--body", "body.md", "--title", "docs: Add a page", "--only", "engine"]
    assert main(argv) == 0
    assert "skip engine: no engine file changed" in capsys.readouterr().out


def test_a_failing_step_prints_its_output(capsys: pytest.CaptureFixture[str]) -> None:
    steps = {"a": lambda: StepResult("a", False, "bad", output=("why",))}
    assert check_cli.run_steps(["a"], steps)
    printed = capsys.readouterr().out
    assert "FAIL a: bad" in printed and "     why" in printed
