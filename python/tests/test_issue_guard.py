# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/issue_guard.py, the Claude Code hook that refuses a raw `gh` call filing an issue.

The script is a standalone, standard-library-only file (the hook runs it on every shell command), so it is loaded by
path. The matcher must block a command only where `gh` starts an issue write; reading, commenting, labelling and
closing pass, and so does a command that names only some other repository.
"""

import importlib.util
import io
import json
import subprocess
from pathlib import Path
from types import ModuleType

import pytest
from conventions_support import REPO

SCRIPT = REPO / ".claude" / "hooks" / "issue_guard.py"
#: The repository the tests pretend the working directory belongs to.
HERE = "example/project"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("issue_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()

BLOCKED = [
    "gh issue create --title x",
    "gh issue create --title x --body-file note.md",
    "  gh issue create -t x",
    "cd repo && gh issue create --title x",
    "echo done; gh issue create --title x",
    "FOO=1 gh issue create --title x",
    "gh issue edit 5 --body-file note.md",
    "gh issue edit 5 --body 'text'",
    "gh issue edit 5 -F note.md",
    "gh issue edit 5 --add-label bug --body-file note.md",
    "gh api repos/example/project/issues -X POST -f title=x",
    "gh api repos/example/project/issues/5 --method PATCH -f body=x",
    "gh api repos/example/project/issues -f title=x",
    "gh issue create --repo example/project --title x",
    "gh issue create -R example/project --title x",
    "GH_REPO=example/project gh issue create --title x",
    "gh issue create \\\n  --title x",
    "gh issue create `\n  --title x",
]

ALLOWED = [
    "",
    "gh issue view 5 --comments",
    "gh issue list --state open",
    "gh issue close 5",
    "gh issue reopen 5",
    "gh issue comment 5 --body 'text'",
    "gh issue edit 5 --add-label bug",
    "gh issue edit 5 --title 'a title'",
    "gh api repos/example/project/issues/5",
    "gh api repos/example/project/issues/5/comments -f body=x",
    "gh pr create --title x --body-file note.md",
    "uv run --project python coney-tools issue create --title x --body note.md",
    'echo "gh issue create --title x"',
    "echo 'gh issue edit 5 --body-file f'",
    'git commit -m "docs: use coney-tools issue, not gh issue create"',
    "grep -rn 'gh issue create' docs",
    "cat <<'EOF' > note.md\ngh issue create --title x\nEOF",
    "cat <<EOF\nuse:\n  gh issue edit 5 --body-file f\nEOF\nls",
    "@'\ngh issue create --title x\n'@ | Set-Content note.md",
    "mygh issue create --title x",
    # Another repository: the rule is this repository's own.
    "gh issue create -R other/scratch --title x",
    "gh issue create --repo other/scratch --title x",
    "gh issue create --repo=other/scratch --title x",
    "GH_REPO=other/scratch gh issue create --title x",
    "gh issue edit 3 --repo other/scratch --body-file f",
    "gh api repos/other/scratch/issues -X POST -f title=x",
]

#: Naming this repository, even next to another one, is still this repository's issue.
NAMES_THIS_ONE = [
    "gh issue create -R Example/Project --title x",
    "gh issue create -R other/scratch -R example/project --title x",
    "gh api repos/other/scratch/issues -X POST -f title=x && gh api repos/example/project/issues -X POST -f title=y",
]


@pytest.fixture(autouse=True)
def this_repository(monkeypatch: pytest.MonkeyPatch) -> None:
    """The working directory belongs to `HERE`."""
    monkeypatch.setattr(GUARD, "this_repository", lambda cwd: HERE)


@pytest.mark.parametrize("command", BLOCKED + NAMES_THIS_ONE)
def test_a_raw_issue_write_is_blocked(command: str) -> None:
    assert GUARD.blocked(command, "."), command


@pytest.mark.parametrize("command", ALLOWED)
def test_reading_commenting_and_other_repositories_pass(command: str) -> None:
    assert not GUARD.blocked(command, "."), command


def test_a_repository_that_cannot_be_told_counts_as_this_one(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(GUARD, "this_repository", lambda cwd: None)
    assert GUARD.blocked("gh issue create -R other/scratch --title x", ".")


@pytest.mark.parametrize(
    ("url", "expected"),
    [
        ("https://github.com/Example/Project.git\n", "example/project"),
        ("https://github.com/example/project", "example/project"),
        ("git@github.com:example/project.git\n", "example/project"),
        ("https://user@github.com/example/project/", "example/project"),
    ],
)
def test_the_repository_is_read_from_the_origin_remote(
    monkeypatch: pytest.MonkeyPatch, url: str, expected: str
) -> None:
    monkeypatch.undo()
    done = subprocess.CompletedProcess([], 0, stdout=url, stderr="")
    monkeypatch.setattr(subprocess, "run", lambda *args, **kwargs: done)
    assert GUARD.this_repository(".") == expected


def test_no_origin_remote_gives_no_repository(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.undo()
    done = subprocess.CompletedProcess([], 2, stdout="", stderr="no such remote")
    monkeypatch.setattr(subprocess, "run", lambda *args, **kwargs: done)
    assert GUARD.this_repository(".") is None

    def missing(*args: object, **kwargs: object) -> None:
        raise FileNotFoundError("git")

    monkeypatch.setattr(subprocess, "run", missing)
    assert GUARD.this_repository(".") is None


def run(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str) -> tuple[int, str]:
    """Run the hook on `stdin`; returns the exit code and what it wrote to stderr."""
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main()
    return code, capsys.readouterr().err


def payload(command: str, tool: str = "Bash") -> str:
    """The JSON Claude Code sends a PreToolUse hook."""
    return json.dumps(
        {"hook_event_name": "PreToolUse", "tool_name": tool, "cwd": str(Path.cwd()), "tool_input": {"command": command}}
    )


def test_a_blocked_call_exits_2_and_names_the_replacement(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, err = run(monkeypatch, capsys, payload("gh issue create --title x"))
    assert code == 2
    assert "coney-tools issue" in err and "issue draft" in err


def test_the_powershell_tool_is_checked_the_same_way(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert run(monkeypatch, capsys, payload("gh issue create --title x", "PowerShell"))[0] == 2


def test_a_comment_exits_0_silently(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    assert run(monkeypatch, capsys, payload("gh issue comment 5 --body hi")) == (0, "")


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
    commands = [hook["command"] for hook in guard[0]["hooks"]]
    assert any("issue_guard.py" in command for command in commands)
