# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/pr_guard.py, the Claude Code hook that opens or edits a pull request only with a title
and description that pass CI's `title` and `body` checks.

The hook is a standalone, standard-library-only file, so it is loaded by path, like the other guards. The checks it
runs are `coney-tools` commands in a subprocess; here a fake runner stands in for them, so no test starts `uv`.
"""

import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run

SCRIPT = REPO / ".claude" / "hooks" / "pr_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("pr_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()


class FakeRunner:
    """Records the `coney-tools` calls and answers each with a fixed exit code."""

    def __init__(self, title_code: int = 0, body_code: int = 0) -> None:
        self.calls: list[tuple[list[str], str]] = []
        self.codes = {"check-title": title_code, "check-body": body_code}

    def __call__(self, args: list[str], stdin: str) -> tuple[int, str]:
        self.calls.append((args, stdin))
        code = self.codes[args[1]]
        return code, f"{args[1]} refused: made up" if code == 1 else "ok"


@pytest.fixture
def body(tmp_path: Path) -> Path:
    """A description file in a scratch folder."""
    path = tmp_path / "body.md"
    path.write_text("## Summary\n\nx\n", encoding="utf-8")
    return path


def test_create_with_title_and_body_file_runs_both_checks(body: Path) -> None:
    runner = FakeRunner()
    command = f'gh pr create --title "tools: Add a check" --body-file {body.name} --head b'
    assert GUARD.check(command, str(body.parent), runner=runner) is None
    assert runner.calls[0] == (["repo", "check-title", "-"], "tools: Add a check\n")
    assert runner.calls[1][0] == ["repo", "check-body", "--body", str(body), "--title", "tools: Add a check"]


def test_a_refused_body_blocks_with_the_checks_reason(body: Path) -> None:
    reason = GUARD.check(
        f"gh pr create -t 'docs: Fix x' -F \"{body}\"", str(body.parent), runner=FakeRunner(body_code=1)
    )
    assert reason is not None and reason.startswith("Blocked: ")
    assert "check-body refused: made up" in reason and "release-queue" in reason


def test_a_refused_title_blocks(body: Path) -> None:
    reason = GUARD.check(f'gh pr create --title=bad "--body-file={body}"', "", runner=FakeRunner(title_code=1))
    assert reason is not None and "check-title refused" in reason


def test_attach_lets_local_images_through(body: Path) -> None:
    runner = FakeRunner()
    command = f"gh pr create --title 'gameplay: Fix x' --body-file \"{body}\" --attach ./shots/a.png --attach ./b.png"
    assert GUARD.check(command, "", runner=runner) is None
    assert runner.calls[1][0][-1] == "--local-images"


@pytest.mark.parametrize(
    ("command", "words"),
    [
        ("gh pr create --fill", "--fill"),
        ("gh pr create --fill-first --title x", "--fill-first"),
        ("gh pr create --web", "--web"),
        ("gh pr create --title 'tools: Add x' --body 'text'", "--body"),
        ("gh pr create --title 'tools: Add x' --body-file -", "stdin"),
        ("gh pr create --title 'tools: Add x'", "no --body-file"),
        ("gh pr create --body-file body.md", "no --title"),
        ("gh pr edit 12 --body 'new text'", "--body"),
    ],
)
def test_shapes_the_checks_cannot_see_are_refused(command: str, words: str) -> None:
    runner = FakeRunner()
    reason = GUARD.check(command, "", runner=runner)
    assert reason is not None and words in reason
    assert runner.calls == []


def test_edit_checks_only_what_it_changes(body: Path) -> None:
    runner = FakeRunner()
    assert GUARD.check(f'gh pr edit 12 --body-file "{body}"', "", runner=runner) is None
    assert runner.calls == [(["repo", "check-body", "--body", str(body)], "")]
    runner = FakeRunner()
    assert GUARD.check("gh pr edit 12 --title 'tools: Fix y'", "", runner=runner) is None
    assert runner.calls == [(["repo", "check-title", "-"], "tools: Fix y\n")]


@pytest.mark.parametrize(
    "command",
    [
        "gh pr edit 12 --add-label S-blocked",
        "gh pr view 12 --json body",
        "gh pr list --state open",
        "echo 'gh pr create --fill'",
        "git commit -m 'run gh pr create --fill later'",
        "cat <<'EOF' > notes.md\ngh pr create --fill\nEOF",
        "$text = @'\ngh pr create --web\n'@",
        "grep -n 'gh pr create' docs/guides/how-we-work.md",
    ],
)
def test_other_commands_pass_without_a_check(command: str) -> None:
    runner = FakeRunner()
    assert GUARD.check(command, "", runner=runner) is None
    assert runner.calls == []


def test_a_command_after_a_separator_is_found(body: Path) -> None:
    reason = GUARD.check(f'cd "{body.parent}" && gh pr create --fill', "", runner=FakeRunner())
    assert reason is not None and "--fill" in reason


def test_a_missing_file_or_a_check_that_cannot_run_fails_open(body: Path) -> None:
    runner = FakeRunner()
    assert (
        GUARD.check("gh pr create --title 'tools: Add x' --body-file gone.md", str(body.parent), runner=runner) is None
    )
    assert [args[1] for args, _ in runner.calls] == ["check-title"]
    assert GUARD.check(f'gh pr create -t x -F "{body}"', "", runner=FakeRunner(title_code=2, body_code=2)) is None


def test_an_unclosed_quote_fails_open() -> None:
    assert GUARD.check("gh pr create --title 'unclosed --fill", "", runner=FakeRunner()) is None


def test_powershell_keeps_backslashes_in_paths() -> None:
    windows = r"C:\Users\me\scratch\PR.md"
    calls = GUARD.pr_calls(f'gh pr create --title "tools: Add x" --body-file "{windows}"', powershell=True)
    assert calls is not None
    assert calls[0].body_file == windows


# A backslash path names the file only on Windows; elsewhere the hook finds no file and runs no check.
@pytest.mark.skipif(os.name != "nt", reason="backslashes separate paths only on Windows")
def test_powershell_backslash_path_reaches_the_check(tmp_path: Path) -> None:
    folder = tmp_path / "scratch"
    folder.mkdir()
    (folder / "PR.md").write_text("x", encoding="utf-8")
    runner = FakeRunner()
    windows = str(folder / "PR.md").replace("/", "\\")
    command = f'gh pr create --title "tools: Add x" --body-file "{windows}"'
    assert GUARD.check(command, "", powershell=True, runner=runner) is None
    assert runner.calls[1][0][3] == windows


def test_another_repository_passes(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(GUARD, "this_repository", lambda cwd: "ours/coney")
    assert GUARD.check("gh pr create --fill -R someone/else", "", runner=FakeRunner()) is None
    assert GUARD.check("gh pr create --fill -R ours/coney", "", runner=FakeRunner()) is not None


@pytest.mark.parametrize(
    "command",
    [
        "gh -R ours/coney pr create --title 'tools: Add x' --body 'text'",
        "gh --repo ours/coney pr create --fill",
        "gh --repo=ours/coney pr create --web",
        "gh --hostname github.com pr edit 5 --body x",
        "/usr/bin/gh pr create --fill",
    ],
)
def test_gh_options_before_pr_do_not_hide_the_command(monkeypatch: pytest.MonkeyPatch, command: str) -> None:
    monkeypatch.setattr(GUARD, "this_repository", lambda cwd: "ours/coney")
    reason = GUARD.check(command, "", runner=FakeRunner())
    assert reason is not None and reason.startswith("Blocked: ")


def test_a_repository_named_before_pr_is_still_another_repository(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(GUARD, "this_repository", lambda cwd: "ours/coney")
    assert GUARD.check("gh -R someone/else pr create --fill", "", runner=FakeRunner()) is None


@pytest.mark.parametrize(
    ("command", "powershell"),
    [
        ("$url = gh pr create --fill", True),
        ("foreach ($b in $bs) { gh pr create --fill }", True),
        ("& gh pr create --fill", True),
        ('bash -c "gh pr create --fill"', False),
        ("pwsh -NoProfile -Command 'gh pr create --web'", True),
        ("if true; then gh pr create --fill; fi", False),
        ("env GH_REPO=x gh pr create --fill", False),
        ("command gh pr create --fill", False),
    ],
)
def test_wrapped_commands_are_found(command: str, powershell: bool) -> None:
    reason = GUARD.check(command, "", powershell=powershell, runner=FakeRunner())
    assert reason is not None and reason.startswith("Blocked: ")


def test_a_quoted_pattern_is_not_a_command() -> None:
    runner = FakeRunner()
    assert GUARD.check('grep -E "(gh pr create|gh pr edit)" docs/guides/*.md', "", runner=runner) is None
    assert runner.calls == []


def test_a_leading_cd_moves_where_the_file_is_found(tmp_path: Path) -> None:
    (tmp_path / "PR.md").write_text("decoy", encoding="utf-8")
    scratch = tmp_path / "scratch" / "unit"
    scratch.mkdir(parents=True)
    (scratch / "PR.md").write_text("## Summary\n", encoding="utf-8")
    runner = FakeRunner()
    command = "cd scratch/unit && gh pr create --title 'tools: Add x' --body-file PR.md"
    assert GUARD.check(command, str(tmp_path), runner=runner) is None
    assert runner.calls[1][0][3] == str(scratch / "PR.md")
    runner = FakeRunner()
    command = f'Set-Location "{scratch}"; gh pr create --title "tools: Add x" --body-file PR.md'
    assert GUARD.check(command, str(tmp_path), powershell=True, runner=runner) is None
    assert runner.calls[1][0][3] == str(scratch / "PR.md")


def test_a_drive_path_is_absolute_on_every_system() -> None:
    """`Path` on Linux and macOS reads `C:/x` as a relative name; a command can carry one on any system."""
    assert GUARD._absolute(PurePosixPath("C:/Repositories/PR.md"))
    assert GUARD._absolute(PurePosixPath("C:" + chr(92) + "Repositories" + chr(92) + "PR.md"))
    assert GUARD._absolute(PurePosixPath("/srv/PR.md"))
    assert GUARD._absolute(PureWindowsPath("C:/Repositories/PR.md"))
    assert not GUARD._absolute(PurePosixPath("scratch/PR.md"))


def test_a_cd_to_an_expansion_skips_the_file_check(tmp_path: Path) -> None:
    (tmp_path / "PR.md").write_text("decoy", encoding="utf-8")
    runner = FakeRunner()
    command = 'cd "$UNIT" && gh pr create --title "tools: Add x" --body-file PR.md'
    assert GUARD.check(command, str(tmp_path), runner=runner) is None
    assert [args[1] for args, _ in runner.calls] == ["check-title"]


@pytest.mark.parametrize(
    ("command", "powershell"),
    [
        ('gh pr create --title "$T" --body-file PR.md', False),
        ('gh pr create --title "$(head -1 PR.md)" --body-file PR.md', False),
        ("gh pr create --title $(head -1 PR.md) --body-file PR.md", False),
        ("gh pr create --title $t --body-file PR.md", True),
        ("gh pr create --title (Get-Content TITLE -TotalCount 1) --body-file PR.md", True),
    ],
)
def test_a_title_the_shell_fills_in_is_not_checked(tmp_path: Path, command: str, powershell: bool) -> None:
    (tmp_path / "PR.md").write_text("## Summary\n", encoding="utf-8")
    runner = FakeRunner(title_code=1)
    assert GUARD.check(command, str(tmp_path), powershell=powershell, runner=runner) is None
    assert [args for args, _ in runner.calls] == [["repo", "check-body", "--body", str(tmp_path / "PR.md")]]


def test_powershell_quote_escapes_are_undone(body: Path) -> None:
    runner = FakeRunner()
    command = f"gh pr create --title 'docs: Fix the player''s guide' --body-file \"{body}\""
    assert GUARD.check(command, "", powershell=True, runner=runner) is None
    assert runner.calls[0][1] == "docs: Fix the player's guide\n"


def test_a_dry_run_passes_without_a_check() -> None:
    runner = FakeRunner()
    assert GUARD.check("gh pr create --dry-run --fill", "", runner=runner) is None
    assert runner.calls == []


def test_a_check_that_crashes_fails_open(body: Path) -> None:
    def crashing(args: list[str], stdin: str) -> tuple[int, str]:
        return 1, "Traceback (most recent call last):\n  File x\nKeyError: 'y'"

    assert GUARD.check(f"gh pr create -t 'tools: Add x' -F \"{body}\"", "", runner=crashing) is None


@pytest.mark.skipif(os.name != "nt", reason="Git Bash drive paths exist only on Windows")
def test_git_bash_drive_paths_are_read_on_windows(body: Path) -> None:
    drive, rest = str(body.parent).split(":", 1)
    msys = f"/{drive.lower()}{rest.replace(chr(92), '/')}"
    runner = FakeRunner()
    assert GUARD.check(f"cd {msys}; gh pr create -t 'tools: Add x' -F {body.name}", "", runner=runner) is None
    assert Path(runner.calls[1][0][3]) == body


def run_main(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str) -> tuple[int, str]:
    """Run the hook's entry point on `stdin`; the exit code and stderr."""
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main()
    return code, capsys.readouterr().err


def payload(command: str, tool: str = "Bash") -> str:
    """The JSON Claude Code sends a PreToolUse hook."""
    return json.dumps({"tool_name": tool, "tool_input": {"command": command}, "cwd": "."})


def test_main_blocks_with_exit_2(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    code, err = run_main(monkeypatch, capsys, payload("gh pr create --fill"))
    assert code == 2 and "Blocked:" in err


@pytest.mark.parametrize("stdin", ["", "not json", json.dumps({"tool_input": {}}), payload("ls -la")])
def test_main_lets_anything_else_through(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run_main(monkeypatch, capsys, stdin) == (0, "")


def test_the_dispatcher_runs_the_hook_for_both_shells() -> None:
    assert "pr_guard" in guards_run("Bash", "gh pr create") and "pr_guard" in guards_run("PowerShell", "gh pr edit 1")
