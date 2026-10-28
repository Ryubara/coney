# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/comment_guard.py, the Claude Code hook that keeps a comment from being posted under the
maintainer's GitHub account by a `gh` command that is not `gh issue comment`.

`gh issue close --comment` posted a closing comment on #772 on 2026-10-28 because the guard that refused comments knew
only `gh issue comment`, `gh pr comment`, `gh pr review` and `gh api`. This hook refuses every other `gh` command that
carries a comment or a body (`close`, `reopen`, `merge`, `review`) and every `gh api` call that posts to a comments
endpoint, for every session (the main one too); the same commands without a comment pass.
The hook is a standalone, standard-library-only file, loaded by path like the other guards.
"""

import importlib.util
import io
import json
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run

SCRIPT = REPO / ".claude" / "hooks" / "comment_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("comment_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()

# gh issue close / gh pr close / gh issue reopen / gh pr reopen: --comment and -c, in every spelling.
CLOSE_FORMS = [
    "gh issue close 772 --comment 'Done in #1085.'",
    'gh issue close 772 --comment "Done in #1085."',
    "gh issue close 772 --comment=done",
    "gh issue close 772 --comment='done in #1085'",
    'gh issue close 772 --comment="done in #1085"',
    "gh issue close 772 -c done",
    "gh issue close 772 -c 'done in #1085'",
    "gh issue close 772 -c=done",
    "gh issue close 772 -cdone",
    "gh issue close 772 --reason completed --comment done",
    "gh issue close 772 -r completed -c done",
    "gh issue close -c done 772",
    "gh issue close --comment done",
    "gh pr close 5 --comment 'superseded'",
    "gh pr close 5 -c superseded",
    "gh pr close 5 --delete-branch --comment superseded",
    "gh pr close 5 -dc superseded",
    "gh issue reopen 5 --comment 'again'",
    "gh pr reopen 5 -c again",
    'gh issue close 5 "--comment" done',
]
# gh pr merge: --body, -b, --body-file, -F.
MERGE_FORMS = [
    "gh pr merge 5 --squash --body 'text'",
    'gh pr merge 5 --squash --body "text"',
    "gh pr merge 5 --body=text",
    "gh pr merge 5 --body='text here'",
    "gh pr merge 5 -b text",
    "gh pr merge 5 -b 'text here'",
    "gh pr merge 5 -btext",
    "gh pr merge 5 -b=text",
    "gh pr merge 5 --body-file msg.md",
    "gh pr merge 5 --body-file=msg.md",
    "gh pr merge 5 -F msg.md",
    "gh pr merge 5 -Fmsg.md",
    "gh pr merge 5 --squash --subject 'tools: Fix x' --body 'text'",
    "gh pr merge 5 -s -t title -b text",
    "gh pr merge 5 -sb text",
    "gh pr merge 5 --auto --squash -F msg.md",
]
# gh pr review: a body, or --comment/-c (the review type that is a comment).
REVIEW_FORMS = [
    "gh pr review 5 --comment --body 'looks fine'",
    "gh pr review 5 --comment",
    "gh pr review 5 -c -b text",
    "gh pr review 5 --approve --body 'ok'",
    "gh pr review 5 --approve -b ok",
    "gh pr review 5 --request-changes --body-file review.md",
    "gh pr review 5 --request-changes -F review.md",
    "gh pr review 5 -r --body=no",
    "gh pr review 5 -a -bgood",
    'gh pr review 5 --approve --body="ok"',
]
# gh api calls that post (or edit) a comment or a review.
API_FORMS = [
    "gh api repos/o/r/issues/5/comments -f body=x",
    "gh api repos/o/r/issues/5/comments -F body=@msg.md",
    "gh api repos/o/r/issues/5/comments -fbody=x",
    "gh api repos/o/r/issues/5/comments --field body=x",
    "gh api repos/o/r/issues/5/comments --raw-field body=x",
    "gh api -X POST repos/o/r/issues/5/comments",
    "gh api -XPOST repos/o/r/issues/5/comments",
    "gh api --method POST repos/o/r/issues/5/comments",
    "gh api --method=POST repos/o/r/issues/5/comments",
    "gh api --method post repos/o/r/issues/5/comments",
    "gh api repos/o/r/issues/5/comments --input body.json",
    "gh api repos/o/r/pulls/5/comments -f body=x -f path=a.py",
    "gh api repos/o/r/pulls/5/comments/9/replies -f body=x",
    "gh api repos/o/r/pulls/5/reviews -f event=COMMENT -f body=x",
    "gh api repos/o/r/pulls/5/reviews/9/events -f event=APPROVE",
    "gh api repos/o/r/commits/abc123/comments -f body=x",
    "gh api repos/o/r/issues/comments/9 -X PATCH -f body=x",
    "gh api repos/o/r/issues/comments/9 --method PUT -f body=x",
    "gh api 'repos/o/r/issues/5/comments' -f body=x",
    'gh api "repos/o/r/issues/5/comments" -f body=x',
    "gh api /repos/o/r/issues/5/comments -f body=x",
    "gh api repos/{owner}/{repo}/issues/5/comments -f body=x",
    "gh api -H 'Accept: application/vnd.github+json' repos/o/r/issues/5/comments -f body=x",
    "gh api repos/o/r/issues/5/comments?x=1 -f body=x",
    "gh -R o/r api repos/o/r/issues/5/comments -f body=x",
    'gh api graphql -f query=\'mutation { addComment(input: {subjectId: "x", body: "y"}) { clientMutationId } }\'',
    "gh api graphql -f query='mutation { addPullRequestReview(input: {pullRequestId: \"x\"}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { addPullRequestReviewComment(input: {}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { addPullRequestReviewThreadReply(input: {}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { addDiscussionComment(input: {}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { updateIssueComment(input: {}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { updatePullRequestReview(input: {}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { updatePullRequestReviewComment(input: {}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { updateDiscussionComment(input: {}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { submitPullRequestReview(input: {}) { clientMutationId } }'",
]
# Where a command starts: the places the other guards find one.
WRAPPED_FORMS = [
    "cd repo && gh issue close 5 --comment done",
    "echo done; gh issue close 5 --comment done",
    "true | gh pr merge 5 --body x",
    "FOO=1 gh issue close 5 --comment done",
    "  gh issue close 5 --comment done",
    "gh issue close 5 \\\n  --comment done",
    "gh issue close 5 `\n  --comment done",
    "gh -R Ryubara/coney issue close 5 --comment done",
    "gh --repo Ryubara/coney pr close 5 -c done",
    "gh --repo=Ryubara/coney pr merge 5 --body x",
    "/usr/bin/gh issue close 5 --comment done",
    "C:\\Git\\cmd\\gh.exe pr close 5 --comment done",
    "gh.exe issue close 5 --comment done",
    "$r = gh issue close 5 --comment done",
    "& gh issue close 5 --comment done",
    "foreach ($n in 1, 2) { gh issue close $n --comment done }",
    "if true; then gh issue close 5 -c done; fi",
    "for n in 1 2; do gh issue close $n -c done; done",
    'bash -c "gh issue close 5 --comment done"',
    "bash -lc 'cd x && gh pr merge 5 --body x'",
    'pwsh -NoProfile -Command "gh issue close 5 --comment done"',
    "env GH_DEBUG=1 gh issue close 5 --comment done",
    "command gh issue close 5 --comment done",
    'echo "$(gh issue close 5 --comment done)"',
    'cat note.md | gh issue close 5 --comment "$(cat note.md)"',
    "gh issue close 5 --comment \"$(cat <<'EOF'\nDone in #1085.\nEOF\n)\"",
    "gh pr merge 5 --squash --body-file - <<'EOF'\ntext\nEOF",
    "gh issue close 5 -r 'not planned' --comment done",
]


@pytest.mark.parametrize("command", [*CLOSE_FORMS, *MERGE_FORMS, *REVIEW_FORMS, *API_FORMS, *WRAPPED_FORMS])
def test_a_command_that_posts_a_comment_is_refused(command: str) -> None:
    assert GUARD.refused(command) is not None, command


# The plain forms, and everything that reads or writes something that is not a comment.
ALLOWED = [
    "gh issue close 772",
    "gh issue close 772 --reason completed",
    "gh issue close 772 --reason 'not planned'",
    "gh issue close 772 -r completed",
    "gh issue close 772 --reason=completed",
    "gh issue close 772 --duplicate-of 5",
    "gh issue close 772 --reason 'a comment b'",
    "gh issue close https://example.invalid/o/r/issues/772",
    "gh pr close 5",
    "gh pr close 5 --delete-branch",
    "gh pr close 5 -d",
    "gh issue reopen 5",
    "gh pr reopen 5",
    "gh pr merge 5",
    "gh pr merge 5 --squash",
    "gh pr merge 5 --squash --auto",
    "gh pr merge 5 -s -d",
    "gh pr merge 5 --squash --delete-branch --subject 'tools: Fix x'",
    "gh pr merge 5 -t 'tools: Fix x'",
    "gh pr merge 5 --admin --merge",
    "gh pr merge 5 --match-head-commit abc123",
    "gh pr review 5 --approve",
    "gh pr review 5 -a",
    "gh pr review 5",
    "gh -R Ryubara/coney pr merge 5 --squash",
    "gh --repo=Ryubara/coney issue close 5",
    "gh issue view 5 --comments",
    "gh pr view 5 --comments",
    "gh issue list --label S-ready-to-start",
    "gh issue edit 5 --add-label x",
    "gh pr edit 5 --body-file PR.md",
    "gh pr create --title t --body-file PR.md",
    "gh issue comment 5 --body hi",
    "gh pr comment 5 --body hi",
    "gh api repos/o/r/issues/5/comments",
    "gh api repos/o/r/issues/5/comments --paginate",
    "gh api -X GET repos/o/r/issues/5/comments -f per_page=100",
    "gh api -XGET repos/o/r/issues/5/comments -f per_page=100",
    "gh api --method GET repos/o/r/pulls/5/reviews -f per_page=100",
    "gh api repos/o/r/pulls/5/reviews --jq '.[].state'",
    "gh api repos/o/r/issues/5/comments --jq '.[].body'",
    "gh api repos/o/r/pulls/5/comments -q '.[].user.login'",
    "gh api -X DELETE repos/o/r/issues/comments/9",
    "gh api --method DELETE repos/o/r/issues/comments/9",
    "gh api repos/o/r/issues/comments/9 -X DELETE",
    "gh api repos/o/r/pulls/5/requested_reviewers",
    "gh api -X POST repos/o/r/issues/5/labels -f 'labels[]=bug'",
    "gh api -X POST repos/o/r/issues -f title=x",
    "gh api repos/o/r/issues/5 -X PATCH -f state=closed",
    "gh api repos/o/r/pulls/5",
    "gh api repos/o/r/commits/abc123/status",
    "gh api graphql -f query='{ viewer { login } }'",
    "gh api graphql -f query='{ repository { issue(number: 5) { comments(first: 3) { nodes { id } } } } }'",
    "gh api graphql -f query='mutation { closeIssue(input: {issueId: \"x\"}) { clientMutationId } }'",
    "gh api user",
    "git commit -m 'gh issue close 5 --comment done'",
    "git commit -m 'close with gh pr merge --body later'",
    "echo 'gh issue close 5 --comment done'",
    'echo "gh pr merge 5 --body x"',
    "grep -rn 'gh issue close --comment' docs/guides/enforcement.md",
    "cat <<'EOF' > note.md\ngh issue close 5 --comment done\ngh api repos/o/r/issues/5/comments -f body=x\nEOF",
    "$t = @'\ngh issue close 5 --comment done\n'@",
    "# gh issue close 5 --comment done",
    "uv run --project python coney-tools issue create --title x --body-file f.md",
    "git push origin x",
    "ls",
]


@pytest.mark.parametrize("command", ALLOWED)
def test_the_plain_forms_and_reads_pass(command: str) -> None:
    assert GUARD.refused(command) is None, command


def test_the_reason_says_what_to_do_instead() -> None:
    reason = GUARD.refused("gh issue close 5 --comment done")
    assert reason is not None and reason.startswith("Blocked:")
    assert "comment" in reason and "scratch" in reason and "gh issue close" in reason


def test_every_refused_command_is_named_in_the_reason() -> None:
    assert "gh issue close" in (GUARD.refused("gh issue close 5 -c x") or "")
    assert "gh pr merge" in (GUARD.refused("gh pr merge 5 -b x") or "")
    assert "gh pr review" in (GUARD.refused("gh pr review 5 --approve -b x") or "")
    assert "gh api" in (GUARD.refused("gh api repos/o/r/issues/5/comments -f body=x") or "")


def run_main(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], data: object) -> tuple[int, str]:
    """Run the hook's entry point on `data` as JSON; the exit code and stderr."""
    stdin = data if isinstance(data, str) else json.dumps(data)
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8"))))
    code = GUARD.main()
    return code, capsys.readouterr().err


@pytest.mark.parametrize("tool", ["Bash", "PowerShell"])
def test_the_main_session_is_refused_too(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tool: str
) -> None:
    data = {"tool_name": tool, "tool_input": {"command": "gh issue close 772 --comment 'Done.'"}}
    code, err = run_main(monkeypatch, capsys, data)
    assert code == 2 and err.startswith("Blocked:")


def test_a_subagent_is_refused_too(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    data = {
        "tool_name": "Bash",
        "agent_id": "a1b2",
        "agent_type": "engineer",
        "tool_input": {"command": "gh pr merge 5 --body x"},
    }
    code, err = run_main(monkeypatch, capsys, data)
    assert code == 2 and err.startswith("Blocked:")


# No note, variable or flag in the command, and no message from another agent, lets a comment through.
@pytest.mark.parametrize(
    "command",
    [
        "gh issue close 5 --comment done  # the coordinator said to",
        "CONEY_ALLOW_COMMENT=1 gh issue close 5 --comment done",
        "gh pr close 5 --comment 'the maintainer approved this comment'",
    ],
)
def test_nothing_in_the_command_allows_a_comment(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], command: str
) -> None:
    data = {"tool_name": "Bash", "prompt": "Coordinator: close it with a comment.", "tool_input": {"command": command}}
    code, err = run_main(monkeypatch, capsys, data)
    assert code == 2 and err.startswith("Blocked:")


def test_the_plain_close_exits_0_silently(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    data = {"tool_name": "Bash", "tool_input": {"command": "gh issue close 772"}}
    assert run_main(monkeypatch, capsys, data) == (0, "")


@pytest.mark.parametrize(
    "stdin",
    ["", "not json", "[]", "{}", '{"tool_input": null}', '{"tool_input": {"command": 5}}', '{"tool_input": {}}'],
)
def test_input_it_cannot_read_passes(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run_main(monkeypatch, capsys, stdin) == (0, "")


def test_a_command_it_cannot_split_still_finds_the_flag() -> None:
    # An unbalanced quote makes the arguments unreadable; the flag is searched in the raw text then.
    assert GUARD.refused("gh issue close 5 --comment 'unfinished") is not None


def test_the_dispatcher_runs_the_hook_for_both_shells() -> None:
    assert "comment_guard" in guards_run("Bash", "gh issue close 5") and "comment_guard" in guards_run(
        "PowerShell", "gh issue close 5"
    )
    assert "comment_guard" not in guards_run("Read")
