# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.issue_form: the draft skeleton, the pre-creation check and the labels of a new issue.

Every body is synthetic. The pages "on main" are a made-up tree (`audit_support.FakeTree`) and the states of the
issues and pull requests a body cites are an injected lookup, so nothing here touches git or the network.
"""

from typing import Any

import pytest
from audit_support import CONVENTIONS
from conventions_support import REPO
from issue_support import GOOD, TREE, body

from coney_tools import issue_form
from coney_tools.config import ConfigError
from coney_tools.issue_form import CheckOptions, IssueProblem

TASK = issue_form.load_form(REPO, CONVENTIONS, "task")
BUG = issue_form.load_form(REPO, CONVENTIONS, "bug")


def problems(text: str, title: str = "Reimplement the Throw orders", **options: Any) -> list[IssueProblem]:
    """The problems of `text` as a task issue with the made-up tree."""
    options.setdefault("tree", TREE)
    return issue_form.check_issue(title, text, TASK, CONVENTIONS, CheckOptions(**options)).problems


def messages(text: str, title: str = "Reimplement the Throw orders", **options: Any) -> str:
    """All problem messages of `text` joined, to search in."""
    return "\n".join(problem.message for problem in problems(text, title, **options))


def kinds(text: str, **options: Any) -> set[str]:
    """The kinds of the problems of `text`."""
    return {problem.kind for problem in problems(text, **options)}


# --- the form, read from the conventions and the form's own file ---------------------------------------------------


def test_the_task_form_is_read_in_the_conventions_order_with_the_files_guidance() -> None:
    assert [field.id for field in TASK.fields] == list(
        section.id for section in CONVENTIONS.issues.forms["task"].sections
    )
    area = next(field for field in TASK.fields if field.id == "area")
    assert area.kind == "dropdown"
    assert area.options == CONVENTIONS.labels.names("area")
    summary = TASK.fields[0]
    assert summary.required and summary.description.startswith("Start with the problem")
    assert TASK.labels == ("task", "S-needs-triage")


def test_the_bug_form_has_dropdowns_and_a_required_checkbox() -> None:
    started = next(field for field in BUG.fields if field.id == "started")
    assert started.kind == "dropdown" and "Story" in started.options
    box = next(field for field in BUG.fields if field.id == "no-game-files")
    assert box.kind == "checkboxes" and box.required_options and "game files" in box.required_options[0]


def test_an_unknown_form_or_a_form_file_without_a_field_is_a_config_error(tmp_path: object) -> None:
    with pytest.raises(ConfigError, match="no form 'epic'"):
        issue_form.load_form(REPO, CONVENTIONS, "epic")
    with pytest.raises(ConfigError, match="ISSUE_TEMPLATE"):
        issue_form.load_form(tmp_path, CONVENTIONS, "task")  # type: ignore[arg-type]


# --- the draft ----------------------------------------------------------------------------------------------------


def test_the_draft_has_the_forms_headings_in_order_at_level_three() -> None:
    draft = issue_form.draft_body(TASK, CONVENTIONS)
    headings = [line for line in draft.splitlines() if line.startswith("#")]
    assert headings == [f"### {section.heading}" for section in CONVENTIONS.issues.forms["task"].sections]


def test_every_section_of_the_draft_is_preceded_by_a_comment_with_its_rules() -> None:
    lines = issue_form.draft_body(TASK, CONVENTIONS).splitlines()
    for index, line in enumerate(lines):
        if line.startswith("### "):
            comment = "\n".join(lines[:index])
            tail = comment[comment.rfind("<!--") :]
            assert tail.rstrip().endswith("-->"), line
    draft = "\n".join(lines)
    assert "required" in draft and "optional" in draft
    assert "Start with the problem or goal in plain words" in draft  # the form's own description


def test_the_draft_lists_the_area_options_the_screenshots_convention_and_the_from_line() -> None:
    draft = issue_form.draft_body(TASK, CONVENTIONS)
    for label in CONVENTIONS.labels.names("area"):
        assert label in draft
    assert "Screenshots: none (<reason>)" in draft
    assert "\nFrom:" in draft
    assert "../blob/main/<path>" in draft  # the link form, from issues.links


def test_the_bug_draft_has_checkboxes_and_the_dropdown_choices() -> None:
    draft = issue_form.draft_body(BUG, CONVENTIONS)
    assert "- [ ] I attached no game files" in draft
    assert "Debug menu jump" in draft and "Sometimes" in draft


def test_a_draft_left_blank_fails_the_check_on_content_but_not_on_headings() -> None:
    result = issue_form.check_issue(
        "A title", issue_form.draft_body(TASK, CONVENTIONS), TASK, CONVENTIONS, CheckOptions(tree=TREE)
    )
    assert result.problems
    assert "headings" not in {problem.kind for problem in result.problems}
    assert {"empty", "choices", "screenshots"} <= {problem.kind for problem in result.problems}


# --- a good body ----------------------------------------------------------------------------------------------------


def test_a_body_in_the_structure_of_issue_340_passes() -> None:
    result = issue_form.check_issue("Reimplement the Throw orders", GOOD, TASK, CONVENTIONS, CheckOptions(tree=TREE))
    assert result.problems == []
    assert result.area == "A-ai"
    assert result.notices == []


def test_the_draft_comments_do_not_count_as_text_and_may_stay() -> None:
    commented = "<!-- from the draft -->\n" + GOOD.replace("### Area", "<!-- area: one of ... -->\n### Area")
    assert problems(commented) == []


def test_crlf_line_ends_pass() -> None:
    assert problems(GOOD.replace("\n", "\r\n")) == []


def test_optional_sections_may_be_left_out() -> None:
    no_optional = (
        "### Summary\n\nThe pause menu is a stand-in.\n\n### Area\n\nA-hud\n\n"
        '### What "done" means\n\nA test.\n\n### Screenshots\n\nScreenshots: none (menu text)\n'
    )
    assert problems(no_optional) == []


# --- headings -----------------------------------------------------------------------------------------------------


def test_a_body_without_headings_is_told_to_start_from_the_draft() -> None:
    text = messages("Follow-up to #346. The physics work does not wire it in.\n\nBlocked on #346.\n")
    assert "no form headings" in text and "issue draft task" in text


def test_a_heading_at_level_two_is_named_with_the_level_the_form_uses() -> None:
    found = problems(GOOD.replace("### Screenshots", "## Screenshots"))
    assert [problem.kind for problem in found] == ["headings"]
    assert "'## Screenshots'" in found[0].message and "'### Screenshots'" in found[0].message


def test_a_heading_at_level_one_is_refused() -> None:
    assert "'# Summary'" in messages(GOOD.replace("### Summary", "# Summary"))


def test_a_heading_in_the_wrong_case_gets_the_forms_spelling() -> None:
    text = messages(GOOD.replace("### Research page", "### Research Page"))
    assert "'Research Page'" in text and "'Research page'" in text


def test_a_heading_that_is_not_in_the_form_is_an_extra() -> None:
    text = messages(GOOD.replace("### Blocked by", "### Notes"))
    assert "'Notes' is not a section of the task form" in text


def test_a_missing_required_section_is_named() -> None:
    text = messages(GOOD.replace('### What "done" means', "### Done"))
    assert "missing the required section '### What \"done\" means'" in text


def test_sections_out_of_order_are_reported_with_the_forms_order() -> None:
    swapped = GOOD.replace("### Area\n\nA-ai\n\n", "").replace("### Summary", "### Area\n\nA-ai\n\n### Summary")
    text = messages(swapped)
    assert "out of order" in text and "Summary, Area, Research page" in text


def test_a_heading_written_twice_is_reported() -> None:
    assert "'Area' appears twice" in messages(GOOD + "\n### Area\n\nA-ai\n")


def test_text_before_the_first_heading_belongs_to_no_section() -> None:
    assert "before the first heading" in messages("Follow-up to #346.\n\n" + GOOD)


def test_a_heading_inside_a_code_fence_is_not_a_heading() -> None:
    fenced = GOOD.replace("- [ ] Throw", "```text\n## Notes\n```\n- [ ] Throw")
    assert problems(fenced) == []


# --- required sections, placeholders and choices ------------------------------------------------------------------


def test_a_required_section_left_empty_or_as_a_placeholder_is_refused() -> None:
    assert "'Summary' is empty" in messages(body(summary=""))
    assert "'Summary' is 'TBD'" in messages(body(summary="TBD"))
    assert "'What \"done\" means' is 'x'" in messages(body(done="x"))


def test_the_area_must_be_one_of_the_forms_options() -> None:
    found = problems(body(area="docs"))
    assert [problem.kind for problem in found] == ["choices"]
    assert "Area is 'docs'" in found[0].message and "A-tools" in found[0].message


def test_an_area_near_an_option_gets_the_option_as_a_hint() -> None:
    assert "did you mean 'A-tools'" in messages(body(area="A-tool"))


def test_a_required_checkbox_must_be_ticked_in_the_bug_form() -> None:
    bug = (
        "### Coney version or commit\n\nabc123\n\n### How did you start the game?\n\nStory\n\n"
        "### Steps to reproduce\n\n1. Start\n\n### What happens\n\nIt breaks.\n\n"
        "### How often does it happen?\n\nAlways\n\n### Screenshots\n\nScreenshots: none (a crash)\n\n"
        "### No game files\n\n"
        "- [ ] I attached no game files: no disc images, BIOS, extracted assets, save files or game text.\n"
    )
    options = CheckOptions(tree=TREE)
    found = issue_form.check_issue("A crash", bug, BUG, CONVENTIONS, options).problems
    assert [problem.kind for problem in found] == ["choices"] and "tick" in found[0].message
    ticked = bug.replace("- [ ]", "- [x]")
    assert issue_form.check_issue("A crash", ticked, BUG, CONVENTIONS, options).problems == []
    wrong = ticked.replace("Story", "Pause")
    assert (
        "How did you start the game? is 'Pause'"
        in issue_form.check_issue("A crash", wrong, BUG, CONVENTIONS, options).problems[0].message
    )


def test_a_dropdown_option_that_starts_with_a_dash_is_accepted_plain_or_in_backticks() -> None:
    """The bug form's `--play-level` option is rendered by GitHub as plain text, and may be written in backticks."""
    bug = (
        "### Coney version or commit\n\nabc123\n\n### How did you start the game?\n\n--play-level\n\n"
        "### Steps to reproduce\n\n1. Start\n\n### What happens\n\nIt breaks.\n\n"
        "### How often does it happen?\n\nAlways\n\n### Screenshots\n\nScreenshots: none (a crash)\n\n"
        "### No game files\n\n"
        "- [x] I attached no game files: no disc images, BIOS, extracted assets, save files or game text.\n"
    )
    options = CheckOptions(tree=TREE)
    for written in ("--play-level", "`--play-level`", "- --play-level"):
        text = bug.replace("\n--play-level\n", f"\n{written}\n", 1)
        assert issue_form.check_issue("A crash", text, BUG, CONVENTIONS, options).problems == [], written


# --- links to pages ---------------------------------------------------------------------------------------------


def test_a_docs_path_in_plain_text_must_be_a_link_when_the_page_is_on_main() -> None:
    text = messages(body(summary="See docs/research/ai-goals.md for the goal."))
    assert "write [docs/research/ai-goals.md](../blob/main/docs/research/ai-goals.md)" in text


def test_a_research_path_in_plain_text_is_refused_too() -> None:
    assert "research/missions.yaml" in messages(body(summary="The table is research/missions.yaml."))


def test_a_page_that_is_not_on_main_may_be_named_in_plain_text_with_a_note() -> None:
    note = "docs/research/ai-reimpl.md (new in pull request #317, not on `main` yet)"
    assert "ai-reimpl" not in messages(body(summary=note))
    bare = messages(body(summary="See docs/research/ai-reimpl.md."))
    assert "is not on main" in bare and "not on `main` yet" in bare


def test_a_note_does_not_excuse_a_plain_path_that_is_on_main() -> None:
    text = messages(body(summary="docs/research/ai-goals.md (not on `main` yet)"))
    assert "is on main: write [docs/research/ai-goals.md]" in text


def test_a_link_to_a_page_that_is_not_on_main_is_refused() -> None:
    text = messages(body(summary="See [the page](../blob/main/docs/research/nope.md)."))
    assert "docs/research/nope.md, which is not on main" in text


def test_a_link_to_a_missing_anchor_is_refused_and_a_present_one_passes() -> None:
    bad = messages(body(summary="See [x](../blob/main/docs/research/ai-goals.md#nope)."))
    assert "ai-goals.md#nope, which has no such section" in bad
    assert problems(body(summary="See [x](../blob/main/docs/research/ai-goals.md#goal-object-pile).")) == []


def test_a_link_on_another_branch_or_without_the_blob_prefix_is_refused() -> None:
    assert "'dev'" in messages(body(summary="See [x](../blob/dev/docs/research/ai-goals.md)."))
    assert "does not work from an issue page" in messages(body(summary="See [x](docs/research/ai-goals.md)."))


def test_without_the_main_tree_the_links_are_not_checked_and_a_notice_says_so() -> None:
    result = issue_form.check_issue(
        "A title",
        body(summary="See [x](../blob/main/docs/research/nope.md)."),
        TASK,
        CONVENTIONS,
        CheckOptions(tree=None),
    )
    assert result.problems == [] and "origin/main" in result.notices[0]


# --- screenshots, provenance, wording ----------------------------------------------------------------------------


def test_the_screenshots_section_needs_an_image_or_the_none_line() -> None:
    found = problems(body(screenshots="It looks wrong."))
    assert found and found[0].kind == "screenshots" and "'### Screenshots'" in found[0].message
    assert "none (<reason>)" in found[0].message
    assert problems(body(screenshots="![the door](https://github.com/user-attachments/assets/abc)")) == []


def test_a_local_image_needs_an_attachment() -> None:
    local = body(screenshots="![the door](./shots/door.png)")
    assert "is a local file" in messages(local)
    assert problems(local, uploaded_only=False) == []


def test_a_leftover_needs_a_from_line_with_text() -> None:
    no_from = body(screenshots="Screenshots: none (AI logic)")
    assert problems(no_from) == []
    assert "From:" in messages(no_from, leftover=True)
    empty = body(screenshots="Screenshots: none (AI logic)\n\nFrom:")
    assert "'From:' line is empty" in messages(empty, leftover=True)
    assert problems(GOOD, leftover=True) == []


def test_a_bold_from_line_counts() -> None:
    bold = body(screenshots="Screenshots: none (AI logic)\n\n**From:** user playtest, 2026-10-11")
    assert problems(bold, leftover=True) == []


def test_owner_is_refused_for_the_person_who_directs_the_project() -> None:
    text = messages(body(summary="The owner asked for this."))
    assert "'owner'" in text and "'user'" in text
    assert kinds(body(summary="The owner asked for this.")) == {"wording"}


def test_the_repository_address_is_refused() -> None:
    text = messages(
        body(summary="See https://github.com/someone/coney/issues/1."), address_terms=("github.com/someone/coney",)
    )
    assert "address" in text


def test_the_title_must_be_one_line_of_text_and_not_say_owner() -> None:
    assert issue_form.check_issue("", GOOD, TASK, CONVENTIONS, CheckOptions(tree=TREE)).problems[0].kind == "title"
    assert "one line" in "\n".join(
        p.message for p in issue_form.check_issue("a\nb", GOOD, TASK, CONVENTIONS, CheckOptions(tree=TREE)).problems
    )
    assert "'owner'" in messages(GOOD, title="The owner's wish")


# --- the statuses of cited issues and pull requests (--online) -------------------------------------------------


def test_status_claims_read_the_state_a_number_is_called() -> None:
    claims = issue_form.status_claims(
        "Pull request #317 (open, not merged yet), #318 (merged), #319 is closed, #320 (a pull request, open),"
        " #321 (closed without merging), #322 and #323."
    )
    assert claims == [(317, "open"), (318, "merged"), (319, "closed"), (320, "open"), (321, "closed")]


def test_a_statement_that_a_pull_request_is_open_is_checked_against_github() -> None:
    states = {317: "merged", 335: "open"}.get
    text = messages(GOOD, states=states)
    assert "#317 is called open but is merged" in text
    assert kinds(GOOD, states=states) == {"status"}
    assert problems(GOOD, states={317: "open", 335: "open"}.get) == []


def test_a_number_github_does_not_know_is_not_a_problem() -> None:
    assert problems(GOOD, states=lambda number: None) == []


def test_closed_may_describe_a_merged_pull_request() -> None:
    closed = GOOD.replace("(a pull request, open, not merged yet)", "(closed)").replace(
        "(open, not merged yet)", "(merged)"
    )
    assert problems(closed, states={317: "merged", 335: "open"}.get) == []


# --- the labels of a new issue ---------------------------------------------------------------------------------


def test_the_labels_are_the_forms_plus_the_area_label() -> None:
    labels, found = issue_form.labels_for(TASK, "A-ai", [], CONVENTIONS)
    assert labels == ["task", "S-needs-triage", "A-ai"] and found == []


def test_a_label_of_the_same_family_replaces_the_forms_and_others_are_added() -> None:
    labels, found = issue_form.labels_for(TASK, "A-tools", ["docs", "P-low", "S-ready-to-start"], CONVENTIONS)
    assert labels == ["A-tools", "docs", "P-low", "S-ready-to-start"] and found == []


def test_an_unknown_label_or_two_priorities_are_refused() -> None:
    _, found = issue_form.labels_for(TASK, "A-ai", ["urgent", "P-low", "P-high"], CONVENTIONS)
    text = " ".join(found)
    assert "'urgent' is not in labels.list" in text and "2 priority labels" in text


def test_the_bug_form_does_not_need_an_area_label() -> None:
    labels, found = issue_form.labels_for(BUG, None, [], CONVENTIONS)
    assert labels == ["bug", "S-needs-triage"] and found == []


def test_the_form_is_found_from_the_headings_the_title_or_the_command_line_default() -> None:
    assert issue_form.detect_form("A title", GOOD, CONVENTIONS) == "task"
    assert issue_form.detect_form("Bug: a crash", "nothing", CONVENTIONS) == "bug"
    assert (
        issue_form.detect_form("A title", "### Steps to reproduce\n\n1. x\n\n### What happens\n\nx", CONVENTIONS)
        == "bug"
    )
    assert issue_form.detect_form("A title", "no headings", CONVENTIONS) == CONVENTIONS.issues.cli_uses_form


def test_state_names_and_kinds_are_what_the_docs_say() -> None:
    assert issue_form.KINDS == (
        "title",
        "headings",
        "empty",
        "choices",
        "screenshots",
        "from",
        "links",
        "wording",
        "status",
    )
