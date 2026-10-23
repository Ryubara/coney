# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.issue_claim_cli, the gh-backed remote and claim.yml: gh and the clock are fakes."""

import argparse
import json
import re
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import pytest
import yaml
from audit_support import CONVENTIONS
from claim_support import BOT, START, Clock, FakeGitHub, make_engine
from conventions_support import REPO

from coney_tools import issue_claim_cli as cli
from coney_tools import issue_claim_model as model
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.issue_claim_remote import GhRemote

RULES = CONVENTIONS.claims
WORKFLOW = REPO / ".github/workflows/claim.yml"


# --- the events -----------------------------------------------------------------------------------------------------


def test_the_issues_of_an_event() -> None:
    plain = {"issue": {"number": 12}}
    assert cli.issues_of_event("issue_comment", plain, CONVENTIONS) == [12]
    assert cli.issues_of_event("issues", plain, CONVENTIONS) == [12]
    assert cli.issues_of_event("issue_comment", {"issue": {"number": 12, "pull_request": {}}}, CONVENTIONS) == []
    assert cli.issues_of_event("schedule", {}, CONVENTIONS) is None
    assert cli.issues_of_event("workflow_dispatch", {}, CONVENTIONS) is None
    body = "## Summary\n\nFixes #7\nFixes #9\nfixes #7\n\nSee #8\n- Fixes #10 maybe"
    assert cli.issues_of_event("pull_request_target", {"pull_request": {"body": body}}, CONVENTIONS) == [7, 9]
    with pytest.raises(ConfigError, match="no handler"):
        cli.issues_of_event("push", {}, CONVENTIONS)


def test_a_comment_event_runs_the_claim_and_a_push_event_is_activity() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(10)
    github.say(10, "alice", "/claim")
    engine = make_engine(github, clock)
    lines, errors = cli.run_event(engine, "issue_comment", {"issue": {"number": 10}}, CONVENTIONS, clock.now)
    assert errors == [] and any("claimed it" in line for line in lines)
    clock.advance(hours=50)
    github.add_pr(60)
    github.link_pr(10, 60, "open")
    cli.run_event(
        engine,
        "pull_request_target",
        {"action": "synchronize", "pull_request": {"body": "Fixes #10", "user": {"login": "alice"}}},
        CONVENTIONS,
        clock.now,
    )
    card = model.parse_card(github.cards(10)[-1].body, RULES)
    assert card is not None and card.last_activity_why == "pull request" and github.others(10) == []


def test_a_schedule_event_runs_the_sweep() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(10)
    github.say(10, "alice", "/claim")
    lines, errors = cli.run_event(make_engine(github, clock), "schedule", {}, CONVENTIONS, clock.now)
    assert errors == [] and github.view(10).assignees == ("alice",) and lines


def test_an_error_on_one_issue_is_reported_and_the_rest_go_on() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(10)
    github.say(10, "alice", "/claim")
    engine = make_engine(github, clock)
    lines, errors = cli.run_event(
        engine, "pull_request_target", {"pull_request": {"body": "Fixes #99\nFixes #10"}}, CONVENTIONS, clock.now
    )
    assert len(errors) == 1 and errors[0].startswith("#99: cannot read from GitHub")
    assert github.view(10).assignees == ("alice",) and any("claimed it" in line for line in lines)


# --- gh -------------------------------------------------------------------------------------------------------------


class Gh:
    """A fake `gh`: answers by the first words of a command, remembers every command."""

    def __init__(self, answers: dict[str, str] | None = None, fail: set[str] | None = None) -> None:
        self.answers = answers or {}
        self.fail = fail or set()
        self.calls: list[list[str]] = []

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        text = " ".join(arguments)
        for key in self.fail:
            if key in text:
                raise OSError(f"gh {key} failed")
        for key, answer in self.answers.items():
            if key in text:
                return answer
        return ""


def remote(gh: Gh) -> GhRemote:
    """A GhRemote over the fake gh with the real conventions' markers."""
    return GhRemote(RULES.bot_login, RULES.card_marker, RULES.reply_marker, gh)


def test_an_issue_is_read_from_gh() -> None:
    data = {
        "title": "gameplay: Fix x",
        "state": "open",
        "state_reason": None,
        "labels": [{"name": "task"}, {"name": "S-ready-to-start"}],
        "assignees": [{"login": "alice"}],
        "body": "a\r\nb",
    }
    view = remote(Gh({"issues/5": json.dumps(data)})).issue(5)
    assert (view.labels, view.assignees, view.body, view.is_pr) == (
        ("task", "S-ready-to-start"),
        ("alice",),
        "a\nb",
        False,
    )
    pr = remote(Gh({"issues/5": json.dumps({**data, "pull_request": {}})})).issue(5)
    assert pr.is_pr


def test_comments_are_read_with_their_author_and_association() -> None:
    item = {
        "id": 77,
        "user": {"login": "alice", "type": "User"},
        "author_association": "COLLABORATOR",
        "body": "/claim",
        "created_at": "2026-10-22T12:00:00Z",
    }
    (comment,) = remote(Gh({"issues/5/comments": json.dumps(item)})).comments(5)
    assert (comment.id, comment.issue, comment.login, comment.association) == (77, 5, "alice", "COLLABORATOR")
    assert comment.created_at == START


def test_repository_wide_comments_know_their_issue() -> None:
    item = {
        "id": 78,
        "issue_url": "https://api.invalid/repos/o/r/issues/12",
        "user": {"login": BOT, "type": "Bot"},
        "body": "<!-- coney-claim -->",
        "created_at": "2026-10-22T12:00:00Z",
    }
    (card,) = remote(Gh({"issues/comments": json.dumps(item)})).all_cards()
    assert card.issue == 12 and card.user_type == "Bot"


def test_the_listing_filters_are_built_in_gh_so_only_matching_comments_are_downloaded() -> None:
    gh = Gh()
    remote(gh).all_cards()
    remote(gh).all_replies()
    remote(gh).recent_commands(START)
    cards, replies, commands = (" ".join(call) for call in gh.calls)
    assert 'startswith("<!-- coney-claim -->")' in cards and BOT in cards
    assert "issues/comments?per_page=100" in cards and "issues/comments?per_page=100" in replies
    assert 'startswith("<!-- coney-claim-reply:")' in replies
    assert "since=2026-10-22T12:00:00Z" in commands and "claim|unclaim|progress" in commands


def test_open_issue_listings_leave_out_pull_requests() -> None:
    gh = Gh({"labels=P-critical": "4\n9\n", "assignee=*": "5\n"})
    r = remote(gh)
    assert r.open_with_label("P-critical") == [4, 9] and r.open_assigned() == [5]
    assert 'has("pull_request") | not' in " ".join(gh.calls[0])


def test_a_bad_label_or_login_never_reaches_gh() -> None:
    gh = Gh()
    with pytest.raises(ConfigError, match="not allowed"):
        remote(gh).open_with_label("x&y=z")
    with pytest.raises(ConfigError, match="not allowed"):
        remote(gh).assign(5, "a b;c")
    assert gh.calls == []


def test_assign_reads_back_who_github_assigned() -> None:
    out = json.dumps({"assignees": [{"login": "bob"}]})
    assert remote(Gh({"assignees": out})).assign(5, "alice") == ("bob",)
    assert remote(Gh({"assignees": "not json"})).assign(5, "alice") == ()


def test_a_comment_is_created_with_its_id_and_a_refused_delete_is_false() -> None:
    gh = Gh({"POST repos/{owner}/{repo}/issues/5/comments": "123\n"})
    assert remote(gh).create_comment(5, "hi") == 123
    assert remote(Gh({"comments": "oops"})).delete_comment(1) is True
    assert remote(Gh(fail={"DELETE"})).delete_comment(1) is False
    with pytest.raises(ConfigError, match="id of the new comment"):
        remote(Gh({"POST": "no id"})).create_comment(5, "x")


def test_a_posted_command_returns_its_id_from_the_address() -> None:
    gh = Gh({"issue comment": "https://example.invalid/o/r/issues/5#issuecomment-4411\n"})
    assert remote(gh).post_command(5, "/claim") == 4411
    assert gh.calls[0] == ["issue", "comment", "5", "--body", "/claim"]
    with pytest.raises(ConfigError, match="did not print"):
        remote(Gh({"issue comment": "done"})).post_command(5, "/claim")


def test_a_blocker_is_finished_only_by_a_merged_pull_request() -> None:
    def issue(state: str, reason: str | None = None, pr: bool = False) -> str:
        return json.dumps({"state": state, "state_reason": reason, **({"pull_request": {}} if pr else {})})

    merged = {
        "data": {
            "repository": {
                "issue": {
                    "closedByPullRequestsReferences": {
                        "nodes": [{"number": 3, "state": "MERGED", "merged": True, "commits": {"nodes": []}}]
                    }
                }
            }
        }
    }
    unmerged: dict[str, Any] = {"data": {"repository": {"issue": {"closedByPullRequestsReferences": {"nodes": []}}}}}
    assert remote(Gh({"issues/30": issue("open")})).blocker(30).why == "open"
    assert remote(Gh({"issues/30": issue("closed", "not_planned")})).blocker(30).done is False
    assert remote(Gh({"issues/30": issue("closed", "completed"), "graphql": json.dumps(merged)})).blocker(30).done
    assert not remote(Gh({"issues/30": issue("closed", "completed"), "graphql": json.dumps(unmerged)})).blocker(30).done
    assert (
        remote(
            Gh({"issues/30": issue("closed", pr=True), "pulls/30": json.dumps({"merged_at": "2026-10-01T00:00:00Z"})})
        )
        .blocker(30)
        .done
    )
    assert (
        not remote(Gh({"issues/30": issue("open", pr=True), "pulls/30": json.dumps({"merged_at": None})}))
        .blocker(30)
        .done
    )


def test_linked_pull_requests_carry_their_state_and_head_date() -> None:
    nodes = [
        {
            "number": 3,
            "state": "OPEN",
            "merged": False,
            "commits": {"nodes": [{"commit": {"committedDate": "2026-10-22T12:00:00Z"}}]},
        },
        {"number": 4, "state": "MERGED", "merged": True, "commits": {"nodes": []}},
    ]
    out = json.dumps({"data": {"repository": {"issue": {"closedByPullRequestsReferences": {"nodes": nodes}}}}})
    found = remote(Gh({"graphql": out})).linked_prs(5)
    assert [(pr.number, pr.state) for pr in found] == [(3, "open"), (4, "merged")] and found[0].head_date == START
    assert remote(Gh({"graphql": "{}"})).linked_prs(5) == []


def test_the_permission_is_asked_of_github_and_unknown_is_not_none() -> None:
    assert remote(Gh({"permission": "write\n"})).permission("alice") == "write"
    assert remote(Gh(fail={"permission"})).permission("alice") is None

    def raising(arguments: list[str]) -> str:
        raise OSError("gh api failed: Not Found (HTTP 404)")

    assert GhRemote(RULES.bot_login, RULES.card_marker, RULES.reply_marker, raising).permission("alice") == "none"


def test_a_linked_pull_request_carries_its_author() -> None:
    nodes = [{"number": 3, "state": "OPEN", "merged": False, "author": {"login": "alice"}, "commits": {"nodes": []}}]
    out = json.dumps({"data": {"repository": {"issue": {"closedByPullRequestsReferences": {"nodes": nodes}}}}})
    (found,) = remote(Gh({"graphql": out})).linked_prs(5)
    assert found.author == "alice"


def test_a_gh_failure_is_a_config_error() -> None:
    with pytest.raises(ConfigError, match="cannot read"):
        remote(Gh(fail={"issues/5"})).issue(5)
    with pytest.raises(ConfigError, match="cannot write"):
        remote(Gh(fail={"assignees"})).assign(5, "alice")


# --- the commands ---------------------------------------------------------------------------------------------------


def parse(*words: str) -> argparse.Namespace:
    """Parse a `claim` command line."""
    group = argparse.ArgumentParser().add_subparsers(dest="group")
    cli.add_claim_parser(group)
    return group.choices["claim"].parse_args(words)


def test_claim_run_reads_the_payload_file_and_prints_the_log(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(REPO)
    payload = tmp_path / "event.json"
    payload.write_text(json.dumps({"issue": {"number": 5}}), encoding="utf-8")
    gh = Gh(
        {
            "issues/5/comments": "",
            "issues/5": json.dumps({"state": "open", "labels": [], "assignees": [], "body": ""}),
        }
    )
    args = parse("run", "--event", "issue_comment", "--payload", str(payload))
    assert cli.run_claim(args, gh, lambda: START) == 0
    assert "nothing to do" in capsys.readouterr().out


def test_claim_run_refuses_a_payload_that_is_not_an_object(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(REPO)
    payload = tmp_path / "event.json"
    payload.write_text("[1]", encoding="utf-8")
    with pytest.raises(ConfigError, match="expected a JSON object"):
        cli.run_claim(parse("run", "--event", "issues", "--payload", str(payload)), Gh(), lambda: START)
    payload.write_text("{", encoding="utf-8")
    assert main(["claim", "run", "--event", "issues", "--payload", str(payload)]) == 2


def test_claim_list_prints_the_held_claims(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    monkeypatch.chdir(REPO)
    held = model.Card(issue=7, claimant="alice", claimed_at=START, scope=("src/ai/",), task="t-1")
    item = {
        "id": 9,
        "issue_url": "https://api.invalid/repos/o/r/issues/7",
        "user": {"login": BOT, "type": "Bot"},
        "body": model.render_card(held, RULES),
        "created_at": "2026-10-22T12:00:00Z",
    }
    gh = Gh({"issues/comments": json.dumps(item)})
    assert cli.run_claim(parse("list"), gh) == 0
    assert "#7 alice claimed task t-1: src/ai/; expires 2026-10-25 12:00 UTC" in capsys.readouterr().out
    assert cli.run_claim(parse("list", "--json"), gh) == 0
    (row,) = json.loads(capsys.readouterr().out)
    assert row["issue"] == 7 and row["deadline"] == "2026-10-25T12:00:00Z" and row["scope"] == ["src/ai/"]


def test_claim_list_says_what_a_waiting_claim_waits_for(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(REPO)
    held = model.Card(issue=7, claimant="alice", claimed_at=START, status="pr-open", pr=60, waiting="PR #60 open")
    item = {
        "id": 9,
        "issue_url": "https://api.invalid/repos/o/r/issues/7",
        "user": {"login": BOT, "type": "Bot"},
        "body": model.render_card(held, RULES),
        "created_at": "2026-10-22T12:00:00Z",
    }
    gh = Gh({"issues/comments": json.dumps(item)})
    assert cli.run_claim(parse("list"), gh) == 0
    assert "#7 alice pr-open: scope not stated; waits while PR #60 open" in capsys.readouterr().out
    assert cli.run_claim(parse("list", "--json"), gh) == 0
    (row,) = json.loads(capsys.readouterr().out)
    assert row["waiting"] == "PR #60 open" and row["deadline"] is None


def test_the_command_is_registered() -> None:
    with pytest.raises(SystemExit) as stop:
        main(["claim", "--help"])
    assert stop.value.code == 0


# --- the workflow ---------------------------------------------------------------------------------------------------


def workflow() -> dict[Any, Any]:
    """The workflow file's data (YAML reads the key `on` as true)."""
    data = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
    assert isinstance(data, dict)
    return data


def triggers() -> dict[str, Any]:
    """The `on` table."""
    data = workflow()
    on: dict[str, Any] = data.get("on") or data[True]
    return on


def test_the_sweep_schedule_is_the_conventions() -> None:
    assert triggers()["schedule"] == [{"cron": RULES.sweep}]


def test_it_reacts_to_comments_issues_and_pull_requests_and_can_be_run_by_hand() -> None:
    on = triggers()
    assert set(on) == {"issue_comment", "issues", "pull_request_target", "schedule", "workflow_dispatch"}
    assert on["issue_comment"]["types"] == ["created"]
    assert {"synchronize", "closed", "edited"} <= set(on["pull_request_target"]["types"])


def test_the_token_writes_issues_and_only_reads_the_rest() -> None:
    assert workflow()["permissions"] == {"contents": "read", "issues": "write", "pull-requests": "read"}


def test_no_expression_reaches_a_script_and_the_checkout_takes_no_other_ref() -> None:
    job = workflow()["jobs"]["claim"]
    for step in job["steps"]:
        assert "${{" not in step.get("run", ""), step
        if str(step.get("uses", "")).startswith("actions/checkout"):
            assert "with" not in step
    assert all(re.search(r"@[0-9a-f]{40}", step["uses"]) for step in job["steps"] if "uses" in step)


def test_runs_for_one_issue_queue_and_a_chat_comment_cannot_replace_a_claim() -> None:
    concurrency = workflow()["concurrency"]
    assert concurrency["cancel-in-progress"] is False
    group = concurrency["group"]
    assert "github.event.issue.number" in group and "pull_request.number" in group
    assert "startsWith(github.event.comment.body, '/')" in group and "github.run_id" in group
    assert "startsWith(github.event.comment.body, '/')" in workflow()["jobs"]["claim"]["if"]


def test_the_workflow_runs_the_command_with_the_event_file() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    assert 'coney-tools claim run --event "$EVENT_NAME" --payload "$GITHUB_EVENT_PATH"' in text


def test_the_workflow_is_a_docs_only_scope_class_like_the_other_linux_workflows() -> None:
    assert CONVENTIONS.paths.ci_class(".github/workflows/claim.yml") == "docs_only"


def test_the_clock_used_when_none_is_given_is_utc() -> None:
    assert cli._utc_now().tzinfo is UTC and isinstance(cli._utc_now(), datetime)
