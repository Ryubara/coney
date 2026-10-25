# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/pr_guard.py, the Claude Code hook that opens or edits a pull request only with a title
and description that pass CI's `title` and `body` checks and, with `pull_requests.open_only` on, only through
`coney-tools pr open`.

The hook is a standalone, standard-library-only file, so it is loaded by path, like the other guards. The checks it
runs are `coney-tools` commands in a subprocess; here a fake runner stands in for them, so no test starts `uv`.
"""

import importlib.util
import io
import json
import os
from collections.abc import Callable
from pathlib import Path, PurePosixPath, PureWindowsPath
from types import ModuleType

import pytest
from conventions_support import REPO, real_data, write_checkout
from hooks_support import guards_run

from coney_tools.conventions import CONVENTIONS_FILE, load_conventions

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
    monkeypatch.setattr(GUARD.guard_repo, "this_repository", lambda cwd, remote="origin": "ours/coney")
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
    monkeypatch.setattr(GUARD.guard_repo, "this_repository", lambda cwd, remote="origin": "ours/coney")
    reason = GUARD.check(command, "", runner=FakeRunner())
    assert reason is not None and reason.startswith("Blocked: ")


def test_a_repository_named_before_pr_is_still_another_repository(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(GUARD.guard_repo, "this_repository", lambda cwd, remote="origin": "ours/coney")
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
    assert GUARD.is_absolute(PurePosixPath("C:/Repositories/PR.md"))
    assert GUARD.is_absolute(PurePosixPath("C:" + chr(92) + "Repositories" + chr(92) + "PR.md"))
    assert GUARD.is_absolute(PurePosixPath("/srv/PR.md"))
    assert GUARD.is_absolute(PureWindowsPath("C:/Repositories/PR.md"))
    assert not GUARD.is_absolute(PurePosixPath("scratch/PR.md"))


class _PosixPath(PurePosixPath):
    """A path with Linux and macOS rules (`C:/x` is a relative name) whose files all exist, for the call sites."""

    def is_file(self) -> bool:
        return True


def test_a_windows_path_in_the_command_is_followed_with_posix_rules(monkeypatch: pytest.MonkeyPatch) -> None:
    """Both call sites must read `C:/x` as a folder of its own, not a name under the working directory."""
    monkeypatch.setattr(GUARD.guard_shell, "Path", _PosixPath)
    monkeypatch.setattr(GUARD.guard_shell, "native", _PosixPath)
    monkeypatch.setattr(GUARD, "Path", _PosixPath)
    monkeypatch.setattr(GUARD, "native", _PosixPath)
    command = "cd C:/Repositories/scratch && gh pr create --title 'tools: Add x' --body-file PR.md"
    calls = GUARD.pr_calls(command, cwd="/home/me/unit")
    assert calls is not None and calls[0].cwd == "C:/Repositories/scratch"
    runner = FakeRunner()
    named = GUARD.PrCall("create", title="tools: Add x", body_file="C:/Repositories/scratch/PR.md", cwd="/home/me/unit")
    assert GUARD.problems(named, runner) == []
    assert runner.calls[1][0][3] == "C:/Repositories/scratch/PR.md"


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


def run_main(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str, open_only: bool | None = None
) -> tuple[int, str]:
    """Run the hook's entry point on `stdin`; the exit code and stderr. `open_only` None reads the conventions file."""
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main(open_only=open_only)
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


# --- the open_only switch (`pull_requests.open_only` of .github/conventions.yml) -------------------------------------

OPEN_ONLY_MESSAGE = "Blocked: pull requests are opened only by coney-tools pr open <task> (docs/guides/pr.md#open)"
THIS = "owner/repo"

#: Says which repository a folder's `origin` is; the tests give it instead of running git.
Origin = Callable[[str], str | None]


def here(path: str, remote: str = "origin") -> str | None:
    """The remote the tests give every folder: this repository."""
    return THIS


def elsewhere(path: str, remote: str = "origin") -> str | None:
    """A remote that is another repository, but for the project's own folder."""
    return THIS if path == str(GUARD.ROOT) else "someone/else"


def check_open(command: str, powershell: bool = False, repository: Origin = here, open_only: bool = True) -> str | None:
    """`check` with the switch given, a fake runner that must stay unused, and a fake `origin`."""
    runner = FakeRunner()
    reason: str | None = GUARD.check(command, "", powershell, runner=runner, open_only=open_only, repository=repository)
    assert runner.calls == [], "with the switch on nothing is left to validate"
    return reason


@pytest.mark.parametrize(
    "command",
    [
        "gh pr create --title 'tools: Add x' --body-file PR.md",
        "gh pr create --fill",
        "gh pr edit 12 --body-file PR.md",
        "gh pr edit 12 -F PR.md",
        "gh pr edit 12 --title 'tools: Add y'",
        "gh pr edit 12 -t 'tools: Add y'",
        "gh pr edit 12 --title='tools: Add y'",
        "gh pr edit 12 --body 'text'",
        "gh pr edit 12 -b text",
        "gh pr edit 12 --attach ./shots/a.png",
        "gh pr merge 12 --squash --auto",
        "gh pr merge --auto 12",
        "git push",
        "git push -u origin tools-x",
        "git push --force-with-lease=tools-x:abc origin tools-x",
        "git -C ../worktrees/x push origin x",
        "cd ../worktrees/x && git push",
        "git -c push.default=current push",
        "gh api repos/owner/repo/pulls -f title=x -f head=b -f base=main",
        "gh api graphql -f query='mutation { createPullRequest(input: {}) { clientMutationId } }'",
        'url=$(gh pr create --title "tools: Add x" --body-file PR.md)',
        'echo "$(gh pr create --fill)"',
        'echo "done: $(gh pr create --title "tools: Add x" --body-file PR.md) ok"',
        "echo `gh pr create --fill`",
        'echo "`git push`"',
        "bash -c 'git push origin x'",
        "if true; then gh pr create --fill; fi",
        "gh -R owner/repo pr create --fill",
    ],
)
def test_open_only_refuses_what_opens_or_rewrites_a_pull_request(command: str) -> None:
    reason = check_open(command)
    assert reason is not None and reason.startswith(OPEN_ONLY_MESSAGE)


@pytest.mark.parametrize(
    "command",
    [
        "gh pr view 12",
        "gh pr list --state open",
        "gh pr checks 12",
        "gh pr diff 12",
        "gh pr comment 12 --body 'looks good'",
        "gh pr edit 12 --add-label S-failing",
        "gh pr edit 12 --add-reviewer someone",
        "gh pr merge 12 --squash",
        "gh pr ready 12",
        "gh run rerun 4455 --failed",
        "gh run list --limit 5",
        "gh issue comment 5 --body 'x'",
        "gh api repos/owner/repo/pulls/12",
        "gh api graphql -f query='query { viewer { login } }'",
        "uv run --project python coney-tools pr open 757-fix-widget",
        "uv run --project python coney-tools pr verify",
        "git status",
        "git fetch origin main",
        "git commit -m 'tools: Add x'",
        "git log --oneline -5 -- push.py",
        'echo "gh pr create --fill"',
        "echo 'git push'",
        'git commit -m "do not git push here"',
        "gh pr create --dry-run --fill",
        "gh pr create -R someone/else --fill",
        "gh pr edit 3 -R someone/else --body-file x.md",
    ],
)
def test_open_only_leaves_everything_else_alone(command: str) -> None:
    assert check_open(command) is None


def test_open_only_refuses_the_coordinators_session_too(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # No agent_id in the input is the main session, which push_guard lets push; this guard does not.
    monkeypatch.setattr(GUARD.guard_repo, "this_repository", here)
    code, err = run_main(monkeypatch, capsys, payload("git push origin tools-x"), open_only=True)
    assert code == 2 and "`git push`" in err and "agent_id" not in err


def test_open_only_names_each_reason() -> None:
    reason = check_open("gh pr create --fill && gh pr merge 3 --auto && git push")
    assert reason is not None
    for what in ("`gh pr create`", "`gh pr merge --auto`", "`git push`"):
        assert what in reason


def test_open_only_lets_a_push_of_another_repository_through() -> None:
    assert check_open("git push origin main", repository=elsewhere) is None
    assert check_open("git -C ../tether push", repository=elsewhere) is None


def test_open_only_follows_a_cd_to_another_repository() -> None:
    seen: list[str] = []

    def origin(path: str, remote: str = "origin") -> str | None:
        seen.append(path)
        return "someone/else" if path.endswith("tether") else THIS

    assert check_open("cd ../tether && git push", repository=origin) is None
    assert any(path.endswith("tether") for path in seen)


def unknown(path: str, remote: str = "origin") -> str | None:
    """A remote git cannot name."""
    return None


def test_open_only_cannot_tell_so_it_refuses() -> None:
    # What cannot be shown to be another repository counts as this one, so the switch holds.
    assert check_open("git push", repository=unknown) is not None
    assert check_open("gh pr create --fill", repository=unknown) is not None


def test_open_only_off_validates_as_before(body: Path) -> None:
    runner = FakeRunner()
    command = f'gh pr create --title "tools: Add x" --body-file {body.name}'
    assert GUARD.check(command, str(body.parent), runner=runner, open_only=False, repository=here) is None
    assert [args[1] for args, _ in runner.calls] == ["check-title", "check-body"]
    # What the switch would refuse passes while it is off: a push, an auto-merge, an edit of the labels.
    for other in ("git push origin x", "gh pr merge 3 --squash --auto", "gh pr edit 3 --add-label x"):
        assert GUARD.check(other, "", runner=runner, open_only=False, repository=here) is None


def test_open_only_off_still_refuses_the_shapes_the_checks_cannot_see() -> None:
    reason = GUARD.check("gh pr create --fill", "", runner=FakeRunner(), open_only=False, repository=here)
    assert reason is not None and reason.startswith("Blocked: ") and "release-queue" in reason
    assert OPEN_ONLY_MESSAGE not in reason


def test_open_only_off_does_not_refuse_the_api_route() -> None:
    command = "gh api repos/owner/repo/pulls -f title=x"
    assert GUARD.check(command, "", runner=FakeRunner(), open_only=False, repository=here) is None


# --- a command inside a command substitution runs, quoted or not (the `$(...)` gap) ----------------------------------


@pytest.mark.parametrize(
    "command",
    [
        'echo "$(gh pr create --fill)"',
        'echo "url: $(gh pr create --fill) done"',
        'echo "$(gh pr create --title "tools: Add x" --fill)"',
        "echo `gh pr create --fill`",
        'echo "`gh pr create --fill`"',
        "x=`gh pr create --fill`; echo $x",
        'url=$(gh pr create --fill) && echo "$url"',
    ],
)
def test_a_pull_request_opened_inside_a_command_substitution_is_read(command: str) -> None:
    reason = GUARD.check(command, "", runner=FakeRunner(), open_only=False, repository=here)
    assert reason is not None and "--fill" in reason


def test_a_substitution_inside_quotes_is_checked_like_any_command(body: Path) -> None:
    runner = FakeRunner()
    command = f'echo "$(gh pr create --title "tools: Add x" --body-file {body.name})"'
    assert GUARD.check(command, str(body.parent), runner=runner, open_only=False, repository=here) is None
    assert [args[1] for args, _ in runner.calls] == ["check-title", "check-body"]
    assert runner.calls[0][1] == "tools: Add x\n"


def test_a_powershell_subexpression_in_a_string_runs() -> None:
    reason = GUARD.check(
        'Write-Host "url: $(gh pr create --fill)"', "", True, runner=FakeRunner(), open_only=False, repository=here
    )
    assert reason is not None and "--fill" in reason


@pytest.mark.parametrize(
    "command",
    [
        'echo "gh pr create --fill"',
        "echo 'gh pr create --fill'",
        "echo 'run `gh pr create --fill` yourself'",
        "git commit -m 'tools: Add `gh pr create --fill` rules'",
        'echo "text with $(date) and gh pr create --fill in it"',
    ],
)
def test_text_that_mentions_a_pull_request_command_still_passes(command: str) -> None:
    assert GUARD.check(command, "", runner=FakeRunner(), open_only=False, repository=here) is None


def test_a_backtick_in_a_powershell_string_is_an_escape_not_a_command() -> None:
    command = 'Write-Host "a `"gh pr create --fill`" b"'
    assert GUARD.check(command, "", True, runner=FakeRunner(), open_only=False, repository=here) is None


def test_an_unterminated_quote_hides_nothing() -> None:
    reason = GUARD.check("echo it's; gh pr create --fill", "", runner=FakeRunner(), open_only=False, repository=here)
    assert reason is not None and "--fill" in reason


# --- reading the switch from the conventions file ---------------------------------------------------------------------


def conventions_text(value: str = "false") -> str:
    """A small conventions file with `open_only` set as given under `pull_requests`, and decoys of the same name."""
    return (
        "repo:\n  open_only: true  # another table's key of the same name\n"
        "pull_requests:\n"
        "  template: .github/pull_request_template.md\n"
        "  stacked:\n"
        "    open_only: true\n"
        f"  open_only: {value}\n"
        "merge:\n  open_only: true\n"
    )


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("true", True),
        ("false", False),
        ("True", True),
        ("FALSE", False),
        ("true  # on", True),
        ("yes", True),
        ("off", False),
    ],
)
def test_the_switch_is_read_from_the_pull_requests_table(tmp_path: Path, value: str, expected: bool) -> None:
    path = tmp_path / "conventions.yml"
    path.write_text(conventions_text(value), encoding="utf-8")
    assert GUARD.read_open_only(path) is expected


@pytest.mark.parametrize(
    "text",
    [
        "",
        "pull_requests:\n  template: x\n",  # the key is missing
        "pull_requests:\n    open_only: true\n",  # deeper than the table's own keys
        "pull_requests:\n  open_only: maybe\n",  # not a yes or no
        'pull_requests:\n  open_only: "true"\n',  # text, which the loader refuses
        "pull_requests:\n  stacked:\n    open_only: true\n",  # another table's key
        "merge:\n  open_only: true\n",
        "# pull_requests:\n#  open_only: true\n",
        "pull_requests:\n  # open_only: true\n",
    ],
)
def test_anything_else_leaves_the_switch_off(tmp_path: Path, text: str) -> None:
    path = tmp_path / "conventions.yml"
    path.write_text(text, encoding="utf-8")
    assert GUARD.read_open_only(path) is False


@pytest.mark.parametrize("content", [None, b"\xff\xfe\x00"])
def test_a_file_that_cannot_be_read_leaves_the_switch_off(tmp_path: Path, content: bytes | None) -> None:
    path = tmp_path / "conventions.yml"
    if content is not None:
        path.write_bytes(content)
    assert GUARD.read_open_only(path) is False


@pytest.mark.parametrize("value", [True, False])
def test_the_reader_agrees_with_the_loader(tmp_path: Path, value: bool) -> None:
    """The hook reads one line of the file; the loader reads all of it. They must say the same."""
    data = real_data()
    data["pull_requests"]["open_only"] = value
    write_checkout(tmp_path, data)
    assert load_conventions(tmp_path).pull_requests.open_only is value
    assert GUARD.read_open_only(tmp_path / CONVENTIONS_FILE) is value


def test_the_reader_agrees_with_the_loader_on_the_committed_file() -> None:
    assert GUARD.read_open_only(REPO / CONVENTIONS_FILE) is load_conventions(REPO).pull_requests.open_only


def test_main_reads_the_switch_from_the_file(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    path = tmp_path / "conventions.yml"
    monkeypatch.setattr(GUARD.guard_repo, "CONVENTIONS", path)
    monkeypatch.setattr(GUARD.guard_repo, "this_repository", here)
    path.write_text("pull_requests:\n  open_only: true\n", encoding="utf-8")
    code, err = run_main(monkeypatch, capsys, payload("git push origin x"))
    assert code == 2 and err.startswith(OPEN_ONLY_MESSAGE)
    path.write_text("pull_requests:\n  open_only: false\n", encoding="utf-8")
    assert run_main(monkeypatch, capsys, payload("git push origin x")) == (0, "")


def test_no_second_switch_is_left_in_the_hook_settings() -> None:
    settings = (REPO / ".claude" / "hooks" / "hooks.toml").read_text(encoding="utf-8")
    assert "refuse_pr_create" not in settings and "[pr_guard]" not in settings


# --- the review round: the repository a command is for, aliases, option spellings, `gh api`, harmless forms ---------


def in_tether(path: str, remote: str = "origin") -> str | None:
    """Every folder's remote is this repository but `tether`'s, which is another one."""
    return "someone/tether" if path.endswith("tether") else THIS


@pytest.mark.parametrize(
    "named",
    [
        "owner/repo",
        "Owner/Repo",
        "github.com/owner/repo",
        "https://github.com/owner/repo",
        "https://github.com/owner/repo.git",
        "git@github.com:owner/repo.git",
        "ssh://git@github.com/owner/repo.git",
    ],
)
@pytest.mark.parametrize("flag", ["-R {}", "--repo {}", "--repo={}", "-R{}"])
@pytest.mark.parametrize("verb", ["pr create --fill", "pr merge 5 --auto", "pr edit 5 --body-file x.md"])
def test_open_only_names_this_repository_in_any_form_from_another_folder(named: str, flag: str, verb: str) -> None:
    # The probe of the review: a checkout of another repository, and -R naming this one.
    command = f"cd ../tether && gh {verb} {flag.format(named)}"
    reason = check_open(command, repository=in_tether)
    assert reason is not None and reason.startswith(OPEN_ONLY_MESSAGE)


@pytest.mark.parametrize(
    "named", ["someone/tether", "github.com/someone/tether", "https://github.com/someone/tether.git"]
)
def test_open_only_lets_another_repository_through_in_any_form(named: str) -> None:
    assert check_open(f"gh pr create --fill -R {named}", repository=in_tether) is None
    assert check_open(f"gh pr merge 5 --auto --repo {named}", repository=in_tether) is None


def test_open_only_judges_a_command_without_r_by_the_folder_it_runs_in() -> None:
    assert check_open("cd ../tether && gh pr create --fill", repository=in_tether) is None
    assert check_open("gh pr create --fill", repository=in_tether) is not None


@pytest.mark.parametrize(
    "command",
    [
        "cd ../tether && git push https://github.com/owner/repo.git HEAD:x",
        "git -C ../tether push https://github.com/owner/repo HEAD:main",
        "git -C ../tether push git@github.com:owner/repo.git HEAD:main",
        "git -C ../tether push ssh://git@github.com/Owner/Repo.git HEAD:main",
        "git -C ../tether push --repo=https://github.com/owner/repo.git",
        "git -C ../tether push --repo https://github.com/owner/repo.git",
        "git -C ../tether push -o ci.skip https://github.com/owner/repo.git HEAD",
        "git -C ../tether push ../repo-bare.git HEAD",  # a path: cannot be told, so it counts as ours
        "git push https://github.com/owner/repo.git HEAD:x",
        'git push "$REMOTE" HEAD',
    ],
)
def test_open_only_judges_a_push_by_where_it_writes(command: str) -> None:
    reason = check_open(command, repository=in_tether)
    assert reason is not None and "`git push`" in reason


@pytest.mark.parametrize(
    "command",
    [
        "git -C ../tether push",
        "git -C ../tether push origin main",
        "cd ../tether && git push -u origin main",
        "git push https://github.com/someone/tether.git HEAD",
        "git push git@github.com:someone/tether.git HEAD",
        "git push --repo=https://github.com/someone/tether.git",
    ],
)
def test_open_only_lets_a_push_to_another_repository_through(command: str) -> None:
    assert check_open(command, repository=in_tether) is None


def test_open_only_resolves_a_named_remote_in_the_folder() -> None:
    def origin(path: str, remote: str = "origin") -> str | None:
        return "someone/else" if remote == "backup" else THIS

    assert check_open("git push backup main", repository=origin) is None
    assert check_open("git push origin main", repository=origin) is not None
    assert check_open("git push upstream main", repository=origin) is not None


# `gh pr new` is gh's alias of `gh pr create`.
def test_gh_pr_new_is_gh_pr_create() -> None:
    reason = check_open("gh pr new --fill")
    assert reason is not None and "`gh pr create`" in reason
    off = GUARD.check("gh pr new --fill", "", runner=FakeRunner(), open_only=False, repository=here)
    assert off is not None and "takes the description from somewhere" in off


def test_gh_pr_new_is_checked_like_create_while_the_switch_is_off(body: Path) -> None:
    runner = FakeRunner()
    command = f'gh pr new --title "tools: Add x" --body-file {body.name}'
    assert GUARD.check(command, str(body.parent), runner=runner, open_only=False, repository=here) is None
    assert [args[1] for args, _ in runner.calls] == ["check-title", "check-body"]


@pytest.mark.parametrize(
    "command",
    [
        "gh pr merge 5 --auto=true",
        "gh pr merge 5 --auto=1",
        "gh pr merge --auto=T 5",
        "gh pr edit 5 -tfoo",
        "gh pr edit 5 -FPR.md",
        "gh pr edit 5 -bx",
        "gh pr edit 5 --title=foo",
        "gh pr edit 5 --attach=./a.png",
        "gh pr create -tfoo -FPR.md",
    ],
)
def test_open_only_reads_attached_and_equals_forms(command: str) -> None:
    assert check_open(command) is not None


@pytest.mark.parametrize(
    "command", ["gh pr merge 5 --auto=false", "gh pr merge 5 --squash", "gh pr edit 5 --add-label x"]
)
def test_open_only_lets_a_merge_without_auto_through(command: str) -> None:
    assert check_open(command) is None


def test_attached_short_options_are_read_while_the_switch_is_off(body: Path) -> None:
    runner = FakeRunner()
    command = f'gh pr create -t"tools: Add x" -F{body.name}'
    assert GUARD.check(command, str(body.parent), runner=runner, open_only=False, repository=here) is None
    assert runner.calls[0][1] == "tools: Add x\n"
    assert runner.calls[1][0][2:4] == ["--body", str(body)]


@pytest.mark.parametrize(
    "command",
    [
        "gh api repos/owner/repo/pulls -f title=x",
        "gh api 'repos/owner/repo/pulls' -f title=x",
        'gh api "repos/owner/repo/pulls" -X POST',
        "gh api /repos/owner/repo/pulls -X POST -f title=x",
        "gh api repos/{owner}/{repo}/pulls -f title=x",
        "gh api https://api.github.com/repos/owner/repo/pulls -f title=x",
        "gh api repos/owner/repo/pulls/12 -X PATCH -f body=x",
        "gh api repos/owner/repo/pulls/12 --method PATCH -f title=x",
        "gh api repos/owner/repo/pulls/12 --method=patch -F body=@b.md",
        "gh api 'repos/{owner}/{repo}/pulls/12' -X PATCH -f state=closed",
        'gh api graphql -f query=\'mutation { enablePullRequestAutoMerge(input: {pullRequestId: "x"}) '
        "{ clientMutationId } }'",
        "gh api graphql -f query='mutation { updatePullRequest(input: {}) { clientMutationId } }'",
        "gh api graphql -F query=@mutation.graphql",
        "gh api graphql -f query=@mutation.graphql",
        'gh api graphql -f query="$QUERY"',
        "gh api graphql --input query.json",
        "gh api graphql -f query='query {\n  viewer { login }\n}\nmutation {\n  "
        "createPullRequest(input: {}) { clientMutationId }\n}'",
    ],
)
def test_open_only_refuses_the_gh_api_routes_that_open_or_rewrite_a_pull_request(command: str) -> None:
    reason = check_open(command)
    assert reason is not None and reason.startswith(OPEN_ONLY_MESSAGE) and "`gh api" in reason


@pytest.mark.parametrize(
    "command",
    [
        "gh api repos/owner/repo/pulls/12",
        "gh api repos/owner/repo/pulls",
        "gh api repos/owner/repo/pulls/12/files",
        "gh api repos/owner/repo/pulls/12/merge -X PUT -f merge_method=squash",
        "gh api repos/owner/repo/pulls/12/comments -f body=x",
        "gh api repos/owner/repo/issues/12/comments -f body=x",
        "gh api graphql -f query='query { viewer { login } }'",
        'gh api graphql -f query=\'query($n: Int!) { repository(owner: "o", name: "r") '
        "{ pullRequest(number: $n) { title } } }'",
        'echo "gh api repos/owner/repo/pulls -f title=x"',
    ],
)
def test_open_only_lets_the_reads_of_gh_api_through(command: str) -> None:
    assert check_open(command) is None


@pytest.mark.parametrize(
    "command",
    [
        "git push --dry-run",
        "git push -n origin x",
        "git push -nu origin x",
        "git push --help",
        "git push -h",
        "git -C ../x push --dry-run origin x",
        "gh pr create --dry-run",
        "gh pr create --help",
        "gh pr create -h",
        "gh pr new --help",
        "gh pr edit --help",
        "gh pr merge --auto --help",
    ],
)
def test_open_only_lets_the_forms_that_write_nothing_through(command: str) -> None:
    assert check_open(command) is None


def test_help_passes_while_the_switch_is_off_too() -> None:
    for command in ("gh pr create --help", "gh pr create -h", "gh pr edit 5 --body x --help"):
        assert GUARD.check(command, "", runner=FakeRunner(), open_only=False, repository=here) is None


def test_the_message_points_at_a_page_that_exists() -> None:
    page = GUARD.OPEN_ONLY_HEAD.split("(")[-1].rstrip(")")
    path, anchor = page.split("#")
    assert (REPO / path).is_file() and "{#" + anchor + "}" in (REPO / path).read_text(encoding="utf-8")


@pytest.mark.parametrize("name", ["pr_guard.py", "pr_guard_shell.py", "pr_guard_repo.py", "pr_guard_routes.py"])
def test_each_file_of_the_guard_is_within_the_size_guide(name: str) -> None:
    assert len((REPO / ".claude" / "hooks" / name).read_text(encoding="utf-8").splitlines()) <= 500


def test_the_refused_list_has_one_home() -> None:
    """The guides link to the PR guard's section instead of writing the list out again."""
    home = (REPO / "docs" / "guides" / "enforcement.md").read_text(encoding="utf-8")
    assert "{#pr-guard}" in home and "gh pr merge --auto" in home
    for page in ("agents.md", "how-we-work.md"):
        text = (REPO / "docs" / "guides" / page).read_text(encoding="utf-8")
        assert "enforcement.md#pr-guard" in text, page
        assert "gh pr merge --auto" not in text, f"{page} writes out the refused list; link to enforcement.md#pr-guard"


# --- the second review: what the guard cannot tell is refused with the switch on -------------------------------------

TETHER = str(Path("C:/work/tether"))


def check_from(command: str, cwd: str, powershell: bool = False, repository: Origin = in_tether) -> str | None:
    """`check` with the switch on, run from the session folder `cwd`."""
    reason: str | None = GUARD.check(
        command, cwd, powershell, runner=FakeRunner(), open_only=True, repository=repository
    )
    return reason


@pytest.mark.parametrize(
    "command",
    [
        "git --git-dir .git push origin HEAD:x",
        "git --git-dir=C:/work/coney/.git push origin HEAD:x",
        "git --git-dir C:/work/coney/.git push origin HEAD:x",
        "GIT_DIR=C:/work/coney/.git git push origin HEAD:x",
        "git -c remote.origin.pushurl=https://github.com/owner/repo.git push",
        "git -c url.https://github.com/owner/repo.git.pushInsteadOf=x push x HEAD",
    ],
)
def test_open_only_reads_git_options_that_take_a_value_and_move_the_repository(command: str) -> None:
    reason = check_from(command, TETHER)
    assert reason is not None and "`git push`" in reason


@pytest.mark.parametrize(
    "command",
    ["git --work-tree . push origin HEAD:x", "git --namespace n push origin HEAD:x", "git --super-prefix p push"],
)
def test_open_only_finds_the_push_behind_a_git_option_with_a_separate_value(command: str) -> None:
    assert check_open(command) is not None


def test_open_only_still_lets_a_push_of_another_repository_through_with_git_options() -> None:
    assert check_from("git --no-pager push origin HEAD:x", TETHER) is None
    assert check_from("git --work-tree . push origin HEAD:x", TETHER) is None
    assert check_from("git -c core.quotepath=off push origin HEAD:x", TETHER) is None


def test_open_only_takes_the_remote_on_the_command_line_over_repo() -> None:
    """git-push(1): when both are given, the command-line argument takes precedence."""
    assert check_open("git push --repo=https://github.com/someone/tether.git origin HEAD:x") is not None
    assert check_open("git push --repo https://github.com/someone/tether.git origin HEAD:x") is not None
    assert check_from("git push --repo=https://github.com/owner/repo.git origin HEAD:x", TETHER) is None
    assert check_open("git push --repo=https://github.com/someone/tether.git", repository=here) is None


@pytest.mark.parametrize(
    "command",
    [
        'cd "$CONEY" && git push origin HEAD:x',
        'git -C "$CONEY" push',
        'cd "$CONEY" && gh pr create --fill',
        'cd "$CONEY" && gh pr merge 5 --auto',
    ],
)
def test_open_only_refuses_what_runs_in_a_folder_it_cannot_follow(command: str) -> None:
    assert check_from(command, TETHER) is not None


def test_open_only_refuses_a_powershell_location_it_cannot_follow() -> None:
    assert check_from("Set-Location $env:CONEY; gh pr create --fill", TETHER, powershell=True) is not None


def test_open_only_still_reads_an_address_from_a_folder_it_cannot_follow() -> None:
    assert check_from("git push origin HEAD:x", TETHER) is None
    assert check_from('cd "$X" && git push https://github.com/someone/tether.git HEAD', TETHER) is None
    assert check_from('cd "$X" && gh pr create --fill -R someone/tether', TETHER) is None


@pytest.mark.parametrize(
    "command",
    [
        "gh pr create --dry-run=false --fill",
        "gh pr create --dry-run --dry-run=false --fill",
        "gh pr merge 5 --auto --help=false",
        "git push -n --no-dry-run origin HEAD:x",
        "git push --dry-run --no-dry-run origin HEAD:x",
        "git push --dry-run=x origin HEAD:x",
        "git push -on origin HEAD:x",  # `-o n`: a push option, no dry run
        "git push -o n origin HEAD:x",
        "git push --re origin HEAD:x",  # ambiguous (`--repo`, `--receive-pack`, `--recurse-submodules`)
        "git push --made-up origin HEAD:x",
    ],
)
def test_open_only_reads_dry_run_and_help_by_their_value(command: str) -> None:
    assert check_open(command) is not None


@pytest.mark.parametrize(
    "command",
    [
        "gh pr create --dry-run=true --fill",
        "gh pr merge 5 --auto --help",
        "git push --no-dry-run -n origin HEAD:x",
        "git push -un origin HEAD:x",
        "git push --dry origin HEAD:x",  # git takes a unique prefix
        "git push -h",
        "git push --rep=https://github.com/someone/tether.git",
    ],
)
def test_open_only_lets_dry_runs_and_help_through(command: str) -> None:
    assert check_open(command) is None


def test_open_only_off_reads_dry_run_by_its_value() -> None:
    off = GUARD.check("gh pr create --dry-run=false --fill", "", runner=FakeRunner(), open_only=False, repository=here)
    assert off is not None and "--fill" in off
    assert (
        GUARD.check("gh pr create --dry-run --fill", "", runner=FakeRunner(), open_only=False, repository=here) is None
    )


@pytest.mark.parametrize(
    "command", ["gh pr create --title $'it\\'s' --body-file b", "gh pr merge 5 --auto --subject $'x\\'y'"]
)
def test_open_only_refuses_arguments_it_cannot_split(command: str) -> None:
    reason = check_open(command)
    assert reason is not None and "cannot read" in reason
    off = GUARD.check(command, "", runner=FakeRunner(), open_only=False, repository=here)
    assert off is None, "with the switch off the hook fails open on what it cannot read"


def test_open_only_judges_unreadable_arguments_by_the_folder_unless_they_may_name_a_repository() -> None:
    assert check_from("gh pr create --title $'it\\'s' --body-file b", TETHER) is None
    assert check_from("gh pr create -R someone/tether --title $'it\\'s'", TETHER) is not None


def test_an_escaped_backtick_does_not_start_a_substitution() -> None:
    assert check_open("echo \\`; gh pr merge 3 --auto") is not None
    assert check_open("echo \\` x \\`; gh pr merge 3 --auto") is not None


def test_backticks_end_the_command_inside_them() -> None:
    """Bash runs `gh pr merge 3` in the substitution and passes `--auto` to `echo`."""
    assert check_open("echo `gh pr merge 3` --auto") is None
    assert check_open("echo `gh pr merge 3 --auto`") is not None


def test_an_attached_value_after_equals_is_the_value(body: Path) -> None:
    """pflag reads `-F=PR.md` as `-F PR.md` and `-t=x` as `-t x`."""
    runner = FakeRunner()
    command = f"gh pr create -t='tools: Add x' -F={body.name}"
    assert GUARD.check(command, str(body.parent), runner=runner, open_only=False, repository=here) is None
    assert runner.calls[0] == (["repo", "check-title", "-"], "tools: Add x\n")
    assert runner.calls[1][0][:4] == ["repo", "check-body", "--body", str(body)]


@pytest.mark.parametrize(
    "command",
    [
        "gh api -X=PATCH repos/o/r/pulls/5 -f body=x",
        "gh api 'repos/o/r/pulls?x=1' -f title=t -f head=h -f base=main",
        "gh api repos/o/r/pulls/5#x -X PATCH -f body=x",
    ],
)
def test_open_only_refuses_the_gh_api_spellings_of_the_second_review(command: str) -> None:
    assert check_open(command) is not None


def test_the_hook_docstring_does_not_write_out_the_refused_list() -> None:
    assert "enforcement.md#pr-guard" in (GUARD.__doc__ or "")
    assert "gh pr merge --auto" not in (GUARD.__doc__ or "")


# --- the third review: the folder's scope, quoted arguments over several lines ----------------------------------------

CONEY = str(Path("C:/work/coney"))


@pytest.mark.parametrize(
    "command",
    [
        "(cd ../tether) && git push origin HEAD:x",
        "(cd C:/work/tether); gh pr create --title t --body-file b",
        "(cd ../tether && git pull); git push origin HEAD:x",
        'bash -c "cd C:/work/tether"; git push origin HEAD:x',
        "echo $(cd ../tether && pwd); git push origin HEAD:x",
        "pushd ../tether && popd && git push origin HEAD:x",
        "popd; git push origin HEAD:x",  # past the first folder: cannot tell
        "cd ../tether && cd - && git push origin HEAD:x",
    ],
)
def test_open_only_ends_a_cd_with_its_subshell_and_popd(command: str) -> None:
    assert check_from(command, CONEY) is not None


@pytest.mark.parametrize(
    "command",
    [
        "(cd ../tether && git push origin HEAD:x)",
        "cd ../tether && (git push origin HEAD:x)",
        'bash -c "cd ../tether && git push origin HEAD:x"',
        "pushd ../tether && git push origin HEAD:x && popd",
        "pushd ../x && popd && cd ../tether && git push origin HEAD:x",
    ],
)
def test_open_only_keeps_a_cd_inside_its_own_subshell(command: str) -> None:
    assert check_from(command, CONEY) is None


def test_a_powershell_location_in_closed_parentheses_cannot_be_told() -> None:
    """PowerShell's parentheses are no subshell: `(Set-Location x)` moves the session."""
    assert check_from("(Set-Location ../x); gh pr create --fill", TETHER, powershell=True) is not None


MULTILINE_BODY = 'gh pr merge 5 --squash --body "line one\nline two"'
HEREDOC_BODY = "gh pr merge 5 --squash --body \"$(cat <<'EOF'\nline one\n\nline two\nEOF\n)\""


@pytest.mark.parametrize("command", [MULTILINE_BODY, HEREDOC_BODY, MULTILINE_BODY + "\ngh pr view 5"])
def test_open_only_reads_a_quoted_argument_over_several_lines(command: str) -> None:
    assert check_open(command) is None


def test_open_only_still_sees_the_command_on_the_next_line() -> None:
    reason = check_open(MULTILINE_BODY + "\ngh pr merge 5 --auto")
    assert reason is not None and "--auto" in reason
    assert check_open("gh pr view 5 # a comment\ngh pr create --title t --body-file b") is not None


@pytest.mark.parametrize(
    "command",
    [
        'gh pr create --title "tools: Add x" --body "line one\nline two"',
        'gh pr create --title "tools: Add x" --body "$(cat <<\'EOF\'\n## Summary\n\nx\nEOF\n)"',
        'gh pr edit 5 --body "line one\nline two"',
    ],
)
def test_a_multi_line_body_is_refused_while_the_switch_is_off(command: str) -> None:
    reason = GUARD.check(command, "", runner=FakeRunner(), open_only=False, repository=here)
    assert reason is not None and "--body" in reason


@pytest.mark.parametrize("command", ["gh pr merge 5 --auto -h=false", "gh pr create -h=0 --title t --body-file b"])
def test_open_only_reads_short_help_by_its_value(command: str) -> None:
    assert check_open(command) is not None


def test_a_quoted_git_dir_in_an_earlier_command_does_not_hide_the_folder() -> None:
    assert check_from('git commit -m "explain --git-dir" && git push origin feat', TETHER) is None


@pytest.mark.parametrize(
    "command",
    [
        "GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=remote.origin.pushurl GIT_CONFIG_VALUE_0=x git push origin HEAD:x",
        "GIT_CONFIG_PARAMETERS=x git push origin HEAD:x",
        "git -c include.path=C:/x.cfg push origin HEAD:x",
        "git -c includeIf.onbranch:x.path=C:/x.cfg push origin HEAD:x",
    ],
)
def test_open_only_refuses_a_push_whose_config_comes_from_the_command(command: str) -> None:
    assert check_from(command, TETHER) is not None


# --- the fourth review: a `#` inside a word is text, not a comment ----------------------------------------------------


@pytest.mark.parametrize(
    ("written", "title"), [('"tools: Fix x (#12)"', "tools: Fix x (#12)"), ("tools:#x", "tools:#x")]
)
def test_a_hash_inside_a_title_is_text_while_the_switch_is_off(body: Path, written: str, title: str) -> None:
    runner = FakeRunner()
    command = f"gh pr create --title {written} --body-file {body.name}"
    assert GUARD.check(command, str(body.parent), runner=runner, open_only=False, repository=here) is None
    assert runner.calls[0] == (["repo", "check-title", "-"], title + "\n")


def test_a_hash_inside_a_word_does_not_hide_the_rest_with_the_switch_on() -> None:
    reason = check_open("gh pr merge 5 --squash --subject a#b --auto")
    assert reason is not None and "--auto" in reason
