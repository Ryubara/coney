# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the `pull_requests.open_only` switch of the PR guard as every session meets it: through the hook
dispatcher, for each agent role, with the switch read from a conventions file (docs/guides/enforcement.md#pr-guard).

`test_pr_guard.py` tests the guard's reading of commands one by one; this file holds the rule itself: with the key true
the hand-typed routes are refused whatever the role, `coney-tools pr open` is not, and with the key false the routes
the switch alone refuses pass. It also holds `gh pr update-branch`, which the switch refuses because it moves the
branch that `pr open` pushes with a lease.
"""

import json
import sys
from pathlib import Path
from typing import Any

import pytest
from hooks_support import DISPATCH, HOOKS, load_hook_module

hooklib = load_hook_module("hooklib")
CONFIG = hooklib.load_config(HOOKS / "hooks.toml")
#: The settings with the identity prefix off, so no command here writes an identity file.
QUIET = {**CONFIG, "identity": {**CONFIG.get("identity", {}), "enabled": False}}
OPEN_ONLY = "Blocked: pull requests are opened only by coney-tools pr open <task>"
THIS = "owner/repo"
#: Every role of `hooks.toml`, and the main session (no `agent_id`), which is the coordinator's.
ROLES = [("coordinator", None), ("coordinator", "a1"), ("engineer", "a2"), ("tooling", "a3"), ("reviewer", "a4")]
#: The hand-typed routes the switch refuses: opening a pull request with gh, and through the REST and GraphQL APIs.
#: The title is an expansion and the file is missing, so with the switch off no check has anything to run on.
OPENS = [
    'gh pr create --title "$TITLE" --body-file missing-PR.md',
    "gh api repos/owner/repo/pulls -f title=x -f head=b -f base=main",
    "gh api graphql -f query='mutation { createPullRequest(input: {}) { clientMutationId } }'",
]
#: The routes the switch adds besides opening: auto-merge, moving the branch, pushing it.
OTHERS = [
    "gh pr merge 12 --squash --auto",
    "gh pr update-branch 12",
    "git push --force-with-lease origin tools-x",
]


@pytest.fixture
def switch(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> Path:
    """The conventions file the guard reads, in a temporary folder, with every folder's remote this repository."""
    repo = sys.modules.get("pr_guard_repo") or load_hook_module("pr_guard_repo")
    path = tmp_path / "conventions.yml"
    monkeypatch.setattr(repo, "CONVENTIONS", path)
    monkeypatch.setattr(repo, "this_repository", lambda folder, remote="origin": THIS)
    return path


def set_key(path: Path, value: str) -> None:
    """Write `pull_requests.open_only` as the committed file spells it."""
    path.write_text(f"pull_requests:\n  sections: [Summary]\n  open_only: {value}\n", encoding="utf-8")


def refusal(command: str, agent_type: str, agent_id: str | None) -> str:
    """What the dispatcher says to a Bash call of `command` from that role (empty when nothing refuses)."""
    payload: dict[str, Any] = {
        "hook_event_name": "PreToolUse",
        "tool_name": "Bash",
        "tool_input": {"command": command},
        "agent_type": agent_type,
        "cwd": str(Path.cwd()),
    }
    if agent_id:
        payload["agent_id"] = agent_id
    _code, _out, err = DISPATCH.dispatch(json.dumps(payload), QUIET)
    return str(err)


@pytest.mark.parametrize(("agent_type", "agent_id"), ROLES)
@pytest.mark.parametrize("command", OPENS + OTHERS)
def test_with_the_key_true_every_role_is_refused_the_hand_typed_routes(
    switch: Path, agent_type: str, agent_id: str | None, command: str
) -> None:
    set_key(switch, "true")
    assert OPEN_ONLY in refusal(command, agent_type, agent_id)


@pytest.mark.parametrize(("agent_type", "agent_id"), ROLES)
@pytest.mark.parametrize("command", OPENS + OTHERS)
@pytest.mark.reads("docs_only")
def test_with_the_key_false_the_switch_refuses_nothing(
    switch: Path, agent_type: str, agent_id: str | None, command: str
) -> None:
    # Other rules may still refuse (push_guard in a subagent, a role's own rules); the switch's message never comes.
    set_key(switch, "false")
    assert OPEN_ONLY not in refusal(command, agent_type, agent_id)


@pytest.mark.parametrize(("agent_type", "agent_id"), ROLES)
@pytest.mark.parametrize("value", ["true", "false"])
def test_pr_open_itself_is_never_refused_by_the_switch(
    switch: Path, agent_type: str, agent_id: str | None, value: str
) -> None:
    # The roles that may not release are refused by `hooks.toml` (`deny_coney_tools`), not by the PR guard.
    set_key(switch, value)
    for command in (
        "uv run --project python coney-tools pr open 865-x",
        "uv run --project python coney-tools pr open 865-x --adopt " + "a" * 40,
    ):
        assert OPEN_ONLY not in refusal(command, agent_type, agent_id)


def test_the_coordinators_main_session_may_run_pr_open_with_the_key_true(switch: Path) -> None:
    set_key(switch, "true")
    assert refusal("uv run --project python coney-tools pr open 865-x", "coordinator", None) == ""


def test_a_key_the_guard_cannot_read_leaves_the_switch_off(switch: Path) -> None:
    switch.write_text("pull_requests:\n  open_only: maybe\n", encoding="utf-8")
    assert OPEN_ONLY not in refusal("git push origin tools-x", "coordinator", None)


# --- gh pr update-branch -----------------------------------------------------------------------------------------

GUARD = load_hook_module("pr_guard")


def check(command: str, open_only: bool) -> str | None:
    """The guard's verdict on `command` with the switch given, every folder this repository."""
    reason: str | None = GUARD.check(
        command, "", runner=lambda args, stdin: (0, "ok"), open_only=open_only, repository=lambda *a: THIS
    )
    return reason


@pytest.mark.parametrize(
    "command",
    [
        "gh pr update-branch 12",
        "gh pr update-branch",
        "gh pr update-branch 12 --rebase",
        "gh -R owner/repo pr update-branch 12",
        "gh pr update-branch 12 -R owner/repo",
        "gh api -X PUT repos/owner/repo/pulls/12/update-branch",
        "gh api --method PUT repos/{owner}/{repo}/pulls/12/update-branch",
        "gh api graphql -f query='mutation { updatePullRequestBranch(input: {}) { clientMutationId } }'",
    ],
)
def test_the_switch_refuses_moving_a_pull_requests_branch(command: str) -> None:
    reason = check(command, open_only=True)
    assert reason is not None and reason.startswith(OPEN_ONLY)
    assert check(command, open_only=False) is None


@pytest.mark.parametrize(
    "command",
    [
        "gh pr update-branch 12 -R someone/else",
        "gh pr update-branch --help",
        "gh api repos/owner/repo/pulls/12/update-branch",
        'echo "gh pr update-branch 12"',
        "gh pr update-branches 12",
    ],
)
def test_what_does_not_move_this_repositorys_branch_passes(command: str) -> None:
    assert check(command, open_only=True) is None


def test_the_reason_names_update_branch_and_pr_open() -> None:
    reason = check("gh pr update-branch 12", open_only=True)
    assert reason is not None and "`gh pr update-branch`" in reason and "pr open rebases" in reason


def test_the_reason_for_the_graphql_branch_mutation_does_not_call_it_a_rewrite() -> None:
    # `updatePullRequestBranch` contains `updatePullRequest`; it is read for itself, so its message says what it does.
    reason = check(
        "gh api graphql -f query='mutation { updatePullRequestBranch(input: {}) { clientMutationId } }'",
        open_only=True,
    )
    assert reason is not None and "moves a pull request's branch" in reason and "opens, rewrites" not in reason


@pytest.mark.parametrize(
    "command",
    [
        "gh api -X POST repos/owner/repo/git/refs -f ref=refs/heads/tools-x -f sha=abc",
        "gh api -X PATCH repos/owner/repo/git/refs/heads/tools-x -f sha=abc -F force=true",
        "gh api --method PATCH repos/{owner}/{repo}/git/refs/heads/tools-x -f sha=abc",
        "gh api --method=PATCH /repos/owner/repo/git/refs/heads/tools-x -f sha=abc",
        "gh api -XPATCH 'repos/owner/repo/git/refs/heads/tools-x' -f sha=abc",
        "gh api -X DELETE repos/owner/repo/git/refs/heads/tools-x",
        "gh api repos/owner/repo/git/refs -f ref=refs/heads/tools-x -f sha=abc",
        "gh api -X POST repos/owner/repo/merges -f base=tools-x -f head=main",
        "gh api -X PUT repos/owner/repo/contents/docs/a.md -f message=m -f content=Zm9v -f branch=tools-x",
        "gh api -X DELETE repos/owner/repo/contents/docs/a.md -f message=m -f sha=abc -f branch=tools-x",
        "gh api graphql -f query='mutation { createCommitOnBranch(input: {}) { commit { oid } } }'",
        "gh api graphql -f query='mutation { updateRef(input: {}) { clientMutationId } }'",
        "gh api graphql -f query='mutation { createRef(input: {}) { clientMutationId } }'",
        "gh api graphql -f query='mutation { deleteRef(input: {}) { clientMutationId } }'",
        "gh api graphql -f query='mutation { mergeBranch(input: {}) { mergeCommit { oid } } }'",
    ],
)
def test_the_switch_refuses_the_other_api_routes_that_move_a_branch(command: str) -> None:
    reason = check(command, open_only=True)
    assert reason is not None and reason.startswith(OPEN_ONLY) and "moves" in reason
    assert check(command, open_only=False) is None


@pytest.mark.parametrize(
    "command",
    [
        "gh api repos/owner/repo/git/refs/heads/tools-x",
        "gh api -X GET repos/owner/repo/git/refs",
        "gh api repos/owner/repo/git/ref/heads/tools-x",
        "gh api repos/owner/repo/contents/docs/a.md",
        "gh api repos/owner/repo/git/commits/abc",
        "gh api -X POST repos/owner/repo/git/blobs -f content=x -f encoding=utf-8",
        'gh api graphql -f query=\'query { repository(owner: "o", name: "r") { ref(qualifiedName: "main") { id } } }\'',
    ],
)
def test_reading_refs_and_making_objects_passes(command: str) -> None:
    assert check(command, open_only=True) is None


@pytest.mark.parametrize(
    ("command", "refused"),
    [
        ("gh pr update-branch 12", True),
        ("gh pr update-branch 12 --rebase", True),
        ("& gh pr update-branch 12", True),
        ("gh api -X PATCH repos/owner/repo/git/refs/heads/tools-x -f sha=abc", True),
        ("gh api --method PUT repos/{owner}/{repo}/pulls/12/update-branch", True),
        ("Write-Output 'gh pr update-branch 12'", False),
        ("gh pr update-branch 12 -R someone/else", False),
    ],
)
def test_the_switch_reads_powershell_commands_too(command: str, refused: bool) -> None:
    reason: str | None = GUARD.check(
        command, "", powershell=True, runner=lambda args, stdin: (0, "ok"), open_only=True, repository=lambda *a: THIS
    )
    assert (reason is not None) is refused


@pytest.mark.parametrize(
    "command",
    [
        "gh pr create --title x --body-file f",
        "gh pr edit 12 --body-file f",
        "gh pr merge 12 --auto",
        "gh pr update-branch 12",
        "git push origin tools-x",
        "gh api -X PATCH repos/owner/repo/git/refs/heads/tools-x -f sha=abc",
        "gh api graphql -f query='mutation { createCommitOnBranch(input: {}) { commit { oid } } }'",
    ],
)
def test_every_refusal_points_at_the_table_of_commands_for_each_need(command: str) -> None:
    reason = check(command, open_only=True)
    assert reason is not None and "docs/guides/pr.md#after-the-switch" in reason


# --- which repository a gh api call is for ---------------------------------------------------------------------------

#: Calls whose endpoint names another repository than THIS: a fork, a plugin repository.
OTHER_REPOSITORY = [
    "gh api -X DELETE repos/other/thing/git/refs/heads/x",
    "gh api -X PATCH repos/other/thing/git/refs/heads/x -f sha=abc",
    "gh api -X POST repos/other/thing/git/refs -f ref=refs/heads/x -f sha=abc",
    "gh api -X PUT repos/other/thing/contents/a.md -f message=m -f content=Zm9v",
    "gh api -X POST repos/other/thing/merges -f base=a -f head=b",
    "gh api -X POST repos/other/thing/pulls -f title=t -f head=h -f base=main",
    "gh api -X PATCH repos/other/thing/pulls/5 -f body=x",
    "gh api -X PUT repos/other/thing/pulls/5/update-branch",
    "gh api -X PUT https://api.github.com/repos/Other/Thing/contents/a.md -f message=m",
]


@pytest.mark.parametrize("command", OTHER_REPOSITORY)
def test_a_gh_api_call_for_another_repository_passes(command: str) -> None:
    assert check(command, open_only=True) is None


@pytest.mark.parametrize("command", OTHER_REPOSITORY)
def test_the_same_call_for_this_repository_is_refused(command: str) -> None:
    this = command.replace("other/thing", THIS).replace("Other/Thing", "Owner/Repo")
    reason = check(this, open_only=True)
    assert reason is not None and reason.startswith(OPEN_ONLY)


def test_a_placeholder_call_in_a_folder_of_another_repository_passes() -> None:
    def repository(folder: str, *a: str) -> str:
        return "other/thing" if folder == "/elsewhere" else THIS

    command = "gh api -X DELETE repos/{owner}/{repo}/git/refs/heads/x"
    assert (
        GUARD.check(command, "/elsewhere", runner=lambda args, stdin: (0, "ok"), open_only=True, repository=repository)
        is None
    )
    reason = GUARD.check(command, "/here", runner=lambda args, stdin: (0, "ok"), open_only=True, repository=repository)
    assert reason is not None and reason.startswith(OPEN_ONLY)


def test_a_graphql_call_names_no_repository_and_is_refused() -> None:
    command = "gh api graphql -f query='mutation { createCommitOnBranch(input: {}) { commit { oid } } }'"
    assert check(command, open_only=True) is not None


# --- a mutation is read as a mutation ----------------------------------------------------------------------------


@pytest.mark.parametrize(
    "command",
    [
        "gh api repos/owner/repo/issues/5/comments -f body='the mergeBranch mutation moves a branch'",
        "gh api -X POST repos/owner/repo/issues/5/comments -f body='createCommitOnBranch(input: {}) is what to avoid'",
        "gh api repos/owner/repo/issues/5 -X PATCH -f title='mutation: updateRef(x) docs'",
        'gh api graphql -f query=\'query { repository(owner: "o", name: "r") { name } } # mergeBranch(x)\'',
        "gh api graphql -f query='query($n: String!) { repository(owner: \"o\", name: $n) { name } }' -F n=updateRef",
        # a name inside a string argument or a comment of a real mutation is text, not a call
        "gh api graphql -f query='mutation { addComment(input: {body: \"avoid createRef(\"}) { id } }'",
        'gh api graphql -f query="mutation { addComment(input: {body: \\"mergeBranch(\\"}) { id } }"',
        "gh api graphql -f query='mutation { addComment(input: {body: \"mutation createRef(\"}) { id } }'",
        "gh api graphql -f query='mutation { addComment(input: {}) { clientMutationId } } # deleteRef(x)'",
    ],
)
def test_a_mutation_name_in_other_text_is_no_mutation(command: str) -> None:
    assert check(command, open_only=True) is None


@pytest.mark.parametrize(
    "command",
    [
        "gh api graphql -f query='mutation { mergeBranch(input: {}) { clientMutationId } }'",
        "gh api graphql -f query='mutation Move($i: UpdateRefInput!) { updateRef(input: $i) { clientMutationId } }'",
        "gh api graphql -f query='mutation{createRef(input:{})}'",
        "gh api graphql -F query='mutation { deleteRef (input: {}) { clientMutationId } }'",
        # a real call after a string that names another one
        "gh api graphql -f query='mutation { a: addComment(input: {body: \"x\"}) { id } b: createRef(input: {}) {id}}'",
        'gh api graphql -f query="mutation { createRef(input: {name: \\"n\\"}) { clientMutationId } }"',
        "gh api graphql -f title=mutation -f query='mutation { createRef(input: {}) { clientMutationId } }'",
    ],
)
def test_a_mutation_is_refused_whatever_its_layout(command: str) -> None:
    reason = check(command, open_only=True)
    assert reason is not None and reason.startswith(OPEN_ONLY)
