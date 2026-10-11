# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.repo_checks: pointer files, commit titles and pull request descriptions."""

import json
import re
from pathlib import Path

import pytest

from coney_tools.repo_checks import (
    IMPORT_LINE,
    MAX_POINTER_BYTES,
    BodyRules,
    BodyRulesError,
    TitleRules,
    TitleRulesError,
    check_body,
    check_pointer_files,
    check_title,
    load_body_rules,
    load_title_rules,
    strip_comments,
)

# The import line is joined on, not written after a "\n" escape, which would read as an email address ("n@...").
GOOD = "# CLAUDE.md\n\nRead `AGENTS.md`.\n\n" + IMPORT_LINE + "\n"


def write(root: Path, claude: str | None = GOOD, gemini: str | None = GOOD.replace("CLAUDE", "GEMINI")) -> Path:
    """Write the pointer files into `root`; None leaves that one out."""
    for name, text in (("CLAUDE.md", claude), ("GEMINI.md", gemini)):
        if text is not None:
            (root / name).write_text(text, encoding="utf-8")
    return root


def test_clean_pointer_files_pass(tmp_path: Path) -> None:
    assert check_pointer_files(write(tmp_path)) == []


def test_missing_pointer_file_is_reported(tmp_path: Path) -> None:
    problems = check_pointer_files(write(tmp_path, gemini=None))
    assert len(problems) == 1 and "GEMINI.md" in problems[0]


def test_missing_import_line_is_reported(tmp_path: Path) -> None:
    problems = check_pointer_files(write(tmp_path, claude="# CLAUDE.md\n\nSee AGENTS.md\n"))
    assert len(problems) == 1 and "@AGENTS.md" in problems[0]


def test_oversized_pointer_file_is_reported(tmp_path: Path) -> None:
    problems = check_pointer_files(write(tmp_path, claude=GOOD + "x" * MAX_POINTER_BYTES))
    assert len(problems) == 1 and "CLAUDE.md" in problems[0]


def test_the_real_repository_is_clean() -> None:
    repo = Path(__file__).resolve().parents[2]
    assert check_pointer_files(repo) == []


RULES = TitleRules(areas=frozenset({"docs", "ci"}), verbs=("Add", "Fix", "Update"))


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


def write_rules(root: Path, data: object) -> Path:
    """Write `data` as the commit conventions file of `root`."""
    (root / ".github").mkdir()
    (root / ".github" / "commit-conventions.json").write_text(json.dumps(data), encoding="utf-8")
    return root


def test_rules_are_read_from_the_conventions_file(tmp_path: Path) -> None:
    rules = load_title_rules(write_rules(tmp_path, {"areas": {"docs": "documentation"}, "verbs": ["Add"]}))
    assert rules == TitleRules(areas=frozenset({"docs"}), verbs=("Add",))


@pytest.mark.parametrize(
    "data",
    [
        [],
        {"verbs": ["Add"]},
        {"areas": {}, "verbs": ["Add"]},
        {"areas": {"docs": "x"}},
        {"areas": {"docs": "x"}, "verbs": []},
        {"areas": {"docs": "x"}, "verbs": ["Add", 3]},
        {"areas": ["docs"], "verbs": ["Add"]},
    ],
)
def test_unusable_rules_raise(tmp_path: Path, data: object) -> None:
    with pytest.raises(TitleRulesError):
        load_title_rules(write_rules(tmp_path, data))


def test_missing_or_broken_conventions_file_raises(tmp_path: Path) -> None:
    with pytest.raises(TitleRulesError):
        load_title_rules(tmp_path)
    (tmp_path / ".github").mkdir()
    (tmp_path / ".github" / "commit-conventions.json").write_text("{not json", encoding="utf-8")
    with pytest.raises(TitleRulesError):
        load_title_rules(tmp_path)


def test_the_real_rules_load_and_accept_a_real_title() -> None:
    rules = load_title_rules(Path(__file__).resolve().parents[2])
    assert check_title("ci: Add the pull request title check", rules) == []


# --- pull request descriptions -----------------------------------------------------------------------------------

REPO = Path(__file__).resolve().parents[2]
BODY_RULES = BodyRules(
    sections=("Summary", "Checklist", "Testing", "Screenshots", "AI use"),
    filled=("Summary", "Testing"),
    placeholders=("...", "<reason>"),
    screenshots_section="Screenshots",
    screenshots_none="Screenshots: none",
    ai_section="AI use",
    written_by_label="Commits written by:",
    choices=("a person", "an AI", "an AI, edited by a person", "a person, with AI help"),
    no_ai="a person",
    model_label="Model:",
    model_none="n/a",
)
CHECKLIST = "Done:\n\n- [x] The check\n\nRemaining:\n\n- [ ] Nothing\n"


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


def write_body_rules(root: Path, data: object) -> Path:
    """Write `data` as the pull request description rules of `root`."""
    (root / ".github").mkdir()
    (root / ".github" / "pr-body-rules.json").write_text(json.dumps(data), encoding="utf-8")
    return root


AI_USE = {
    "section": "AI use",
    "written_by": {"label": "Written by:", "choices": ["a person", "an AI"], "no_ai": "a person"},
    "model": {"label": "Model:", "none": "n/a"},
}
SCREENSHOTS = {"section": "Screenshots", "none": "Screenshots: none"}
GOOD_BODY_RULES = {
    "sections": ["Summary", "Screenshots", "AI use"],
    "filled": ["Summary"],
    "placeholders": ["..."],
    "screenshots": SCREENSHOTS,
    "ai_use": AI_USE,
}


def test_body_rules_are_read_from_the_rules_file(tmp_path: Path) -> None:
    rules = load_body_rules(write_body_rules(tmp_path, GOOD_BODY_RULES))
    assert rules.sections == ("Summary", "Screenshots", "AI use") and rules.filled == ("Summary",)
    assert rules.screenshots_section == "Screenshots" and rules.screenshots_none == "Screenshots: none"
    assert rules.choices == ("a person", "an AI") and rules.no_ai == "a person"
    assert rules.written_by_label == "Written by:" and rules.model_label == "Model:"


def altered(**changes: object) -> dict[str, object]:
    """The good rules with the top-level keys in `changes` replaced (a value of None removes the key)."""
    data = {**GOOD_BODY_RULES, **changes}
    return {key: value for key, value in data.items() if value is not None}


@pytest.mark.parametrize(
    "data",
    [
        [],
        altered(sections=None),
        altered(sections=[]),
        altered(sections=["Summary", 3]),
        altered(filled=["Missing"]),
        altered(filled="Summary"),
        altered(placeholders=None),
        altered(ai_use=None),
        altered(screenshots=None),
        altered(screenshots={"section": "Other", "none": "Screenshots: none"}),
        altered(screenshots={"section": "Screenshots", "none": ""}),
        altered(ai_use={**AI_USE, "section": "Other"}),
        altered(ai_use={**AI_USE, "model": {"label": "", "none": "n/a"}}),
        altered(ai_use={**AI_USE, "written_by": {"label": "W:", "choices": [], "no_ai": "x"}}),
        altered(ai_use={**AI_USE, "written_by": {"label": "W:", "choices": ["a"], "no_ai": "b"}}),
    ],
)
def test_unusable_body_rules_raise(tmp_path: Path, data: object) -> None:
    with pytest.raises(BodyRulesError):
        load_body_rules(write_body_rules(tmp_path, data))


def test_missing_or_broken_body_rules_file_raises(tmp_path: Path) -> None:
    with pytest.raises(BodyRulesError):
        load_body_rules(tmp_path)
    (tmp_path / ".github").mkdir()
    (tmp_path / ".github" / "pr-body-rules.json").write_text("{not json", encoding="utf-8")
    with pytest.raises(BodyRulesError):
        load_body_rules(tmp_path)


def read_template() -> str:
    """The pull request template of this checkout."""
    return (REPO / ".github" / "pull_request_template.md").read_text(encoding="utf-8")


def test_the_unchanged_template_is_refused() -> None:
    problems = check_body(read_template(), load_body_rules(REPO))
    assert any("'## Summary' is empty" in problem for problem in problems), problems
    assert any("'Commits written by:' is empty" in problem for problem in problems), problems
    assert any("'## Testing' is empty" in problem for problem in problems), problems
    assert any("'## Screenshots'" in problem for problem in problems), problems


def test_the_template_matches_the_rules() -> None:
    """The headings, labels and choices in the template are the ones the check reads, so the two cannot drift."""
    rules = load_body_rules(REPO)
    template = read_template()
    headings = re.findall(r"^## (.+?)\s*$", strip_comments(template), re.MULTILINE)
    assert tuple(headings) == rules.sections
    assert f"**{rules.written_by_label}**" in template and f"**{rules.model_label}**" in template
    # The choices are listed, in order, in the comment after the label, separated by " / ".
    listed = re.search(re.escape(rules.written_by_label) + r"\*\*\s*<!--\s*(.+?)\s*-->", template)
    assert listed is not None
    assert tuple(listed.group(1).split(" / ")) == rules.choices


def test_a_filled_in_template_passes() -> None:
    filled = read_template().replace("## Summary\n", "## Summary\n\nAdds a check.\n", 1)
    filled = filled.replace("## Testing\n", "## Testing\n\n- `pytest`: 3 passed\n", 1)
    filled = filled.replace("## Screenshots\n", "## Screenshots\n\nScreenshots: none (no visible change)\n", 1)
    filled = re.sub(r"(\*\*Commits written by:\*\*) <!--.*?-->", r"\1 an AI", filled)
    filled = re.sub(r"(\*\*Model:\*\*) <!--.*?-->", r"\1 Claude Sonnet 5.5", filled)
    assert check_body(filled, load_body_rules(REPO)) == []
