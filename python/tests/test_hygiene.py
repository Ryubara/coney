# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools repo hygiene` and the hygiene workflow: made-up GitHub data and a fake remote, no network."""

import argparse
import dataclasses
import json
import re
from collections.abc import Iterable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import pytest
import yaml
from audit_support import CONVENTIONS, issue, pull, raw_snapshot, task_body
from conventions_support import REPO

from coney_tools import audit_github, hygiene_cli
from coney_tools.audit import Finding, Report, registered
from coney_tools.audit_github import GithubContext, parse_snapshot
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.hygiene_cli import run_hygiene
from coney_tools.hygiene_comments import (
    Action,
    Sticky,
    group_by_item,
    item_marker,
    item_number,
    neutralize,
    plan_items,
    plan_report,
    render_item,
    render_report,
    render_resolved,
    report_issue_body,
    report_marker,
)
from coney_tools.hygiene_github import GhRemote, pick_stickies

COMMENTS = CONVENTIONS.hygiene.comments
NOW = datetime(2026, 10, 12, 6, 0, tzinfo=UTC)
WORKFLOW = REPO / ".github/workflows/hygiene.yml"


def finding(
    check_id: str = "issue.labels", item: str = "#7", message: str = "no type label", fix: str = "skill"
) -> Finding:
    """A finding about `item`."""
    return Finding(check_id, "error", item, message, fix, ("labels",))


# --- neutralize -----------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "hostile",
    [
        "@everyone look",
        "<!-- coney-repo-hygiene -->",
        "--><script>x</script>",
        "[click](https://x.invalid)",
        "![i](https://x.invalid/a.png)",
    ],
)
def test_quoted_text_cannot_mention_link_or_close_the_marker(hostile: str) -> None:
    text = neutralize(hostile)
    assert "<" not in text and "@everyone" not in text
    assert "](" not in text.replace("\\](", "")


def test_a_report_does_not_write_cross_references() -> None:
    assert "#12" not in neutralize("see #12 and #13", references=True)
    assert "#12" in neutralize("see #12")


def test_quoted_text_is_one_short_line() -> None:
    text = neutralize("a\n\n b\t" + "x" * 1000)
    assert "\n" not in text and len(text) == 400 and text.endswith("...")


# --- the comment text -----------------------------------------------------------------------------------------------


def test_an_item_comment_starts_with_the_marker_and_lists_each_finding() -> None:
    text = render_item(COMMENTS, [finding(), finding("issue.sections", message="no Summary")])
    assert text.splitlines()[0] == "<!-- coney-repo-hygiene -->" == item_marker(COMMENTS)
    assert "found 2 things" in text and "- `issue.labels` (error): no type label" in text
    assert "- `issue.sections` (error): no Summary" in text


def test_the_same_findings_give_the_same_text() -> None:
    assert render_item(COMMENTS, [finding()]) == render_item(COMMENTS, [finding()])


def test_a_maintainer_decision_is_said() -> None:
    assert "(a maintainer decides)" in render_item(COMMENTS, [finding(fix="owner")])


def test_a_hostile_title_in_a_message_cannot_break_out_of_the_comment() -> None:
    text = render_item(COMMENTS, [finding(message='title "@maintainer <!-- x -->" is wrong')])
    assert text.count("<!--") == 1 and "@maintainer" not in text


def test_the_report_marker_differs_from_the_item_marker() -> None:
    assert report_marker(COMMENTS) == "<!-- coney-repo-hygiene:report -->"
    assert not report_marker(COMMENTS).startswith(item_marker(COMMENTS))


def report_of(findings: list[Finding], notices: list[str] | None = None) -> Report:
    """A report holding `findings`."""
    report = Report(("files", "github"), ran=["issue.labels", "issue.sections"], findings=findings)
    report.notices.extend(notices or [])
    return report


def test_the_report_counts_and_lists_every_check_with_findings() -> None:
    report = report_of([finding(), finding(item="#8"), finding("issue.sections", message="no Summary")], ["no network"])
    text = render_report(COMMENTS, report, "2026-10-12 06:00 UTC")
    assert text.splitlines()[0] == report_marker(COMMENTS)
    assert "3 errors, 0 warnings from 2 checks (files, github)" in text and "> no network" in text
    assert "### `issue.labels`: 2 (error, fix: skill)" in text and "- `#8`: no type label" in text


def test_a_clean_report_says_so() -> None:
    assert "Nothing to report." in render_report(COMMENTS, report_of([]), "now")


def test_a_huge_report_is_cut_to_fit_one_comment() -> None:
    many = [finding(item=f"#{n}", message="m" * 380) for n in range(1, 400)]
    text = render_report(COMMENTS, report_of(many), "now")
    assert len(text) <= 60000 and "more (" in text


def test_the_report_issue_body_passes_the_audit() -> None:
    labels = list(CONVENTIONS.hygiene.report_issue.labels)
    body = report_issue_body()
    raw = raw_snapshot(issues=[issue(3, title=CONVENTIONS.hygiene.report_issue.title, body=body, labels=tuple(labels))])
    context = GithubContext(CONVENTIONS, parse_snapshot(raw))
    for name in ("issue.sections", "issue.placeholder", "issue.screenshots", "issue.labels", "issue.owner-word"):
        assert not list(audit_github_check(name)(context)), name


def audit_github_check(name: str) -> Any:
    """The registered function of the github check `name`."""

    return registered()[name]


# --- grouping and planning ------------------------------------------------------------------------------------------


def test_only_issues_and_pull_requests_get_comments() -> None:
    groups = group_by_item(
        [finding(item="#7"), finding(item="AGENTS.md"), finding(item="worktree x"), finding(item="#9")], COMMENTS
    )
    assert sorted(groups) == [7, 9] and item_number("#12") == 12 and item_number("#12x") is None


def test_idleness_findings_are_report_only() -> None:
    groups = group_by_item([finding("pr.idle", item="#7"), finding("issue.progress-idle", item="#8")], COMMENTS)
    assert groups == {}


def test_a_new_finding_creates_a_comment_and_a_changed_one_updates_it() -> None:
    groups = {7: [finding()], 8: [finding(item="#8", message="changed")]}
    old = Sticky(55, 8, render_item(COMMENTS, [finding(item="#8", message="old")]))
    actions = plan_items(COMMENTS, groups, {8: old})
    assert [(a.kind, a.number, a.comment_id) for a in actions] == [("update", 8, 55), ("create", 7, None)]


def test_an_unchanged_comment_is_not_touched() -> None:
    groups = {7: [finding()]}
    assert plan_items(COMMENTS, groups, {7: Sticky(1, 7, render_item(COMMENTS, groups[7]))}) == []


def test_fixed_findings_resolve_the_comment_once_and_never_delete_it() -> None:
    stale = Sticky(9, 4, render_item(COMMENTS, [finding(item="#4")]))
    [action] = plan_items(COMMENTS, {}, {4: stale})
    assert action == Action("resolve", 4, 9, render_resolved(COMMENTS))
    assert plan_items(COMMENTS, {}, {4: Sticky(9, 4, action.body)}) == []


def test_a_scope_limits_the_plan_to_that_item() -> None:
    groups = {7: [finding()], 8: [finding(item="#8")]}
    assert [a.number for a in plan_items(COMMENTS, groups, {}, scope=[8])] == [8]


def test_the_limit_keeps_fixes_first() -> None:
    groups = {n: [finding(item=f"#{n}")] for n in (1, 2, 3)}
    stale = Sticky(9, 4, render_item(COMMENTS, [finding(item="#4")]))
    actions = plan_items(COMMENTS, groups, {4: stale}, limit=2)
    assert [(a.kind, a.number) for a in actions] == [("resolve", 4), ("create", 1)]


def test_the_report_comment_is_created_updated_or_left() -> None:
    assert plan_report(None, "body", 3) == Action("create", 3, None, "body")
    assert plan_report(Sticky(5, 3, "old"), "body", 3) == Action("update", 3, 5, "body")
    assert plan_report(Sticky(5, 3, "body"), "body", 3) is None


# --- reading comments back ------------------------------------------------------------------------------------------


def gh_comment(number: int, comment_id: int, login: str, body: str) -> dict[str, Any]:
    """A comment as the API returns it."""
    return {
        "id": comment_id,
        "body": body,
        "user": {"login": login},
        "issue_url": f"https://api.invalid/issues/{number}",
    }


def test_only_the_bots_own_comment_with_the_marker_counts() -> None:
    marker = item_marker(COMMENTS)
    comments = [
        gh_comment(7, 1, "someone", f"{marker}\nforged"),
        gh_comment(7, 2, COMMENTS.author, "not ours"),
        gh_comment(7, 3, COMMENTS.author, f"{marker}\nours"),
        gh_comment(7, 4, COMMENTS.author, f"{marker}\nlater duplicate"),
        gh_comment(8, 5, COMMENTS.author, f"{report_marker(COMMENTS)}\nthe report"),
    ]
    found = pick_stickies(comments, marker, COMMENTS.author)
    assert {number: sticky.comment_id for number, sticky in found.items()} == {7: 3}


class FakeGh:
    """A `gh` that records its calls and answers from a table keyed by the command's first words."""

    def __init__(self, answers: dict[str, str] | None = None) -> None:
        self.calls: list[list[str]] = []
        self.answers = answers or {}

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        for prefix, answer in self.answers.items():
            if " ".join(arguments).startswith(prefix):
                return answer
        return ""


def test_gh_remote_asks_gh_to_filter_and_parses_the_lines() -> None:
    line = json.dumps(gh_comment(7, 3, COMMENTS.author, f"{item_marker(COMMENTS)}\nours"))
    gh = FakeGh({"api repos/{owner}/{repo}/issues/7/comments": line + "\n"})
    found = GhRemote(gh).stickies(item_marker(COMMENTS), COMMENTS.author, 7)
    assert found[7].comment_id == 3
    assert "--paginate" in gh.calls[0] and COMMENTS.author in gh.calls[0][-1]


def test_gh_remote_refuses_a_marker_or_author_that_could_alter_the_filter() -> None:
    with pytest.raises(ConfigError):
        GhRemote(FakeGh()).stickies('<!-- x" or true -->', COMMENTS.author)
    with pytest.raises(ConfigError):
        GhRemote(FakeGh()).stickies(item_marker(COMMENTS), 'bot" or true or "')


def test_gh_remote_writes_a_body_without_a_shell() -> None:
    gh = FakeGh()
    remote = GhRemote(gh)
    remote.create_comment(7, "text; $(rm) @x")
    remote.edit_comment(55, "new")
    assert gh.calls[0] == [
        "api",
        "--method",
        "POST",
        "repos/{owner}/{repo}/issues/7/comments",
        "-f",
        "body=text; $(rm) @x",
    ]
    assert gh.calls[1][:3] == ["api", "--method", "PATCH"] and gh.calls[1][3].endswith("issues/comments/55")


def test_gh_remote_finds_the_report_issue_by_exact_title_and_creates_it_with_labels() -> None:
    listing = json.dumps([{"number": 4, "title": "Repo hygiene extras"}, {"number": 3, "title": "Repo hygiene"}])
    gh = FakeGh({"issue list": listing, "issue create": "https://example.invalid/issues/42\n"})
    remote = GhRemote(gh)
    assert remote.find_report_issue("Repo hygiene") == 3 and remote.find_report_issue("Other") is None
    assert remote.create_report_issue("Repo hygiene", "body", ["task", "A-tools"]) == 42
    create = gh.calls[-1]
    assert create.count("--label") == 2 and create[create.index("--body") + 1] == "body"


def test_a_failed_gh_write_is_a_config_error() -> None:
    def broken(arguments: list[str]) -> str:
        raise OSError("gh issue failed: no")

    with pytest.raises(ConfigError, match="cannot write"):
        GhRemote(broken).create_comment(1, "x")


# --- the command ----------------------------------------------------------------------------------------------------


class FakeRemote:
    """A GitHub in memory: comments by item, a report issue, and a log of the writes."""

    def __init__(self, report_issue: int | None = None) -> None:
        self.comments: dict[int, list[tuple[int, str, str]]] = {}
        self.report_issue = report_issue
        self.writes: list[tuple[str, Any]] = []
        self.next_id = 100

    def stickies(self, first_line: str, author: str, number: int | None = None) -> dict[int, Sticky]:
        raw = [
            gh_comment(n, comment_id, login, body)
            for n, items in self.comments.items()
            for comment_id, login, body in items
            if number is None or n == number
        ]
        return pick_stickies(raw, first_line, author)

    def create_comment(self, number: int, body: str) -> None:
        self.writes.append(("create", number))
        self.next_id += 1
        self.comments.setdefault(number, []).append((self.next_id, COMMENTS.author, body))

    def edit_comment(self, comment_id: int, body: str) -> None:
        self.writes.append(("edit", comment_id))
        for number, items in self.comments.items():
            self.comments[number] = [(i, u, body if i == comment_id else b) for i, u, b in items]

    def find_report_issue(self, title: str) -> int | None:
        return self.report_issue

    def create_report_issue(self, title: str, body: str, labels: Iterable[str]) -> int:
        self.writes.append(("issue", title))
        self.report_issue = 3
        return 3


@pytest.fixture(autouse=True)
def no_main_tree(monkeypatch: pytest.MonkeyPatch) -> None:
    """The audit runs without a copy of origin/main."""
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(audit_github, "git_tree", lambda root, ref: None)


def snapshot_file(tmp_path: Path, **kwargs: Any) -> Path:
    """Write a made-up snapshot and return its path."""
    path = tmp_path / "snapshot.json"
    path.write_text(json.dumps(raw_snapshot(**kwargs)), encoding="utf-8")
    return path


def namespace(snapshot: Path, event: int | None = None, apply: bool = True) -> argparse.Namespace:
    """The parsed options of `repo hygiene`."""
    return argparse.Namespace(event=event, sweep=event is None, apply=apply, snapshot=snapshot)


def broken_issue(number: int = 7) -> dict[str, Any]:
    """An open issue the audit finds something wrong with: no type label."""
    return issue(number, labels=("A-tools", "P-medium", "S-in-progress"), body=task_body())


def test_an_event_comments_on_its_item_once_and_edits_it_when_it_changes(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    remote = FakeRemote()
    path = snapshot_file(tmp_path, issues=[broken_issue(7), broken_issue(8)])
    assert run_hygiene(namespace(path, event=7), remote) == 0
    assert remote.writes == [("create", 7)] and 8 not in remote.comments
    assert "issue.labels" in remote.comments[7][0][2]
    run_hygiene(namespace(path, event=7), remote)
    assert remote.writes == [("create", 7)]
    assert "its comment is up to date" in capsys.readouterr().out


def test_fixing_the_item_resolves_the_comment_instead_of_deleting_it(tmp_path: Path) -> None:
    remote = FakeRemote()
    run_hygiene(namespace(snapshot_file(tmp_path, issues=[broken_issue(7)]), event=7), remote)
    fixed = snapshot_file(tmp_path, issues=[issue(7)])
    run_hygiene(namespace(fixed, event=7), remote)
    [(_, _, body)] = remote.comments[7]
    assert body == render_resolved(COMMENTS) and remote.writes[-1][0] == "edit"


def test_a_dry_run_writes_nothing(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    remote = FakeRemote()
    run_hygiene(namespace(snapshot_file(tmp_path, issues=[broken_issue(7)]), event=7, apply=False), remote)
    assert remote.writes == [] and "would create comment on #7" in capsys.readouterr().out


def test_events_can_be_switched_off(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:

    off = type(COMMENTS)(**{**COMMENTS.__dict__, "on_events": False})
    real = hygiene_cli.load_conventions
    monkeypatch.setattr(
        hygiene_cli,
        "load_conventions",
        lambda root: _with_comments(real(root), off),
    )
    remote = FakeRemote()
    run_hygiene(namespace(snapshot_file(tmp_path, issues=[broken_issue(7)]), event=7), remote)
    assert remote.writes == [] and "on_events is false" in capsys.readouterr().out


def _with_comments(conventions: Any, comments: Any) -> Any:
    """The conventions with other comment settings."""

    return dataclasses.replace(conventions, hygiene=dataclasses.replace(conventions.hygiene, comments=comments))


def test_the_sweep_creates_the_report_issue_and_its_comment_but_no_item_comments(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    remote = FakeRemote()
    run_hygiene(namespace(snapshot_file(tmp_path, issues=[broken_issue(7)])), remote, NOW)
    assert remote.writes == [("issue", CONVENTIONS.hygiene.report_issue.title), ("create", 3)]
    assert 7 not in remote.comments
    assert "item comments come from events only" in capsys.readouterr().out
    [(_, _, report)] = remote.comments[3]
    assert report.startswith(report_marker(COMMENTS)) and "2026-10-12 06:00 UTC" in report and "`#7`" in report


def test_the_sweep_edits_the_report_in_place(tmp_path: Path) -> None:
    remote = FakeRemote(report_issue=3)
    path = snapshot_file(tmp_path, issues=[broken_issue(7)])
    run_hygiene(namespace(path), remote, NOW)
    run_hygiene(namespace(path), remote, NOW)
    assert remote.writes == [("create", 3)]
    run_hygiene(namespace(path), remote, datetime(2026, 10, 19, 6, 0, tzinfo=UTC))
    assert remote.writes[-1][0] == "edit" and len(remote.comments[3]) == 1


def test_a_sweep_dry_run_with_no_report_issue_only_says_what_it_would_do(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    remote = FakeRemote()
    run_hygiene(namespace(snapshot_file(tmp_path), apply=False), remote, NOW)
    assert remote.writes == [] and "would create the report issue" in capsys.readouterr().out


def test_with_on_sweep_the_sweep_comments_on_items_up_to_the_limit(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:

    on = type(COMMENTS)(**{**COMMENTS.__dict__, "on_sweep": True, "max_per_sweep": 2})
    real = hygiene_cli.load_conventions
    monkeypatch.setattr(hygiene_cli, "load_conventions", lambda root: _with_comments(real(root), on))
    remote = FakeRemote(report_issue=3)
    run_hygiene(namespace(snapshot_file(tmp_path, issues=[broken_issue(n) for n in (7, 8, 9)])), remote, NOW)
    assert [write for write in remote.writes if write[1] != 3] == [("create", 7), ("create", 8)]


def test_the_command_needs_an_event_or_a_sweep(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as stop:
        main(["repo", "hygiene"])
    assert stop.value.code == 2
    assert "one of the arguments --event --sweep is required" in capsys.readouterr().err


def test_a_pull_request_with_a_bad_title_is_commented_on(tmp_path: Path) -> None:
    remote = FakeRemote()
    path = snapshot_file(tmp_path, pulls=[pull(12, title="fix stuff")])
    run_hygiene(namespace(path, event=12), remote)
    assert "pr.title" in remote.comments[12][0][2]


# --- the workflow ---------------------------------------------------------------------------------------------------


def workflow() -> dict[Any, Any]:
    """The workflow file's data (YAML reads the key `on` as true)."""
    data = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
    assert isinstance(data, dict)
    return data


def triggers(data: dict[Any, Any]) -> dict[str, Any]:
    """The `on` table."""
    return data.get("on") or data[True]


def test_the_workflow_may_only_read_the_code_and_write_issues() -> None:
    assert workflow()["permissions"] == {"contents": "read", "issues": "write"}


def test_the_sweep_schedule_is_the_conventions() -> None:
    assert triggers(workflow())["schedule"] == [{"cron": CONVENTIONS.hygiene.sweep}]


def test_it_reacts_to_item_events_and_can_be_run_by_hand() -> None:
    on = triggers(workflow())
    assert {"issues", "pull_request_target", "schedule", "workflow_dispatch"} == set(on)
    assert {"opened", "edited", "labeled", "unlabeled", "reopened"} <= set(on["issues"]["types"])
    assert "synchronize" not in on["pull_request_target"]["types"]


def test_no_expression_reaches_a_script_and_the_checkout_takes_no_other_ref() -> None:
    job = workflow()["jobs"]["hygiene"]
    for step in job["steps"]:
        assert "${{" not in step.get("run", ""), step
        if str(step.get("uses", "")).startswith("actions/checkout"):
            assert "with" not in step
    assert all(re.search(r"@[0-9a-f]{40}", step["uses"]) for step in job["steps"] if "uses" in step)


def test_runs_for_one_item_queue_instead_of_cancelling() -> None:
    assert workflow()["concurrency"]["cancel-in-progress"] is False
    assert "issue.number" in workflow()["concurrency"]["group"]


def test_a_comment_workflow_is_not_a_required_check() -> None:
    assert "hygiene" not in CONVENTIONS.merge.required_checks


def test_the_workflow_runs_the_command_the_conventions_describe() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    assert "coney-tools repo hygiene --event" in text and "coney-tools repo hygiene --sweep" in text
    assert "--apply" in text
