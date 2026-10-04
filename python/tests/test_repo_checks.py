# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.repo_checks: the CLAUDE.md and GEMINI.md pointer files, and commit titles."""

import json
from pathlib import Path

import pytest

from coney_tools.repo_checks import (
    IMPORT_LINE,
    MAX_POINTER_BYTES,
    TitleRules,
    TitleRulesError,
    check_pointer_files,
    check_title,
    load_title_rules,
)

# The import line is joined on, not written after a "\n" escape, which would read as an email address ("n@...").
GOOD = "# CLAUDE.md\n\nRead `AGENTS.md`.\n\n" + IMPORT_LINE + "\n"


def write(root: Path, claude: str | None = GOOD, gemini: str | None = GOOD.replace("CLAUDE", "GEMINI")) -> Path:
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
