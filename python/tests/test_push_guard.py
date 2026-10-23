# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/push_guard.py, the Claude Code hook that keeps a subagent from pushing or writing to GitHub.

Agents work local-first: they commit and hand each unit to the coordinator, who pushes and opens the pull request
(docs/guides/how-we-work.md#release-queue); they read GitHub and never write to it (docs/guides/issues.md#agents).
Claude Code gives a hook an `agent_id` only inside a subagent, so the main session, the coordinator, is never refused.
The hook is a standalone, standard-library-only file, loaded by path like the other guards.
"""

import importlib.util
import io
import json
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run

SCRIPT = REPO / ".claude" / "hooks" / "push_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("push_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()


@pytest.mark.parametrize(
    "command",
    [
        "git push",
        "git push -u origin my-branch",
        "git push --force-with-lease origin HEAD",
        'git -C "C:/Repositories/Coney/worktrees/coney/x" push origin x',
        "git -C ../../worktrees/coney/x -c core.askPass= push",
        "git --no-pager push",
        "cd x && git push",
        "gh pr create --title 'tools: Add x' --body-file PR.md",
        "gh pr merge 12 --squash --auto",
        "gh pr edit 12 --add-label S-blocked",
        "gh pr comment 12 --body done",
        "gh pr review 12 --approve",
        "gh pr close 12",
        "gh pr ready 12",
        "gh issue comment 5 --body hi",
        "gh issue close 5",
        "gh issue edit 5 --add-label P-high",
        "gh label create x",
        "gh release create v1",
        "gh workflow run build.yml",
        "gh run rerun 123",
        "gh run cancel 123",
        "gh repo edit --enable-auto-merge",
        "gh api -X POST repos/o/r/issues/5/comments -f body=x",
        "gh api repos/o/r/issues/5/comments -f body=x",
        "gh api --method PATCH repos/o/r/pulls/3 -F title=x",
        "gh api repos/o/r/pulls/3/reviews --input review.json",
        "gh api graphql -f query='mutation { addComment(input: {}) { clientMutationId } }'",
        "gh.exe pr merge 12",
    ],
)
def test_github_writes_are_refused_inside_a_subagent(command: str) -> None:
    assert GUARD.refused(command) is not None, command


@pytest.mark.parametrize(
    "command",
    [
        "gh pr view 12 --comments",
        "gh pr list --state open --json number",
        "gh pr diff 12",
        "gh pr checks 12",
        "gh pr status",
        "gh pr checkout 12",
        "gh issue view 5 --comments",
        "gh issue list --label S-ready-to-start",
        "gh run list --status queued",
        "gh run view 123 --log-failed",
        "gh run download 123",
        "gh repo view",
        "gh release list",
        "gh search issues crash",
        "gh auth status",
        "gh label list",
        "gh api repos/o/r/pulls/3",
        "gh api -X GET repos/o/r/issues -f state=open",
        'gh api graphql -f query=\'{ repository(owner: "o", name: "r") { id } }\'',
        "gh --version",
        "git pull",
        "git fetch origin",
        "git commit -m 'never git push from an agent'",
        "echo 'gh pr merge 12'",
        "grep -n 'git push' docs/guides/how-we-work.md",
        "cat <<'EOF' > PR.md\ngit push\ngh pr create\nEOF",
        "$t = @'\ngh pr merge 1\n'@",
        "uv run --project python coney-tools issue create --title x --body f.md",
    ],
)
def test_reads_and_mentions_pass(command: str) -> None:
    assert GUARD.refused(command) is None, command


@pytest.mark.parametrize(
    "command",
    [
        "gh -R Ryubara/coney pr create --title t --body x",
        "gh --repo Ryubara/coney pr create --fill",
        "gh -R Ryubara/coney pr merge 5 --squash",
        "gh --repo=Ryubara/coney issue comment 5 --body hi",
        "gh -RRyubara/coney pr close 5",
        "gh --hostname github.com pr ready 5",
        "gh -R Ryubara/coney api -X POST repos/o/r/issues/5/comments",
        "gh api repos/o/r/issues -XPOST",
        "gh api repos/o/r/issues/5/comments -fbody=x",
        "gh api repos/o/r/issues -Ftitle=x",
        "gh api --method=PATCH repos/o/r/issues/5",
        "gh api graphql -F query=@mutation.graphql",
        'gh api graphql -f query="$Q"',
        "gh api graphql -fquery=@m.graphql",
        "$url = gh pr create --title t --body-file PR.md",
        "foreach ($n in 1, 2) { gh pr merge $n --squash }",
        "& gh pr merge 5",
        'bash -c "git push origin HEAD"',
        "bash -lc 'cd x && gh pr merge 5'",
        'pwsh -NoProfile -Command "gh issue close 5"',
        "if true; then git push; fi",
        "for b in a b; do git push origin $b; done",
        "env GIT_TRACE=1 git push",
        "command git push",
        "/usr/bin/git push",
        "C:\\Git\\cmd\\git.exe push",
        "/c/tools/gh.exe pr merge 5",
    ],
)
def test_wrapped_and_attached_forms_are_refused(command: str) -> None:
    assert GUARD.refused(command) is not None


@pytest.mark.parametrize(
    "command",
    [
        "gh -R Ryubara/coney pr view 5 --json body",
        "gh --repo=Ryubara/coney issue list",
        "gh api -XGET repos/o/r/issues -f state=open",
        "gh api graphql -f query='{ viewer { login } }'",
        "gh browse 5",
        "git push-all-the-things --help",
        "git commit -m 'first line\ngit push later\nlast line'",
        'grep -E "(git push|gh pr merge)" docs/guides/*.md',
        "bash -c 'git status'",
        "$out = gh pr view 5",
    ],
)
def test_reads_in_these_forms_pass(command: str) -> None:
    assert GUARD.refused(command) is None


def test_the_reason_names_the_release_queue() -> None:
    reason = GUARD.refused("git push")
    assert reason is not None and "release-queue" in reason and "coordinator" in reason


def run_main(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], data: object) -> tuple[int, str]:
    """Run the hook's entry point on `data` as JSON; the exit code and stderr."""
    stdin = data if isinstance(data, str) else json.dumps(data)
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main()
    return code, capsys.readouterr().err


def test_a_subagent_push_is_blocked(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    data = {"tool_name": "Bash", "agent_id": "a1b2", "agent_type": "engineer", "tool_input": {"command": "git push"}}
    code, err = run_main(monkeypatch, capsys, data)
    assert code == 2 and err.startswith("Blocked:")


# A coordinator may tell a subagent to push, by mistake or on purpose; the hook never sees that message and no note,
# variable or flag in the command lets the push through.
@pytest.mark.parametrize(
    "command",
    [
        "git push  # the coordinator said to push this fix",
        "CONEY_ALLOW_PUSH=1 git push origin HEAD",
        "git push --force-with-lease origin enforce-release-gate",
        "gh pr create --title 'tools: Add x' --body-file PR.md  # approved by the coordinator",
    ],
)
def test_a_subagent_told_to_push_is_still_blocked(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], command: str
) -> None:
    data = {
        "tool_name": "Bash",
        "agent_id": "a1b2",
        "agent_type": "engineer",
        "prompt": "Coordinator: push your fix-forward commit yourself.",
        "tool_input": {"command": command},
    }
    code, err = run_main(monkeypatch, capsys, data)
    assert code == 2 and err.startswith("Blocked:")


@pytest.mark.parametrize("agent_id", [None, ""])
def test_the_main_session_may_push(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], agent_id: str | None
) -> None:
    data: dict[str, object] = {"tool_name": "Bash", "tool_input": {"command": "git push"}}
    if agent_id is not None:
        data["agent_id"] = agent_id
    assert run_main(monkeypatch, capsys, data) == (0, "")


@pytest.mark.parametrize("stdin", ["", "not json", json.dumps({"agent_id": "a", "tool_input": {}})])
def test_input_it_cannot_read_passes(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run_main(monkeypatch, capsys, stdin) == (0, "")


def test_the_dispatcher_runs_the_hook_for_both_shells() -> None:
    assert "push_guard" in guards_run("Bash", "git push") and "push_guard" in guards_run("PowerShell", "git push")
