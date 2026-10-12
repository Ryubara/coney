# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/issue_guard.py, the Claude Code hook that refuses raw `gh` commands which write an issue.

The script is a standalone, standard-library-only file (the hook runs it on every shell command), so it is loaded by
path. The matcher must block a command only where `gh` starts one; text that merely mentions the command passes.
"""

import importlib.util
import io
import json
from types import ModuleType

import pytest
from conventions_support import REPO

SCRIPT = REPO / ".claude" / "hooks" / "issue_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("issue_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()

BLOCKED = [
    "gh issue create --title x --body y",
    "gh issue create",
    "  gh issue create --title x",
    "cd worktree && gh issue create --title x --body-file f.md",
    "echo done; gh issue create --title x",
    "cat f | gh issue create --title x --body-file -",
    "echo $(gh issue create --title x --body y)",
    "(gh issue create --title x --body y)",
    "FOO=1 gh issue create --title x",
    "gh.exe issue create --title x",
    "& gh issue create --title x --body y",
    "gh issue edit 5 --body-file f.md",
    "gh issue edit 5 --body 'new text'",
    "gh issue edit 5 --body='new text'",
    "gh issue edit 5 -b text",
    "gh issue edit 5 -F f.md",
    "gh issue edit 5 --title t --body-file f.md",
    "gh issue edit 5 \\\n  --body-file f.md",
    "gh issue edit 5 `\n  --body-file f.md",
    "gh api repos/o/r/issues -X POST -f title=x -f body=y",
    "gh api repos/o/r/issues -f title=x -f body=y",
    "gh api repos/{owner}/{repo}/issues --method POST -f title=x",
    "gh api repos/{owner}/{repo}/issues/5 -X PATCH -f body=new",
    "gh api repos/{owner}/{repo}/issues/5 --method PATCH -F body=@f.md",
    "gh api repos/{owner}/{repo}/issues/5 -X PATCH -f title=new",
    "gh api repos/{owner}/{repo}/issues/5/sub_issues -X POST -F sub_issue_id=9",
    "gh api /repos/o/r/issues -X POST --input f.json",
    "gh api graphql -f query='mutation { createIssue(input: {}) { issue { id } } }'",
    "gh api graphql -f query='mutation { updateIssue(input: {}) { issue { id } } }'",
]

ALLOWED = [
    "",
    "ls -la",
    "gh issue view 5 --comments",
    "gh issue list --state open",
    "gh issue edit 5 --add-label A-ai",
    "gh issue edit 5 --remove-label S-needs-triage --add-label S-in-progress",
    "gh issue edit 5 --title 'A better title'",
    "gh issue comment 5 --body 'a comment'",
    "gh issue comment 5 --body-file note.md",
    "gh issue close 5 --reason completed",
    "gh issue reopen 5",
    "gh pr create --title x --body y",
    "gh pr edit 5 --body-file f.md",
    "gh api repos/{owner}/{repo}/issues/5 --jq .id",
    "gh api repos/{owner}/{repo}/issues/5 -X PATCH -f state=closed",
    "gh api repos/{owner}/{repo}/issues/5/comments -f body=hello",
    "gh api repos/{owner}/{repo}/issues/5/labels -f 'labels[]=A-ai'",
    "gh api repos/{owner}/{repo}/issues/comments/77 -X PATCH -f body=fixed",
    "gh api repos/{owner}/{repo}/pulls/5 -X PATCH -f body=x",
    "gh api graphql -f query='query { viewer { login } }'",
    "uv run --project python coney-tools issue create --title x --body f.md",
    "uv run --project python coney-tools issue edit 5 --body f.md",
    "coney-tools issue draft task > body.md",
    'echo "gh issue create is blocked"',
    "echo 'run gh issue edit 5 --body-file f.md instead'",
    'git commit -m "document gh issue create and gh issue edit --body"',
    "grep -rn 'gh issue create' docs",
    'rg "gh issue create" .',
    "cat <<'EOF' > note.md\ngh issue create --title x\ngh issue edit 5 --body-file f\nEOF",
    "cat <<EOF\nuse:\n  gh issue create\nEOF\nls",
    "cat <<-EOF\n\tgh api repos/o/r/issues -X POST\n\tEOF",
    "@'\ngh issue create --title x\n'@ | Set-Content note.md",
    "ghissue create",
    "gh issues create",
]


@pytest.mark.parametrize("command", BLOCKED)
def test_a_command_that_writes_an_issue_is_blocked(command: str) -> None:
    assert GUARD.blocked(command), command


@pytest.mark.parametrize("command", ALLOWED)
def test_a_command_that_only_reads_or_mentions_it_passes(command: str) -> None:
    assert not GUARD.blocked(command), command


def run(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str) -> tuple[int, str]:
    """Run the hook on `stdin`; returns the exit code and what it wrote to stderr."""
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main()
    return code, capsys.readouterr().err


def payload(command: str, tool: str = "Bash") -> str:
    """The JSON Claude Code sends a PreToolUse hook."""
    return json.dumps({"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": {"command": command}})


def test_a_blocked_call_exits_2_and_points_to_coney_tools(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, err = run(monkeypatch, capsys, payload("gh issue create --title x --body y"))
    assert code == 2
    assert "coney-tools issue draft" in err and "issue create" in err and "issue edit" in err


def test_the_powershell_tool_is_checked_the_same_way(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert run(monkeypatch, capsys, payload("gh issue edit 3 --body-file f.md", "PowerShell"))[0] == 2


def test_an_allowed_call_exits_0_silently(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    assert run(monkeypatch, capsys, payload("gh issue view 5")) == (0, "")


@pytest.mark.parametrize(
    "stdin", ["", "not json", "[]", "{}", '{"tool_input": null}', '{"tool_input": {"command": 5}}']
)
def test_input_the_hook_cannot_read_never_blocks(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run(monkeypatch, capsys, stdin) == (0, "")


def test_the_settings_register_the_hook_for_both_shell_tools() -> None:
    settings = json.loads((REPO / ".claude" / "settings.json").read_text(encoding="utf-8"))
    entries = settings["hooks"]["PreToolUse"]
    guard = [entry for entry in entries if "issue_guard.py" in json.dumps(entry)]
    assert len(guard) == 1 and guard[0]["matcher"] == "Bash|PowerShell"
    assert "issue_guard.py" in guard[0]["hooks"][0]["command"] and guard[0]["hooks"][0]["type"] == "command"
