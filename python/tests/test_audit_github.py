# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.audit_github: every GitHub check on made-up issues and pull requests (no network)."""

import json
from dataclasses import replace
from datetime import UTC, datetime
from typing import Any

import pytest
from audit_support import (
    CONVENTIONS,
    PR_BODY,
    REAL_LABELS,
    FakeTree,
    bug_body,
    context,
    found,
    issue,
    items,
    pull,
    raw_snapshot,
    snapshot,
    task_body,
)

from coney_tools import audit_github as gh
from coney_tools.audit import registered
from coney_tools.config import ConfigError

GITHUB_CHECKS = [spec.id for spec in CONVENTIONS.hygiene.checks.values() if spec.scope == "github"]


# --- the snapshot -------------------------------------------------------------------------------------------------


def test_a_snapshot_is_parsed_from_the_json_gh_returns() -> None:
    shot = snapshot(
        issues=[issue(5, state="CLOSED", reason="NOT_PLANNED")], pulls=[pull(7, state="MERGED", closes=(5,))]
    )
    (closed,) = shot.issues
    assert not closed.open and closed.dropped and closed.labels[0] == "task" and shot.issue(5) is closed
    (merged,) = shot.pulls
    assert merged.state == "merged" and merged.closes == (5,) and shot.pull(7) is merged and shot.issue(7) is None


def test_a_malformed_snapshot_is_a_configuration_error() -> None:
    with pytest.raises(ConfigError, match="malformed"):
        gh.parse_snapshot({"repo": "x/y"})


def test_fetch_reads_with_gh_only_and_keeps_what_it_reads() -> None:
    calls: list[list[str]] = []
    answers = {
        "repo view": {"nameWithOwner": "someone/coney", "defaultBranchRef": {"name": "main"}},
        "issue list": [issue(1)],
        "pr list": [pull(2)],
        "label list": REAL_LABELS,
        "api repos/someone/coney": {"allow_squash_merge": True},
        "api repos/someone/coney/rulesets": [{"id": 9}],
        "api repos/someone/coney/rulesets/9": {"id": 9, "rules": []},
        "api graphql": {"data": {"repository": {"issues": {"pageInfo": {"hasNextPage": False}, "nodes": []}}}},
    }

    def run(arguments: list[str]) -> str:
        calls.append(arguments)
        key = " ".join(arguments[:2]) if arguments[0] != "api" or arguments[1] == "graphql" else " ".join(arguments)
        return json.dumps(answers[key])

    raw = gh.fetch_raw(run)
    assert raw["repo"] == "someone/coney" and raw["rulesets"] == [{"id": 9, "rules": []}] and raw["notices"] == []
    assert gh.parse_snapshot(raw).issue(1) is not None
    # every command is a read
    assert {call[0] for call in calls} <= {"repo", "issue", "pr", "label", "api"}
    writes = {"edit", "create", "close", "reopen", "comment", "merge", "--method", "-X", "-f", "-F", "--field"}
    # `-f` only passes the variables of the one GraphQL query (a read: no `mutation`).
    plain = [call for call in calls if call[:2] != ["api", "graphql"]]
    assert not any(token in writes for call in plain for token in call)
    assert not any("mutation" in token for call in calls for token in call)


def test_unreadable_settings_become_a_notice_not_a_failure() -> None:
    def run(arguments: list[str]) -> str:
        if arguments[0] == "api":
            raise OSError("gh api failed: HTTP 403")
        if arguments[:2] == ["repo", "view"]:
            return json.dumps({"nameWithOwner": "a/b", "defaultBranchRef": {"name": "main"}})
        return "[]"

    raw = gh.fetch_raw(run)
    assert raw["settings"] is None and raw["rulesets"] is None and "403" in raw["notices"][0]


def test_a_fetch_that_hits_the_limit_says_so(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(gh, "FETCH_LIMIT", 1)

    def run(arguments: list[str]) -> str:
        if arguments[:2] == ["repo", "view"]:
            return json.dumps({"nameWithOwner": "a/b", "defaultBranchRef": {"name": "main"}})
        if arguments[:2] == ["issue", "list"]:
            return json.dumps([issue(1)])
        return "[]"

    assert "older ones may be missing" in " ".join(gh.fetch_raw(run)["notices"])


def test_a_clean_issue_and_pull_request_have_no_findings_in_any_check() -> None:
    ctx = context(
        issues=[issue(1), issue(2, body=bug_body(), labels=("bug", "A-combat", "P-low", "S-needs-info"))],
        pulls=[pull(3)],
    )
    functions = registered()
    for check_id in GITHUB_CHECKS:
        if check_id in ("issue.links", "gh.ruleset"):
            continue
        assert found(functions[check_id], ctx) == [], check_id


# --- issue.sections, issue.placeholder, issue.screenshots -----------------------------------------------------------


def test_a_missing_required_section_is_named_with_the_headings_it_has_instead() -> None:
    body = task_body().replace('What "done" means', "Done when")
    (problem,) = found(gh.issue_sections, context(issues=[issue(201, body=body)]))
    assert (
        problem.item == "#201"
        and "'What \"done\" means'" in problem.message
        and "'Done when' instead" in problem.message
    )
    assert problem.url.endswith("/issues/201")


def test_a_bug_in_the_bug_form_is_checked_against_the_bug_form() -> None:
    body = bug_body().replace("### How often does it happen?\n\nAlways\n\n", "")
    (problem,) = found(
        gh.issue_sections, context(issues=[issue(3, body=body, labels=("bug", "A-ai", "P-low", "S-needs-triage"))])
    )
    assert "How often does it happen?" in problem.message and "bug form" in problem.message


def test_an_issue_with_no_headings_lacks_every_required_section_of_the_command_line_form() -> None:
    (problem,) = found(gh.issue_sections, context(issues=[issue(4, body="Just some text.")]))
    for heading in ("Summary", "Area", "Screenshots"):
        assert heading in problem.message
    assert "task form" in problem.message


def test_optional_sections_may_be_absent_and_headings_may_be_level_two() -> None:
    body = (
        '## Summary\n\nThe problem.\n\n## Area\n\nA-tools\n\n## What "done" means\n\nIt works.\n\n'
        "## Screenshots\n\nScreenshots: none (no visible change)\n"
    )
    assert found(gh.issue_sections, context(issues=[issue(5, body=body)])) == []


def test_a_closed_issue_is_not_checked_for_sections() -> None:
    assert found(gh.issue_sections, context(issues=[issue(6, body="Old.", state="CLOSED")])) == []


def test_a_placeholder_summary_is_found() -> None:
    (problem,) = found(gh.issue_placeholder, context(issues=[issue(200, body=task_body(summary="x"))]))
    assert problem.item == "#200" and "'Summary' is 'x'" in problem.message


@pytest.mark.parametrize("summary", ["", "TBD", "...", "_No response_"])
def test_an_empty_or_placeholder_required_section_is_found(summary: str) -> None:
    assert items(found(gh.issue_placeholder, context(issues=[issue(7, body=task_body(summary=summary))]))) == ["#7"]


def test_a_placeholder_in_an_optional_section_is_left_alone() -> None:
    body = task_body().replace("### Research page\n\nNone.", "### Research page\n\nx")
    assert found(gh.issue_placeholder, context(issues=[issue(8, body=body)])) == []


def test_screenshots_need_an_image_or_a_none_line_with_a_reason() -> None:
    ctx = context(
        issues=[
            issue(10, body=task_body(screenshots="")),
            issue(11, body=task_body(screenshots="Screenshots: none ()")),
            issue(12, body=task_body(screenshots="Screenshots: none (sound only)")),
            issue(13, body=task_body(screenshots="![a](https://github.com/user-attachments/assets/1)")),
            issue(14, body=task_body(screenshots="![a](./shots/a.png)")),
            issue(15, body=task_body(screenshots="_No response_")),
        ]
    )
    assert items(found(gh.issue_screenshots, ctx)) == ["#10", "#11", "#14", "#15"]
    local = found(gh.issue_screenshots, ctx)[2]
    assert "local file" in local.message


def test_a_missing_screenshots_section_is_left_to_issue_sections() -> None:
    body = task_body().split("## Screenshots")[0]
    assert found(gh.issue_screenshots, context(issues=[issue(16, body=body)])) == []
    assert items(found(gh.issue_sections, context(issues=[issue(16, body=body)]))) == ["#16"]


# --- issue.labels ---------------------------------------------------------------------------------------------------


def labels_of(*names: str, state: str = "OPEN") -> list[str]:
    """The label problems of one issue labelled `names`."""
    return [
        problem.message for problem in found(gh.issue_labels, context(issues=[issue(1, labels=names, state=state)]))
    ]


def test_an_issue_with_all_four_families_is_clean() -> None:
    assert labels_of("task", "A-tools", "P-low", "S-in-progress") == []


def test_a_missing_family_is_named() -> None:
    assert "no type label" in labels_of("A-tools", "P-low", "S-in-progress")[0]
    assert "no area label" in labels_of("task", "P-low", "S-in-progress")[0]
    assert "no priority label" in labels_of("task", "A-tools", "S-in-progress")[0]
    assert "no status label" in labels_of("task", "A-tools", "P-low")[0]


def test_priority_may_wait_for_triage() -> None:
    assert labels_of("task", "A-tools", "S-needs-triage") == []


def test_a_ready_to_start_issue_counts_as_triaged_and_needs_a_priority() -> None:
    assert labels_of("task", "A-tools", "P-low", "S-ready-to-start") == []
    assert "no priority label" in labels_of("task", "A-tools", "S-ready-to-start")[0]


def test_two_labels_of_a_one_only_family_are_found() -> None:
    assert "2 type labels (bug, parity)" in labels_of("bug", "parity", "A-ai", "P-low", "S-blocked")[0]
    assert "2 priority labels" in labels_of("task", "A-ai", "P-low", "P-high", "S-blocked")[0]


def test_several_areas_are_fine() -> None:
    assert labels_of("bug", "A-ai", "A-combat", "P-low", "S-blocked") == []


def test_an_unknown_label_is_found_and_githubs_own_are_fine() -> None:
    assert "label 'weird' is not in labels.list" in labels_of("task", "A-tools", "P-low", "S-blocked", "weird")[0]
    assert labels_of("task", "A-tools", "P-low", "S-blocked", "good first issue", "help wanted") == []


def test_a_closed_issue_is_not_asked_for_a_status_or_other_families() -> None:
    assert labels_of("task", "S-pending-review", state="CLOSED") == []
    assert labels_of("task", state="CLOSED") == []
    assert "label 'weird' is not in labels.list" in labels_of("task", "weird", state="CLOSED")[0]


def closed_status(*issues: Any, keeps: bool = False) -> list[str]:
    """The items `issue.closed-status` finds among `issues`, with `issues.labels.closed_keeps_status` set to `keeps`."""
    rules = CONVENTIONS.issues
    conventions = replace(CONVENTIONS, issues=replace(rules, labels=replace(rules.labels, closed_keeps_status=keeps)))
    return items(found(gh.issue_closed_status, context(conventions, issues=list(issues))))


def test_a_closed_issue_still_carrying_a_status_label_is_found() -> None:
    stale = issue(1, state="CLOSED", labels=("task", "A-tools", "P-low", "S-pending-review"))
    clean = issue(2, state="CLOSED", labels=("task", "A-tools", "P-low"))
    reopened = issue(3, labels=("task", "A-tools", "P-low", "S-in-progress"))
    (problem,) = found(gh.issue_closed_status, context(issues=[stale, clean, reopened]))
    assert problem.item == "#1" and "closed but still labelled S-pending-review" in problem.message


def test_every_status_label_of_a_closed_issue_is_named_whatever_the_close_reason() -> None:
    both = issue(4, state="CLOSED", reason="NOT_PLANNED", labels=("wontfix", "S-blocked", "S-needs-info"))
    (problem,) = found(gh.issue_closed_status, context(issues=[both]))
    assert "S-blocked, S-needs-info" in problem.message


def test_the_closed_status_rule_can_be_switched_off_in_the_conventions() -> None:
    assert closed_status(issue(1, state="CLOSED", labels=("task", "S-blocked")), keeps=True) == []
    assert closed_status(issue(1, state="CLOSED", labels=("task", "S-blocked"))) == ["#1"]


# --- wording --------------------------------------------------------------------------------------------------------


def test_banned_word_in_an_issue_or_pull_request_is_found_and_user_is_fine() -> None:
    bad = task_body(summary="The owner decides what ships.")
    ctx = context(
        issues=[issue(1, body=bad), issue(2), issue(3, title="Ask the owner", body=task_body())],
        pulls=[pull(4, body=PR_BODY.replace("Adds a check.", "The owner asked for this.")), pull(5)],
    )
    assert items(found(gh.issue_person_word, ctx)) == ["#1", "#3"]
    assert items(found(gh.pr_person_word, ctx)) == ["#4"]


def test_the_banned_word_finding_names_the_user_and_the_maintainer() -> None:
    ctx = context(issues=[issue(1, body=task_body(summary="The owner decides what ships."))])
    message = found(gh.issue_person_word, ctx)[0].message
    assert "'owner'" in message and "'user'" in message and "'maintainer'" in message


def test_banned_word_in_code_or_a_closed_issue_is_not_found() -> None:
    ctx = context(
        issues=[
            issue(1, body=task_body(summary="Rename `owner_name`; see owner/repo.")),
            issue(2, state="CLOSED", body=task_body(summary="the owner")),
        ]
    )
    assert found(gh.issue_person_word, ctx) == []


# --- leftovers and statuses -----------------------------------------------------------------------------------------


def test_a_closed_issue_with_an_unchecked_item_and_no_number_is_found() -> None:
    body = task_body(extra="### Fix list\n\n- [x] done\n- [ ] the runtime check\n- [ ] later, tracked in #99\n\n")
    ctx = context(issues=[issue(94, body=body, state="CLOSED", labels=("task",)), issue(95, body=body)])
    (problem,) = found(gh.issue_closed_unchecked, ctx)
    assert problem.item == "#94" and "1 unchecked item" in problem.message and "the runtime check" in problem.message


def test_a_closed_issue_names_the_merged_pull_request_with_a_ticked_checklist_that_closed_it() -> None:
    body = task_body(extra="### Fix list\n\n- [ ] the runtime check\n\n")
    ctx = context(
        issues=[issue(94, body=body, state="CLOSED", labels=("task",)), issue(95, body=body, state="CLOSED")],
        pulls=[
            pull(7, state="MERGED", merged=AFTER, closes=(94,)),
            pull(8, state="MERGED", merged=AFTER, closes=(95,), body=UNFINISHED),
        ],
    )
    first, second = found(gh.issue_closed_unchecked, ctx)
    assert "closed by #7, whose Checklist is all ticked" in first.message
    assert "closed by" not in second.message


@pytest.mark.parametrize("reason", ["NOT_PLANNED", "DUPLICATE"])
def test_a_dropped_issue_may_keep_its_items(reason: str) -> None:
    body = task_body(extra="### Fix list\n\n- [ ] never done\n\n")
    assert found(gh.issue_closed_unchecked, context(issues=[issue(1, body=body, state="CLOSED", reason=reason)])) == []


def test_an_issue_labelled_wontfix_may_keep_its_items() -> None:
    body = task_body(extra="### Fix list\n\n- [ ] never done\n\n")
    ctx = context(issues=[issue(1, body=body, state="CLOSED", labels=("task", "wontfix"))])
    assert found(gh.issue_closed_unchecked, ctx) == []


def test_a_pull_request_called_open_that_has_merged_is_found() -> None:
    body = task_body(summary="Needs #169 (open) and #170 (merged) and #171 (open), also #5 (open).")
    ctx = context(
        issues=[issue(1, body=body), issue(5)],
        pulls=[pull(169, state="MERGED"), pull(170, state="MERGED"), pull(171)],
    )
    (problem,) = found(gh.issue_stale_status, ctx)
    assert (
        "#169 is called open but is merged" in problem.message
        and "#170" not in problem.message
        and "#5" not in problem.message
    )


def test_a_pull_request_called_closed_after_it_merged_is_not_wrong() -> None:
    ctx = context(issues=[issue(1, body=task_body(summary="#9 (closed)"))], pulls=[pull(9, state="MERGED")])
    assert found(gh.issue_stale_status, ctx) == []


def test_an_issue_an_older_remaining_list_points_at_without_a_from_line_is_found() -> None:
    merged = PR_BODY.replace("Done:\n", "Remaining:\n\n- [ ] more work (#30)\n- [ ] other (#31)\n\nDone:\n")
    ctx = context(
        issues=[
            issue(30, body=task_body(extra="From: #12\n\n")),
            issue(31),
            issue(32),
        ],
        pulls=[pull(12, state="MERGED", body=merged)],
    )
    (problem,) = found(gh.issue_provenance, ctx)
    assert problem.item == "#31" and "leftover of #12" in problem.message


# --- issue.links ----------------------------------------------------------------------------------------------------


def link_problems(summary: str, tree: FakeTree | None) -> list[str]:
    """The link problems of an issue whose Summary is `summary`."""
    ctx = context(issues=[issue(1, body=task_body(summary=summary))], tree=tree)
    return [problem.message for problem in found(gh.issue_links, ctx)]


TREE = FakeTree({"docs/guides/a.md": frozenset({"known"}), "docs/research/x.md": frozenset()})


def test_a_docs_path_written_out_is_found() -> None:
    (message,) = link_problems("Read docs/guides/a.md first.", TREE)
    assert "names docs/guides/a.md as plain text" in message and "../blob/main/docs/guides/a.md" in message


def test_a_working_link_is_fine() -> None:
    assert link_problems("See [docs/guides/a.md](../blob/main/docs/guides/a.md#known).", TREE) == []


def test_a_link_to_a_page_or_section_that_is_not_on_main_is_found() -> None:
    (page,) = link_problems("See [a](../blob/main/docs/guides/gone.md).", TREE)
    assert "docs/guides/gone.md, which is not on main" in page
    (anchor,) = link_problems("See [a](../blob/main/docs/guides/a.md#nope).", TREE)
    assert "docs/guides/a.md#nope, which has no such section" in anchor


def test_a_link_to_another_ref_or_a_relative_link_is_found() -> None:
    assert "not 'main'" in link_problems("[a](../blob/feature/docs/guides/a.md)", TREE)[0]
    assert "does not work from an issue page" in link_problems("[a](docs/guides/a.md)", TREE)[0]


def test_without_the_tree_only_the_text_is_checked() -> None:
    (message,) = link_problems("[a](../blob/main/docs/guides/gone.md) and docs/guides/a.md", None)
    assert message.startswith("names docs/guides/a.md as plain text")


# --- ages -----------------------------------------------------------------------------------------------------------


def test_triage_age_counts_from_the_day_the_issue_was_opened() -> None:
    old = ("task", "A-tools", "S-needs-triage")
    ctx = context(
        issues=[
            issue(1, labels=old, created=8, updated=0.5),
            issue(2, labels=old, created=6),
            issue(3, created=100),
            issue(4, labels=old, state="CLOSED", created=100),
        ]
    )
    (problem,) = found(gh.issue_triage_age, ctx)
    assert problem.item == "#1" and "8 days (limit 7)" in problem.message


def test_info_and_progress_ages_count_from_the_last_activity() -> None:
    ctx = context(
        issues=[
            issue(1, labels=("task", "A-tools", "S-needs-info"), updated=22),
            issue(2, labels=("task", "A-tools", "S-needs-info"), updated=20),
            issue(3, labels=("task", "A-tools", "P-low", "S-in-progress"), updated=31),
            issue(4, labels=("task", "A-tools", "P-low", "S-in-progress"), updated=29),
        ]
    )
    assert items(found(gh.issue_info_age, ctx)) == ["#1"]
    assert items(found(gh.issue_progress_idle, ctx)) == ["#3"]


def test_an_issue_in_progress_or_review_with_nobody_assigned_is_an_orphan() -> None:
    labels = ("task", "A-tools", "P-low")
    ctx = context(
        issues=[
            issue(1, labels=(*labels, "S-in-progress"), assignees=()),
            issue(2, labels=(*labels, "S-pending-review"), assignees=()),
            issue(3, labels=(*labels, "S-in-progress")),
            issue(4, labels=(*labels, "S-ready-to-start"), assignees=()),
            issue(5, labels=(*labels, "S-in-progress"), assignees=(), state="CLOSED"),
        ]
    )
    assert items(found(gh.issue_claim_orphan, ctx)) == ["#1", "#2"]


def test_the_orphan_check_is_report_only() -> None:
    assert "issue.claim-orphan" in CONVENTIONS.hygiene.comments.report_only


def test_a_pending_review_issue_needs_an_open_pull_request_that_names_it() -> None:
    pending = ("task", "A-tools", "P-low", "S-pending-review")
    ctx = context(
        issues=[issue(1, labels=pending), issue(2, labels=pending), issue(3, labels=pending), issue(4, labels=pending)],
        pulls=[
            pull(10, closes=(1,)),
            pull(11, body=PR_BODY.replace("Adds a check.", "Fixes #2")),
            pull(12, state="MERGED", closes=(3,)),
        ],
    )
    assert items(found(gh.issue_review_without_pr, ctx)) == ["#3", "#4"]


def test_a_pull_request_idle_past_the_limit_is_found() -> None:
    ctx = context(pulls=[pull(1, updated=31), pull(2, updated=29), pull(3, updated=99, state="MERGED")])
    assert items(found(gh.pr_idle, ctx)) == ["#1"]


# --- pull requests --------------------------------------------------------------------------------------------------


def test_title_and_body_of_open_pull_requests_are_checked() -> None:
    ctx = context(
        pulls=[
            pull(1, title="Give each human stats"),
            pull(2, body="Nothing here."),
            pull(3, title="bad", state="MERGED", body="x"),
            pull(4),
        ]
    )
    assert items(found(gh.pr_title, ctx)) == ["#1"] and items(found(gh.pr_body, ctx)) == ["#2"]


def test_a_local_path_or_a_local_image_in_a_description_is_found() -> None:
    with_path = PR_BODY.replace("Adds a check.", "See scratch/conventions-3/AUDIT-1.md for the list.")
    with_image = PR_BODY.replace("Screenshots: none (tooling)", "![a](./shots/a.png)")
    problems = found(gh.pr_body, context(pulls=[pull(1, body=with_path), pull(2, body=with_image)]))
    assert items(problems) == ["#1", "#2"]
    assert "local path" in problems[0].message and "local file" in problems[1].message


LEGACY = PR_BODY.replace("Done:\n", "Remaining:\n\n- [ ] the follow-up\n- [ ] tracked (#44)\n\nDone:\n")
UNFINISHED = PR_BODY.replace("Done:\n", "Done:\n\n- [ ] the second unit (#44)\n")
BEFORE = "2026-10-12T10:39:12Z"
AFTER = "2026-10-12T10:39:13Z"


def test_a_pull_request_merged_before_the_rule_with_an_uncited_remaining_item_is_found() -> None:
    ctx = context(
        pulls=[
            pull(1, state="MERGED", body=LEGACY, merged=BEFORE),
            pull(2, state="OPEN", body=LEGACY),
            pull(3, state="MERGED", merged=BEFORE),
            pull(4, state="MERGED", body=LEGACY, merged=AFTER),
        ]
    )
    (problem,) = found(gh.pr_remaining_uncited, ctx)
    assert problem.item == "#1" and "1 Remaining item" in problem.message and "the follow-up" in problem.message


def test_a_pull_request_merged_since_the_rule_with_an_unticked_item_is_found() -> None:
    ctx = context(
        pulls=[
            pull(1, state="MERGED", body=UNFINISHED, merged=AFTER),
            pull(2, state="MERGED", body=UNFINISHED, merged=BEFORE),
            pull(3, state="MERGED", body=LEGACY, merged=AFTER),
            pull(4, state="MERGED", merged=AFTER),
            pull(5, state="OPEN", body=UNFINISHED),
        ]
    )
    problems = found(gh.pr_unfinished_items, ctx)
    assert items(problems) == ["#1", "#3"]
    assert "1 unticked Checklist item" in problems[0].message and "the second unit (#44)" in problems[0].message


def test_an_open_pull_request_with_an_unticked_item_breaks_the_template_rules() -> None:
    ctx = context(pulls=[pull(1, state="OPEN", body=UNFINISHED), pull(2, state="OPEN")])
    (problem,) = found(gh.pr_body, ctx)
    assert problem.item == "#1" and "the second unit" in problem.message and "not ticked" in problem.message


def test_a_stacked_pull_request_names_its_base_and_has_no_auto_merge() -> None:
    depends = PR_BODY.replace("Adds a check.", "Depends on #10\n\nAdds a check.")
    ctx = context(
        pulls=[
            pull(1, base="workouts-1"),
            pull(2, base="workouts-1", body=depends),
            pull(3, base="workouts-1", body=depends, auto_merge=True),
            pull(4, auto_merge=True),
        ]
    )
    assert items(found(gh.pr_stacked_depends, ctx)) == ["#1"]
    assert items(found(gh.pr_stacked_automerge, ctx)) == ["#3"]


def test_a_pull_request_based_on_a_merged_branch_needs_a_rebase() -> None:
    ctx = context(
        pulls=[pull(1, state="MERGED", head="part-1"), pull(2, base="part-1"), pull(3, base="other"), pull(4)]
    )
    (problem,) = found(gh.pr_base_merged, ctx)
    assert problem.item == "#2" and "git rebase --onto origin/main" in problem.message


def test_a_pull_request_merged_into_a_branch_leaves_its_issues_open() -> None:
    ctx = context(
        issues=[issue(20), issue(21, state="CLOSED"), issue(22)],
        pulls=[
            pull(1, state="MERGED", base="part-1", closes=(20, 21)),
            pull(2, state="MERGED", base="part-1", body=PR_BODY.replace("Adds a check.", "Fixes #22")),
            pull(3, state="MERGED", base="part-1", closes=(21,)),
            pull(4, state="MERGED", closes=(20,)),
        ],
    )
    problems = found(gh.pr_merged_into_base, ctx)
    assert items(problems) == ["#1", "#2"] and "#20 stay open" in problems[0].message


# --- labels and settings --------------------------------------------------------------------------------------------


def test_matching_labels_are_clean() -> None:
    assert found(gh.labels_match, context()) == []


def test_label_drift_is_named_per_label() -> None:
    drifted = [dict(label) for label in REAL_LABELS if label["name"] != "A-ai"]
    drifted[0]["color"] = "ffffff"
    drifted[1]["description"] = "Changed"
    drifted.append({"name": "stray", "color": "ffffff", "description": ""})
    problems = found(gh.labels_match, context(labels=drifted))
    by_item = {problem.item: problem.message for problem in problems}
    assert by_item["label A-ai"] == "is in labels.list but not on GitHub"
    assert "colour is ffffff" in by_item["label bug"] and "description is 'Changed'" in by_item["label parity"]
    assert by_item["label stray"] == "is on GitHub but not in labels.list"


SETTINGS: dict[str, Any] = {
    "allow_squash_merge": True,
    "allow_merge_commit": False,
    "allow_rebase_merge": False,
    "allow_auto_merge": True,
    "delete_branch_on_merge": True,
    "squash_merge_commit_title": "PR_TITLE",
    "squash_merge_commit_message": "PR_BODY",
}


def ruleset(**changes: Any) -> dict[str, Any]:
    """The real ruleset of main, with `changes` applied to its rules."""
    rules = [
        {"type": "deletion"},
        {"type": "non_fast_forward"},
        {"type": "required_linear_history"},
        {"type": "pull_request", "parameters": {"allowed_merge_methods": ["squash"]}},
        {
            "type": "required_status_checks",
            "parameters": {
                "strict_required_status_checks_policy": False,
                "required_status_checks": [{"context": name} for name in CONVENTIONS.merge.required_checks],
            },
        },
    ]
    base = {"enforcement": "active", "conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"]}}, "rules": rules}
    base.update(changes)
    return base


def test_matching_settings_and_ruleset_are_clean() -> None:
    assert found(gh.ruleset_matches, context(settings=SETTINGS, rulesets=[ruleset()])) == []


def test_a_setting_that_differs_is_named() -> None:
    settings = {**SETTINGS, "allow_merge_commit": True, "squash_merge_commit_message": "COMMIT_MESSAGES"}
    problems = found(gh.ruleset_matches, context(settings=settings, rulesets=[ruleset()]))
    assert items(problems) == ["setting allow_merge_commit", "setting squash_merge_commit_message"]


def test_a_ruleset_that_differs_is_named() -> None:
    rules = ruleset()["rules"]
    rules[2:3] = []  # no linear history
    rules[-1]["parameters"]["required_status_checks"].pop()
    rules[-1]["parameters"]["strict_required_status_checks_policy"] = True
    rules[2]["parameters"]["allowed_merge_methods"] = ["squash", "merge"]
    messages = " | ".join(p.message for p in found(gh.ruleset_matches, context(rulesets=[ruleset(rules=rules)])))
    assert "linear history is off" in messages and "merge methods are ['merge', 'squash']" in messages
    assert "required checks differ" in messages and "up to date is True" in messages


def test_a_default_branch_without_an_active_ruleset_is_found() -> None:
    assert "no active ruleset protects main" in found(gh.ruleset_matches, context(rulesets=[]))[0].message
    inactive = ruleset(enforcement="disabled")
    assert "no active ruleset" in found(gh.ruleset_matches, context(rulesets=[inactive]))[0].message


def test_settings_that_could_not_be_read_are_not_compared() -> None:
    assert found(gh.ruleset_matches, context(settings=None, rulesets=None)) == []


def test_every_github_check_has_code_and_an_entry() -> None:
    assert set(GITHUB_CHECKS) <= set(registered())
    assert raw_snapshot()["settings"] is None  # the default snapshot reads no settings


# --- pr.affected-unedited -----------------------------------------------------------------------------------------

#: The rule began on the 9th, a day before the pull requests below merged (at MERGED_AT).
MERGED_AT = "2026-10-10T08:00:00Z"
#: Days before the snapshot (2026-10-11 12:00): the merge was 1.17 days before it.
EDITED_BEFORE_THE_MERGE, EDITED_AFTER_THE_MERGE = 3.0, 0.5
AFFECTED = PR_BODY.replace("None (nothing cites it)", "- #20: step 2 is done\n- #21: now cites the old file\n- #22: x")


def affected_context(**kwargs: Any) -> Any:
    """A context whose Issues affected rule began on 2026-10-09."""
    rules = CONVENTIONS.pull_requests
    began = replace(rules.issues_affected, since=datetime(2026, 10, 9, tzinfo=UTC))
    return context(replace(CONVENTIONS, pull_requests=replace(rules, issues_affected=began)), **kwargs)


def test_a_merged_pull_request_whose_listed_issues_were_not_edited_since_is_found() -> None:
    ctx = affected_context(
        issues=[
            issue(20, edited=EDITED_BEFORE_THE_MERGE),
            issue(21, edited=EDITED_AFTER_THE_MERGE),
            issue(22, edited=EDITED_BEFORE_THE_MERGE),
        ],
        pulls=[pull(5, state="MERGED", body=AFFECTED, merged=MERGED_AT)],
    )
    (problem,) = found(gh.pr_affected_unedited, ctx)
    assert problem.item == "#5" and "#20, #22" in problem.message and "#21" not in problem.message
    assert "not edited since it merged" in problem.message


def test_listed_issues_edited_after_the_merge_are_fine() -> None:
    ctx = affected_context(
        issues=[issue(n, edited=EDITED_AFTER_THE_MERGE) for n in (20, 21, 22)],
        pulls=[pull(5, state="MERGED", body=AFFECTED, merged=MERGED_AT)],
    )
    assert found(gh.pr_affected_unedited, ctx) == []


def test_a_comment_after_the_merge_is_not_the_edit() -> None:
    ctx = affected_context(
        issues=[
            issue(20, edited=EDITED_BEFORE_THE_MERGE, updated=EDITED_AFTER_THE_MERGE),
            issue(21, edited=EDITED_AFTER_THE_MERGE, updated=EDITED_AFTER_THE_MERGE),
            issue(22, updated=EDITED_AFTER_THE_MERGE),  # edit time unknown: not judged
        ],
        pulls=[pull(5, state="MERGED", body=AFFECTED, merged=MERGED_AT)],
    )
    (problem,) = found(gh.pr_affected_unedited, ctx)
    assert "#20" in problem.message and "#21" not in problem.message and "#22" not in problem.message
    assert "a comment does not count" in problem.message


def graphql_page(*nodes: dict[str, Any]) -> str:
    """A `gh api graphql` answer holding one last page of issues."""
    page = {"pageInfo": {"hasNextPage": False, "endCursor": None}, "nodes": list(nodes)}
    return json.dumps({"data": {"repository": {"issues": page}}})


def edit_page(numbers: list[int], cursor: str | None, more: bool) -> str:
    """A `gh api graphql` answer with one page of issues numbered `numbers`."""
    nodes: list[dict[str, Any]] = [
        {"number": n, "createdAt": "2026-10-01T00:00:00Z", "lastEditedAt": None, "timelineItems": {"nodes": []}}
        for n in numbers
    ]
    page = {"pageInfo": {"hasNextPage": more, "endCursor": cursor}, "nodes": nodes}
    return json.dumps({"data": {"repository": {"issues": page}}})


def test_two_pages_of_edit_times_are_both_read() -> None:
    pages = iter([edit_page([1], "c1", True), edit_page([2], None, False)])
    asked: list[list[str]] = []

    def run(arguments: list[str]) -> str:
        asked.append(arguments)
        return next(pages)

    assert sorted(gh.fetch_edit_times(run, "a/b")) == [1, 2]
    assert "after=c1" in asked[1] and not any(token.startswith("after=") for token in asked[0])


@pytest.mark.parametrize("cursor", [None, "", "c1"])
def test_a_next_page_with_no_or_a_repeated_cursor_stops_with_a_notice(cursor: str | None) -> None:
    pages = iter([edit_page([1], "c1", True)] + [edit_page([2], cursor, True)] * 5)

    def run(arguments: list[str]) -> str:
        if arguments[:2] == ["repo", "view"]:
            return json.dumps({"nameWithOwner": "a/b", "defaultBranchRef": {"name": "main"}})
        if arguments[:2] == ["api", "graphql"]:
            return next(pages)
        if arguments[0] == "api":
            raise OSError("no admin")
        return "[]"

    raw = gh.fetch_raw(run)
    assert any(
        "paging does not end" in n and "after 2 pages" in n and "pr.affected-unedited did not run" in n
        for n in raw["notices"]
    )


def test_endless_paging_is_capped(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(gh, "EDIT_PAGE_LIMIT", 3)
    counter = iter(range(100))
    calls: list[int] = []

    def run(arguments: list[str]) -> str:
        calls.append(1)
        return edit_page([next(counter)], f"c{next(counter)}", True)

    with pytest.raises(ValueError, match="more than 3 pages"):
        gh.fetch_edit_times(run, "a/b")
    assert len(calls) == 3


def test_fetch_reads_the_body_edit_and_label_times_and_a_failure_is_a_notice() -> None:
    def run(arguments: list[str]) -> str:
        if arguments[:2] == ["repo", "view"]:
            return json.dumps({"nameWithOwner": "a/b", "defaultBranchRef": {"name": "main"}})
        if arguments[:2] == ["issue", "list"]:
            return json.dumps([issue(1), issue(2)])
        if arguments[:2] == ["api", "graphql"]:
            return graphql_page(
                {
                    "number": 1,
                    "createdAt": "2026-10-01T00:00:00Z",
                    "lastEditedAt": "2026-10-05T00:00:00Z",
                    "timelineItems": {
                        "nodes": [
                            {"createdAt": "2026-10-07T00:00:00Z", "actor": {"__typename": "User", "login": "ann"}},
                            {"createdAt": "2026-10-09T00:00:00Z", "actor": {"__typename": "Bot", "login": "gha"}},
                        ]
                    },
                },
                {
                    "number": 2,
                    "createdAt": "2026-10-02T00:00:00Z",
                    "lastEditedAt": None,
                    "timelineItems": {"nodes": []},
                },
            )
        if arguments[0] == "api":
            raise OSError("no admin")
        return "[]"

    shot = gh.parse_snapshot(gh.fetch_raw(run))
    first, second = shot.issue(1), shot.issue(2)
    assert first is not None and first.edited == datetime(2026, 10, 7, tzinfo=UTC)
    assert second is not None and second.edited == datetime(2026, 10, 2, tzinfo=UTC)

    def broken(arguments: list[str]) -> str:
        if arguments[:2] == ["api", "graphql"]:
            raise OSError("graphql failed")
        return run(arguments)

    raw = gh.fetch_raw(broken)
    assert any("pr.affected-unedited did not run" in notice for notice in raw["notices"])
    unread = gh.parse_snapshot(raw).issue(1)
    assert unread is not None and unread.edited is None


def test_a_closed_issue_a_pull_request_and_a_missing_number_are_not_held_to_the_edit() -> None:
    ctx = affected_context(
        issues=[issue(20, state="CLOSED", edited=EDITED_BEFORE_THE_MERGE)],
        pulls=[pull(21, state="OPEN"), pull(5, state="MERGED", body=AFFECTED, merged=MERGED_AT)],
    )
    assert found(gh.pr_affected_unedited, ctx) == []


def test_a_pull_request_merged_before_the_rule_or_listing_none_or_still_open_is_not_held_to_it() -> None:
    ctx = affected_context(
        issues=[issue(n, edited=EDITED_BEFORE_THE_MERGE) for n in (20, 21, 22)],
        pulls=[
            pull(1, state="MERGED", body=AFFECTED, merged="2026-10-08T08:00:00Z"),
            pull(2, state="MERGED", merged=MERGED_AT),
            pull(3, state="OPEN", body=AFFECTED),
            pull(4, state="CLOSED", body=AFFECTED),
        ],
    )
    assert found(gh.pr_affected_unedited, ctx) == []
