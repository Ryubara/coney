# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.repo_checks: pointer files, commit titles and pull request descriptions."""

import re
from pathlib import Path

import pytest
from conventions_support import REPO, conventions_with
from git_support import git, make_branch, message

from coney_tools import audit_text, repo_checks
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.repo_checks import (
    branch_trailer_models,
    check_body,
    check_docs,
    check_pointer_files,
    check_title,
    legacy_remaining_items,
    legacy_uncited_remaining,
    strip_comments,
    trailer_models,
    unfinished_items,
)

POINTERS = load_conventions(REPO).paths.pointer_files
# The import line is joined on, not written after a "\n" escape, which would read as an email address ("n@...").
GOOD = "# CLAUDE.md\n\nRead `AGENTS.md`.\n\n" + POINTERS.line + "\n"


def write(root: Path, claude: str | None = GOOD, gemini: str | None = GOOD.replace("CLAUDE", "GEMINI")) -> Path:
    """Write the pointer files into `root`; None leaves that one out."""
    for name, text in (("CLAUDE.md", claude), ("GEMINI.md", gemini)):
        if text is not None:
            (root / name).write_text(text, encoding="utf-8")
    return root


def test_clean_pointer_files_pass(tmp_path: Path) -> None:
    assert check_pointer_files(write(tmp_path), POINTERS) == []


def test_missing_pointer_file_is_reported(tmp_path: Path) -> None:
    problems = check_pointer_files(write(tmp_path, gemini=None), POINTERS)
    assert len(problems) == 1 and "GEMINI.md" in problems[0]


def test_missing_import_line_is_reported(tmp_path: Path) -> None:
    problems = check_pointer_files(write(tmp_path, claude="# CLAUDE.md\n\nSee AGENTS.md\n"), POINTERS)
    assert len(problems) == 1 and "@AGENTS.md" in problems[0]


def test_oversized_pointer_file_is_reported(tmp_path: Path) -> None:
    problems = check_pointer_files(write(tmp_path, claude=GOOD + "x" * POINTERS.max_bytes), POINTERS)
    assert len(problems) == 1 and "CLAUDE.md" in problems[0]


def test_the_real_repository_is_clean() -> None:
    assert check_pointer_files(REPO, POINTERS) == []


RULES = conventions_with(
    ("commits.title.areas", {"docs": "documentation", "ci": "workflows"}),
    ("commits.title.verbs", ["Add", "Fix", "Update"]),
).commits.title


@pytest.mark.parametrize(
    "title",
    [
        "docs: Add the contributor documents",
        "ci: Fix the sanitizer and clang-tidy jobs",
        "docs: Update the build guide (BREAKING)",
        "ci: Add " + "x" * 64,  # exactly 72 characters
    ],
)
def test_good_titles_pass(title: str) -> None:
    assert check_title(title, RULES) == []


@pytest.mark.parametrize(
    ("title", "reason"),
    [
        ("Add the contributor documents", "not in the form"),
        ("docs:Add the documents", "not in the form"),
        ("docs: Add", "not in the form"),
        ("web: Add the site", "unknown area 'web'"),
        ("docs: Added the documents", "'Added' is not one of the verbs"),
        ("docs: Record the documents", "'Record' is not one of the verbs"),
        ("docs: Add the documents.", "ends with a period"),
        ("docs: Add the documents ", "ends with whitespace"),
        ("ci: Add " + "x" * 65, "73 characters; 72 at most"),
        ("docs: Add (BREAKING) the guide", "(BREAKING) must end the title"),
    ],
)
def test_bad_titles_are_refused_with_the_reason(title: str, reason: str) -> None:
    problems = check_title(title, RULES)
    assert any(reason in problem for problem in problems), problems


def test_every_broken_rule_is_reported() -> None:
    assert len(check_title("web: Added the guide.", RULES)) == 3


def test_the_real_rules_load_and_accept_a_real_title() -> None:
    rules = load_conventions(REPO).commits.title
    assert check_title("ci: Add the pull request title check", rules) == []


# --- pull request descriptions -----------------------------------------------------------------------------------

BODY_RULES = load_conventions(REPO).pull_requests
CHECKLIST = "Done:\n\n- [x] The check\n"


NO_SHOTS = "Screenshots: none (a docs-only change, nothing visible)"


def body(
    summary: str = "Adds a check.",
    written_by: str = "an AI",
    model: str = "Claude Sonnet 5.5",
    testing: str = "- `pytest python/tests`: 120 passed",
    screenshots: str = NO_SHOTS,
) -> str:
    """A description in the template's form; the arguments fill the parts the checks read."""
    return (
        f"## Summary\n\n{summary}\n\n## Checklist\n\n{CHECKLIST}\n"
        f"## Testing\n\n{testing}\n\n## Screenshots\n\n{screenshots}\n\n"
        f"## AI use\n\n- **Commits written by:** {written_by}\n- **Model:** {model}\n"
    )


def test_a_filled_in_body_passes() -> None:
    assert check_body(body(), BODY_RULES) == []


@pytest.mark.parametrize(
    "written_by",
    ["a person", "an AI", "an AI, edited by a person", "a person, with AI help", "An AI", "an AI.", "**an AI**"],
)
def test_every_allowed_choice_passes(written_by: str) -> None:
    assert check_body(body(written_by=written_by), BODY_RULES) == []


def test_windows_line_endings_and_extra_text_pass() -> None:
    text = body().replace("Adds a check.", "Adds a check.\n\n### Why\n\nBecause.").replace("\n", "\r\n")
    assert check_body(text, BODY_RULES) == []


@pytest.mark.parametrize("section", ["Summary", "Checklist", "Testing", "Screenshots", "AI use"])
def test_a_missing_section_is_reported(section: str) -> None:
    problems = check_body(body().replace(f"## {section}\n", ""), BODY_RULES)
    assert any(f"missing the section '## {section}'" in problem for problem in problems), problems


def test_a_heading_at_another_level_does_not_count() -> None:
    problems = check_body(body().replace("## Checklist", "### Checklist"), BODY_RULES)
    assert any("'## Checklist'" in problem for problem in problems), problems


def test_a_heading_inside_a_comment_does_not_count() -> None:
    text = body().replace("## Checklist\n", "<!--\n## Checklist\n-->\n")
    assert any("'## Checklist'" in problem for problem in check_body(text, BODY_RULES))


def test_an_empty_body_reports_every_section() -> None:
    assert len(check_body("", BODY_RULES)) == 5


@pytest.mark.parametrize("summary", ["", "   ", "<!-- One short paragraph. -->", "<!--\nmany\nlines\n-->", "..."])
def test_an_empty_summary_is_reported(summary: str) -> None:
    problems = check_body(body(summary=summary), BODY_RULES)
    assert any("'## Summary' is empty" in problem for problem in problems), problems


def test_an_empty_checklist_is_not_refused() -> None:
    assert check_body(body().replace(CHECKLIST, ""), BODY_RULES) == []


@pytest.mark.parametrize("testing", ["", "   ", "<!-- What was run and the results. -->", "- ...", "..."])
def test_an_empty_testing_section_is_reported(testing: str) -> None:
    problems = check_body(body(testing=testing), BODY_RULES)
    assert any("'## Testing' is empty" in problem for problem in problems), problems


@pytest.mark.parametrize(
    "screenshots",
    [
        "![Coney after](https://github.com/user-attachments/assets/1234-abcd)",
        "![](https://example.invalid/shot.png)",
        '<img width="640" alt="after" src="https://example.invalid/after.png">',
        "<IMG src='https://example.invalid/after.png'>",
        '<img src="https://github.com/user-attachments/assets/1234-abcd" width="320">',
        '![after](https://example.invalid/after.png "Coney after")',
        "![after](<https://example.invalid/a b.png>)",
        "Before: https://github.com/user-attachments/assets/1234-abcd\n"
        "After: https://github.com/user-attachments/files/5/x.mp4",
        "| Before | After |\n| --- | --- |\n| ![b](https://example.invalid/b.png) | ![a](http://x.invalid/a.png) |",
        "- **Coney before:** ...\n- **Coney after:** ![after](https://example.invalid/after.png)",
        "Screenshots: none (no visible change)",
        "screenshots: NONE (docs only)",
        "- **Screenshots:** none (research page only)",
        "Screenshots: none (the change is a log line (not shown))",
    ],
)
def test_screenshots_with_an_image_or_a_reason_pass(screenshots: str) -> None:
    assert check_body(body(screenshots=screenshots), BODY_RULES) == []


@pytest.mark.parametrize(
    "screenshots",
    [
        "",
        "   ",
        "<!-- shot: before.png -->",
        "<!--\nScreenshots: none (no visible change)\n-->",
        "...",
        "- **Coney before:** ...\n- **Coney after:** ...\n- **Original (PCSX2):** ...",
        "Screenshots: none",
        "Screenshots: none ()",
        "Screenshots: none (   )",
        "Screenshots: none (<reason>)",
        "Screenshots: none (...)",
        "Screenshots: nothing (no change)",
        "I will add a screenshot later.",
        "[a link](https://example.invalid/after.png)",
    ],
)
def test_screenshots_without_an_image_or_a_reason_are_reported(screenshots: str) -> None:
    problems = check_body(body(screenshots=screenshots), BODY_RULES)
    assert any("'## Screenshots'" in problem and "Screenshots: none" in problem for problem in problems), problems


@pytest.mark.parametrize(
    ("screenshots", "path"),
    [
        ("![Coney after](./shots/x.png)", "./shots/x.png"),
        ("![](shot.png)", "shot.png"),
        ("![after](shots/after.png)", "shots/after.png"),
        ('![after](../shots/after.png "Coney after")', "../shots/after.png"),
        ("![after](<./shots/my after.png>)", "./shots/my after.png"),
        ("![after](C:\\scratch\\shots\\after.png)", "C:\\scratch\\shots\\after.png"),
        ("![after](file:///tmp/after.png)", "file:///tmp/after.png"),
        ('<img alt="after" src="./shots/x.png">', "./shots/x.png"),
        ("<IMG src='after.png'>", "after.png"),
        ("![b](https://github.com/user-attachments/assets/1234-abcd)\n![a](./shots/a.png)", "./shots/a.png"),
        ("Screenshots: none (no visible change)\n![a](./shots/a.png)", "./shots/a.png"),
    ],
)
def test_an_image_that_is_a_local_file_is_reported(screenshots: str, path: str) -> None:
    problems = check_body(body(screenshots=screenshots), BODY_RULES)
    expected = f"'{path}' is a local file: attach it with gh --attach (gh 2.99+) so it uploads"
    assert any("'## Screenshots'" in problem and expected in problem for problem in problems), problems


def test_each_local_image_is_reported_once() -> None:
    problems = check_body(body(screenshots="![b](./shots/b.png)\n![a](./shots/a.png)"), BODY_RULES)
    assert len([problem for problem in problems if "is a local file" in problem]) == 2


@pytest.mark.parametrize(
    ("summary", "path"),
    [
        ("The captures are in scratch/pr-testing-shots/shots.", "scratch/pr-testing-shots/shots"),
        ("See ../../scratch/x/PR.md for more.", "../../scratch/x/PR.md"),
        (r"Saved at C:\Repositories\Coney\scratch\x\a.png", r"C:\Repositories\Coney\scratch\x\a.png"),
        ("Saved at c:/Repositories/Coney/scratch/x/a.png.", "c:/Repositories/Coney/scratch/x/a.png"),
        (r"Saved at D:\shots", r"D:\shots"),
        ("The files are in ../../worktrees/coney/x.", "../../worktrees/coney/x"),
        (r"Built in scratch\build", r"scratch\build"),
    ],
)
def test_a_local_path_in_the_text_is_reported(summary: str, path: str) -> None:
    problems = check_body(body(summary=summary), BODY_RULES)
    assert any(f"'{path}' is a local path" in problem for problem in problems), problems


def test_a_local_path_in_any_section_is_reported() -> None:
    problems = check_body(body(testing="- ran it from scratch/pr/x.log: 12 passed"), BODY_RULES)
    assert any("'scratch/pr/x.log' is a local path" in problem for problem in problems), problems


@pytest.mark.parametrize(
    "summary",
    [
        "Run `cd scratch/<branch>` first.",
        "```sh\ncd scratch/x\ngh pr create --attach ./shots/a.png\n```",
        "~~~\n" + r"C:\Repositories\Coney" + "\n~~~",
        "See https://example.invalid/scratch/x and https://example.invalid/a/../../b.",
        "A scratch folder, kept scratch-free; the base:/ style is not a drive. Time 10:30/11:00.",
        "![after](./shots/after.png) and <img src='../../x.png'>",
        r"<!-- C:\Repositories\Coney is only in a comment -->",
    ],
)
def test_code_urls_images_and_comments_are_not_local_paths(summary: str) -> None:
    problems = check_body(body(summary=summary), BODY_RULES)
    assert not any("is a local path" in problem for problem in problems), problems


def test_screenshots_are_not_looked_for_outside_their_section() -> None:
    text = body(screenshots="").replace("## Testing", "![x](x.png)\n\n## Testing")
    assert any("'## Screenshots'" in problem for problem in check_body(text, BODY_RULES))


def test_a_missing_screenshots_section_is_reported_once() -> None:
    text = body().replace("## Screenshots\n", "")
    assert len(check_body(text, BODY_RULES)) == 1


@pytest.mark.parametrize("written_by", ["", " ", "<!-- a person / an AI -->", "..."])
def test_empty_commits_written_by_is_reported(written_by: str) -> None:
    problems = check_body(body(written_by=written_by), BODY_RULES)
    assert any("'Commits written by:' is empty" in problem for problem in problems), problems


@pytest.mark.parametrize("written_by", ["Claude", "an AI / a person", "AI", "a person and an AI", "a human"])
def test_commits_written_by_outside_the_choices_is_reported(written_by: str) -> None:
    problems = check_body(body(written_by=written_by), BODY_RULES)
    assert any("is not one of" in problem and "an AI, edited by a person" in problem for problem in problems), problems


def test_a_missing_commits_written_by_line_is_reported() -> None:
    text = body().replace("- **Commits written by:** an AI\n", "")
    assert any("'Commits written by:' is missing" in problem for problem in check_body(text, BODY_RULES))


def test_commits_written_by_outside_the_ai_use_section_does_not_count() -> None:
    text = body(written_by="").replace("## Checklist", "- **Commits written by:** an AI\n\n## Checklist")
    assert any("'Commits written by:' is empty" in problem for problem in check_body(text, BODY_RULES))


@pytest.mark.parametrize("written_by", ["an AI", "an AI, edited by a person", "a person, with AI help"])
@pytest.mark.parametrize("model", ["", "  ", "<!-- the model that wrote them -->", "n/a", "..."])
def test_an_empty_model_is_reported_when_an_ai_took_part(written_by: str, model: str) -> None:
    problems = check_body(body(written_by=written_by, model=model), BODY_RULES)
    assert any("'Model:'" in problem and "an AI took part" in problem for problem in problems), problems


@pytest.mark.parametrize("model", ["", "n/a", "<!-- the model -->"])
def test_a_person_needs_no_model(model: str) -> None:
    assert check_body(body(written_by="a person", model=model), BODY_RULES) == []


def test_a_missing_model_line_is_reported_when_an_ai_took_part() -> None:
    text = body().replace("- **Model:** Claude Sonnet 5.5\n", "")
    assert any("'Model:' is missing" in problem for problem in check_body(text, BODY_RULES))


def test_the_model_is_not_checked_when_commits_written_by_is_unusable() -> None:
    problems = check_body(body(written_by="", model=""), BODY_RULES)
    assert len(problems) == 1 and "'Commits written by:'" in problems[0]


def test_every_broken_body_rule_is_reported() -> None:
    assert len(check_body(body(summary="").replace("## AI use\n", ""), BODY_RULES)) == 2


def test_strip_comments_removes_single_and_multi_line_comments() -> None:
    assert strip_comments("a<!-- x -->b<!--\ny\n-->c") == "abc"


def read_template() -> str:
    """The pull request template of this checkout."""
    return (REPO / ".github" / "pull_request_template.md").read_text(encoding="utf-8")


def test_the_unchanged_template_is_refused() -> None:
    problems = check_body(read_template(), BODY_RULES)
    assert any("'## Summary' is empty" in problem for problem in problems), problems
    assert any("'Commits written by:' is empty" in problem for problem in problems), problems
    assert any("'## Testing' is empty" in problem for problem in problems), problems
    assert any("'## Screenshots'" in problem for problem in problems), problems


def test_the_template_matches_the_rules() -> None:
    """The headings, labels and choices in the template are the ones the check reads, so the two cannot drift."""
    rules = BODY_RULES
    template = read_template()
    headings = re.findall(r"^## (.+?)\s*$", strip_comments(template), re.MULTILINE)
    assert tuple(headings) == rules.sections
    assert f"**{rules.ai_use.written_by.label}**" in template and f"**{rules.ai_use.model.label}**" in template
    # The choices are listed, in order, in the comment after the label, separated by " / ".
    listed = re.search(re.escape(rules.ai_use.written_by.label) + r"\*\*\s*<!--\s*(.+?)\s*-->", template)
    assert listed is not None
    assert tuple(listed.group(1).split(" / ")) == rules.ai_use.written_by.choices


def test_a_filled_in_template_passes() -> None:
    filled = read_template().replace("## Summary\n", "## Summary\n\nAdds a check.\n", 1)
    filled = filled.replace("## Testing\n", "## Testing\n\n- `pytest`: 3 passed\n", 1)
    filled = filled.replace("## Screenshots\n", "## Screenshots\n\nScreenshots: none (no visible change)\n", 1)
    filled = re.sub(r"(\*\*Commits written by:\*\*) <!--.*?-->", r"\1 an AI", filled)
    filled = re.sub(r"(\*\*Model:\*\*) <!--.*?-->", r"\1 Claude Sonnet 5.5", filled)
    assert check_body(filled, BODY_RULES) == []


# --- every Checklist item is ticked -------------------------------------------------------------------------------


def checklist_body(checklist: str) -> str:
    """A description whose Checklist section holds `checklist`."""
    return body().replace(CHECKLIST, checklist)


def test_a_checklist_of_ticked_items_passes() -> None:
    assert check_body(checklist_body("Done:\n\n- [x] One\n- [X] Two\n"), BODY_RULES) == []


def test_an_unchecked_item_is_refused_with_the_rule() -> None:
    problems = check_body(checklist_body("Done:\n\n- [x] One\n- [ ] Add the second unit\n"), BODY_RULES)
    assert len(problems) == 1
    assert "'Add the second unit'" in problems[0] and "'## Checklist'" in problems[0], problems
    assert "finish it or take it out" in problems[0], problems


def test_an_unchecked_item_is_refused_even_when_it_cites_an_issue() -> None:
    assert check_body(checklist_body("Done:\n\n- [ ] Later (#123)\n"), BODY_RULES)


def test_every_unchecked_item_is_reported() -> None:
    items = "- [ ] One\n- [x] Two\n- [ ] Three (#3)\n- [ ] Four\n"
    assert len(check_body(checklist_body(items), BODY_RULES)) == 3


def test_an_unchecked_item_under_any_label_is_refused() -> None:
    for label in ("Done:", "Remaining:", "Remaining (later units):", "**Remaining:**", "Notes:"):
        assert check_body(checklist_body(f"{label}\n\n- [ ] Not ticked\n"), BODY_RULES), label


def test_the_template_placeholder_item_is_unfinished_too() -> None:
    assert check_body(checklist_body("Done:\n\n- [ ] ...\n"), BODY_RULES)


def test_prose_and_ticked_placeholders_are_not_refused() -> None:
    assert check_body(checklist_body("Done:\n\nNothing else.\n"), BODY_RULES) == []
    assert check_body(checklist_body("Done:\n\n- [x] ...\n"), BODY_RULES) == []


def test_a_long_item_is_shortened_in_the_message() -> None:
    problems = check_body(checklist_body("- [ ] " + "word " * 30 + "\n"), BODY_RULES)
    assert len(problems) == 1 and "..." in problems[0]


def test_unchecked_items_in_a_comment_are_ignored() -> None:
    assert check_body(checklist_body("Done:\n\n- [x] One\n<!-- - [ ] Not an item -->\n"), BODY_RULES) == []


def test_a_wrapped_item_is_one_item() -> None:
    problems = check_body(checklist_body("- [ ] A long item that wraps\n  onto a second line\n"), BODY_RULES)
    assert len(problems) == 1 and "wraps onto a second line" in problems[0]


def test_unchecked_items_outside_the_checklist_section_are_not_refused() -> None:
    assert check_body(body(testing="- [ ] A step of the manual test\n"), BODY_RULES) == []


def test_unfinished_items_lists_the_text_of_each() -> None:
    text = checklist_body("Done:\n\n- [x] One\n- [ ] Two\n- [ ] Three\n  wrapped\n")
    assert unfinished_items(text, BODY_RULES) == ["Two", "Three wrapped"]
    assert unfinished_items("## Summary\n\nNo checklist.\n", BODY_RULES) == []


# --- pull requests merged under the older Remaining rule ----------------------------------------------------------


def legacy_body(remaining: str) -> str:
    """A description in the older form, whose Checklist holds a Remaining list."""
    return checklist_body(f"Done:\n\n- [x] One\n\nRemaining:\n\n{remaining}")


def test_the_older_rule_wants_a_number_on_each_unchecked_remaining_item() -> None:
    text = legacy_body("- [ ] Cited (#12)\n- [ ] Wrapped\n  onto (#13)\n- [ ] Loose\n- [ ] ...\n")
    assert legacy_uncited_remaining(text, BODY_RULES) == ["Loose"]
    assert legacy_remaining_items(text, BODY_RULES) == ["Cited (#12)", "Wrapped onto (#13)", "Loose"]


def test_the_older_rule_ignores_lists_other_than_remaining() -> None:
    text = checklist_body("Done:\n\n- [ ] Not ticked\n\nNotes:\n\n- [ ] Aside\n")
    assert legacy_uncited_remaining(text, BODY_RULES) == [] and legacy_remaining_items(text, BODY_RULES) == []


def test_the_real_rules_read_the_checklist_section() -> None:
    rules = BODY_RULES.checklist
    assert rules.section == "Checklist" and rules.unchecked == "refused" and rules.legacy_label == "Remaining:"
    assert rules.since.isoformat() == "2026-10-12T10:39:13+00:00"
    assert rules.section in BODY_RULES.sections


# --- the rules come from the conventions file, not from the code ---------------------------------------------------


def test_pointer_rules_name_the_files_the_line_and_the_size(tmp_path: Path) -> None:
    rules = conventions_with(
        ("paths.pointer_files", {"files": ["CLAUDE.md"], "line": "@OTHER.md", "max_bytes": 20})
    ).paths.pointer_files
    (tmp_path / "CLAUDE.md").write_text("@OTHER.md\n" + "x" * 30, encoding="utf-8")
    problems = check_pointer_files(tmp_path, rules)
    assert len(problems) == 1 and "over the 20 allowed" in problems[0]
    (tmp_path / "CLAUDE.md").write_text("@AGENTS.md\n", encoding="utf-8")
    assert "no line that is exactly @OTHER.md" in check_pointer_files(tmp_path, rules)[0]


def test_the_title_limit_is_the_files() -> None:
    rules = conventions_with(("commits.title.max_length", 30)).commits.title
    assert check_title("docs: Add " + "x" * 20, rules) == []
    assert any("31 characters; 30 at most" in problem for problem in check_title("docs: Add " + "x" * 21, rules))


def test_a_trailing_period_is_refused_only_when_the_file_says_so() -> None:
    allowed = conventions_with(("commits.title.trailing_period", True)).commits.title
    assert check_title("docs: Add the documents.", allowed) == []
    assert check_title("docs: Add the documents.", load_conventions(REPO).commits.title)


def test_the_breaking_suffix_is_the_files() -> None:
    rules = conventions_with(("commits.title.breaking_suffix", "[!]")).commits.title
    assert check_title("docs: Add the guide [!]", rules) == []
    assert any("[!] must end the title" in problem for problem in check_title("docs: Add [!] the guide", rules))
    assert check_title("docs: Add (BREAKING) the guide", rules) == []


PATHS = load_conventions(REPO).paths
DOCS_NONE = load_conventions(REPO).pull_requests.docs_none


def test_code_without_docs_is_refused_naming_the_paths() -> None:
    problems = check_docs(["src/a.cpp", "src/b.cpp", "src/c.cpp", "src/d.cpp"], "", PATHS, DOCS_NONE)
    assert len(problems) == 1
    assert "src/a.cpp, src/b.cpp, src/c.cpp and 1 more" in problems[0] and "`Docs: none`" in problems[0]


@pytest.mark.parametrize(
    "changed",
    [
        ["src/a.cpp", "docs/guides/x.md"],
        ["python/src/coney_tools/cli.py", "research/missions.yaml"],
        ["tests/a_test.cpp", ".github/workflows/pr.yml", "CMakeLists.txt"],
        [],
    ],
)
def test_changes_that_need_no_docs_pass(changed: list[str]) -> None:
    assert check_docs(changed, "", PATHS, DOCS_NONE) == []


@pytest.mark.parametrize("line", ["Docs: none", "docs:none", "  DOCS:   None", "Docs: none (a typo fix)"])
def test_the_docs_none_line_passes_in_any_spacing(line: str) -> None:
    assert check_docs(["src/a.cpp"], f"## Summary\n\n{line}\n", PATHS, DOCS_NONE) == []


@pytest.mark.parametrize("text", ["I think Docs: none is fine", "Docs: nonetheless", "Docs none"])
def test_a_docs_none_inside_a_sentence_does_not_pass(text: str) -> None:
    assert check_docs(["src/a.cpp"], text, PATHS, DOCS_NONE)


def test_the_docs_rule_follows_the_files_paths_and_line() -> None:
    conventions = conventions_with(("paths.code", ["lib/"]), ("pull_requests.docs_none.line", "No docs"))
    paths, docs_none = conventions.paths, conventions.pull_requests.docs_none
    assert check_docs(["src/a.cpp"], "", paths, docs_none) == []
    assert check_docs(["lib/a.cpp"], "", paths, docs_none)
    assert check_docs(["lib/a.cpp"], "no docs\n", paths, docs_none) == []


def test_local_paths_are_allowed_only_when_the_file_says_so() -> None:
    text = body(testing="- ran it from scratch/pr/x.log: 12 passed")
    assert check_body(text, BODY_RULES)
    assert check_body(text, conventions_with(("pull_requests.local_paths", True)).pull_requests) == []


def test_a_local_image_is_accepted_only_when_uploads_are_not_required() -> None:
    text = body(screenshots="![a](./shots/a.png)")
    assert check_body(text, BODY_RULES)
    relaxed = conventions_with(("pull_requests.screenshots.uploaded_only", False)).pull_requests
    assert check_body(text, relaxed) == []


def test_the_sections_and_labels_are_the_files() -> None:
    rules = conventions_with(
        ("pull_requests.sections", ["Summary", "Checklist", "Testing", "Pictures", "AI use"]),
        ("pull_requests.screenshots.section", "Pictures"),
    ).pull_requests
    text = body().replace("## Screenshots", "## Pictures")
    assert check_body(text, rules) == []
    assert any("missing the section '## Pictures'" in problem for problem in check_body(body(), rules))


# --- the Model line against the branch's Co-Authored-By trailers -----------------------------------------------------

TRAILERS = load_conventions(REPO).commits.trailers
OPUS = "Claude Opus 5.5"
SONNET = "Claude Sonnet 5.5"


def test_the_trailer_models_are_the_ai_co_authors_of_every_message() -> None:
    messages = [
        message("a: Add b", f"Co-Authored-By: {SONNET} <noreply@anthropic.com>"),
        message("a: Fix c", f"Co-Authored-By: {OPUS} <noreply@anthropic.com>"),
        message("a: Fix d", f"Co-Authored-By: {SONNET} <noreply@anthropic.com>"),
    ]
    assert trailer_models(messages, TRAILERS) == [SONNET, OPUS]


def test_the_trailer_key_and_the_model_name_are_read_case_insensitively() -> None:
    text = message("a: Add b", "co-authored-by:   claude   Haiku 5  <NoReply@Anthropic.com>")
    assert trailer_models([text], TRAILERS) == ["claude Haiku 5"]


def test_a_person_as_co_author_is_not_a_model() -> None:
    text = message("a: Add b", "Co-Authored-By: Ada Lovelace <ada@example.org>")
    assert trailer_models([text], TRAILERS) == []


def test_a_message_without_trailers_names_no_model() -> None:
    assert trailer_models([message()], TRAILERS) == []
    assert trailer_models([], TRAILERS) == []


def test_a_model_the_line_does_not_name_is_refused_naming_it() -> None:
    problems = check_body(body(model=SONNET), BODY_RULES, branch_models=[SONNET, OPUS])
    assert len(problems) == 1
    assert OPUS in problems[0] and "'Model:'" in problems[0] and "Co-Authored-By" in problems[0]


def test_the_same_branch_passes_once_the_line_names_the_model() -> None:
    assert check_body(body(model=f"{SONNET}, {OPUS}"), BODY_RULES, branch_models=[SONNET, OPUS]) == []


def test_the_model_is_matched_anywhere_on_the_line_and_in_any_case() -> None:
    line = "claude  OPUS 5.5 (reviewed), and Claude Sonnet 5.5 for the code"
    assert check_body(body(model=line), BODY_RULES, branch_models=[OPUS, SONNET]) == []


def test_a_version_prefix_is_not_a_match() -> None:
    # "Claude Opus 5.5" must not be found inside "Claude Opus 5.55".
    assert check_body(body(model="Claude Opus 5.55"), BODY_RULES, branch_models=[OPUS])


@pytest.mark.parametrize("model", ["", "n/a"])
def test_a_person_with_a_trailer_on_the_branch_must_name_the_model(model: str) -> None:
    problems = check_body(body(written_by="a person", model=model), BODY_RULES, branch_models=[OPUS])
    assert len(problems) == 1 and OPUS in problems[0]


def test_a_branch_without_trailers_needs_nothing_more() -> None:
    assert check_body(body(written_by="a person", model="n/a"), BODY_RULES, branch_models=[]) == []
    assert check_body(body(), BODY_RULES) == []


def test_an_unusable_model_line_is_reported_once() -> None:
    problems = check_body(body(model="n/a"), BODY_RULES, branch_models=[OPUS])
    assert len(problems) == 1 and "an AI took part" in problems[0]


def test_the_rule_can_be_switched_off_in_the_conventions() -> None:
    rules = conventions_with(("pull_requests.ai_use.model.names_trailer_models", False)).pull_requests
    assert check_body(body(model=SONNET), rules, branch_models=[OPUS]) == []


def test_the_branch_models_come_from_the_commits_after_the_base(tmp_path: Path) -> None:
    repo = make_branch(
        tmp_path,
        [
            message("a: Add b", f"Co-Authored-By: {SONNET} <noreply@anthropic.com>"),
            message("a: Fix c", f"Co-Authored-By: {OPUS} <noreply@anthropic.com>"),
        ],
    )
    # The base commit's own trailer (Haiku) is not on the branch.
    assert branch_trailer_models(repo, "main~2", TRAILERS) == [OPUS, SONNET]
    assert branch_trailer_models(repo, "HEAD", TRAILERS) == []


def test_a_base_git_cannot_find_is_an_error_not_a_pass(tmp_path: Path) -> None:
    repo = make_branch(tmp_path, [message()])
    with pytest.raises(ConfigError, match="origin/main"):
        branch_trailer_models(repo, "origin/main", TRAILERS)


def test_a_folder_that_is_not_a_worktree_is_an_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError):
        branch_trailer_models(tmp_path / "missing", "origin/main", TRAILERS)


def test_a_merge_of_main_counts_main_s_trailers_only_while_origin_main_is_stale(tmp_path: Path) -> None:
    (tmp_path / "upstream").mkdir()
    upstream = make_branch(tmp_path / "upstream", [])
    clone = tmp_path / "clone"
    git(tmp_path, "clone", "-q", str(upstream), str(clone))
    git(clone, "switch", "-q", "-c", "feature")
    (clone / "g.txt").write_text("1", encoding="utf-8")
    git(clone, "add", "g.txt")
    git(clone, "commit", "-q", "-m", message("a: Add b", f"Co-Authored-By: {SONNET} <noreply@anthropic.com>"))
    # Main moves on upstream with another model's commit and the branch merges it, but its origin/main is put back to
    # the old commit: a clone that merged a newer main without having fetched it into origin/main.
    old_main = git(clone, "rev-parse", "origin/main").strip()
    (upstream / "f.txt").write_text("new", encoding="utf-8")
    git(upstream, "commit", "-q", "-a", "-m", message("a: Fix c", f"Co-Authored-By: {OPUS} <noreply@anthropic.com>"))
    git(clone, "fetch", "-q", "origin")
    git(clone, "merge", "-q", "--no-ff", "-m", "Merge main", "origin/main")
    git(clone, "update-ref", "refs/remotes/origin/main", old_main)
    assert branch_trailer_models(clone, "origin/main", TRAILERS) == [OPUS, SONNET]
    # After a fetch, origin/main holds the merged commits and only the branch's own trailer is left.
    git(clone, "fetch", "-q", "origin")
    assert branch_trailer_models(clone, "origin/main", TRAILERS) == [SONNET]


def test_the_branch_models_do_not_depend_on_which_side_of_a_merge_is_newer(tmp_path: Path) -> None:
    # The same merge as above, but main's commit is made before the branch's: `git log` lists the two the other way
    # round (it orders by commit time), and the answer must not change.
    (tmp_path / "upstream").mkdir()
    upstream = make_branch(tmp_path / "upstream", [])
    clone = tmp_path / "clone"
    git(tmp_path, "clone", "-q", str(upstream), str(clone))
    git(clone, "switch", "-q", "-c", "feature")
    old_main = git(clone, "rev-parse", "origin/main").strip()
    (upstream / "f.txt").write_text("new", encoding="utf-8")
    git(upstream, "commit", "-q", "-a", "-m", message("a: Fix c", f"Co-Authored-By: {OPUS} <noreply@anthropic.com>"))
    (clone / "g.txt").write_text("1", encoding="utf-8")
    git(clone, "add", "g.txt")
    git(clone, "commit", "-q", "-m", message("a: Add b", f"Co-Authored-By: {SONNET} <noreply@anthropic.com>"))
    git(clone, "fetch", "-q", "origin")
    git(clone, "merge", "-q", "--no-ff", "-m", "Merge main", "origin/main")
    git(clone, "update-ref", "refs/remotes/origin/main", old_main)
    assert branch_trailer_models(clone, "origin/main", TRAILERS) == [OPUS, SONNET]


# --- list bullets: one definition, shared with the audit's text reader ----------------------------------------------


def test_the_description_and_the_audit_read_bullets_with_one_pattern() -> None:
    """`repo_checks` has no copy of the bullet or task-item pattern; it uses the audit text reader's."""
    # `vars` reads the names off the module's dict: mypy --strict refuses `repo_checks.LIST_MARKER` because
    # repo_checks imports it without re-exporting it, and the test is exactly about that import.
    assert vars(repo_checks)["LIST_MARKER"] is audit_text.LIST_MARKER
    assert vars(repo_checks)["OPEN_ITEM"] is audit_text.OPEN_ITEM


@pytest.mark.parametrize("marker", ["-", "*", "+", "- [ ]", "* [x]", "+ [X]"])
def test_a_real_bullet_is_stripped_so_a_placeholder_item_is_still_blank(marker: str) -> None:
    assert check_body(body(summary=f"{marker} ..."), BODY_RULES) != []
    assert check_body(body(testing=f"{marker} <reason>"), BODY_RULES) != []


@pytest.mark.parametrize("line", ["-...", "*...", "+<reason>", "--flag", "*emphasis*", "-5 degrees"])
def test_a_line_that_starts_with_a_dash_or_star_is_content_not_a_bullet(line: str) -> None:
    """Without a space after it, `-` or `*` is part of the word, so the first character is not dropped.

    The old copy stripped it, which turned `-...` into the placeholder `...` and called a section with content empty.
    """
    assert check_body(body(summary=line), BODY_RULES) == []
    assert check_body(body(testing=line), BODY_RULES) == []


@pytest.mark.parametrize("wrapped", ["-5 degrees", "*x* too", "--play-level 3", "+1 more"])
def test_a_wrapped_line_that_starts_with_a_dash_continues_the_item(wrapped: str) -> None:
    """An indented line is a new item only when a bullet marker and a space start it; `--play-level` is a word."""
    text = checklist_body(f"- [ ] Play the level with\n  {wrapped}\n")
    assert unfinished_items(text, BODY_RULES) == [f"Play the level with {wrapped}"]


def test_an_indented_real_bullet_still_ends_the_item_above() -> None:
    text = checklist_body("- [ ] First\n  - a nested note\n  wrapped?\n- [ ] Second\n")
    assert unfinished_items(text, BODY_RULES) == ["First", "Second"]
