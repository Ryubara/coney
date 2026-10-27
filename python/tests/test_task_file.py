# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.task_file: rendering, parsing and checking a task file."""

from datetime import UTC, datetime

import pytest
from audit_support import CONVENTIONS

from coney_tools import task_file
from coney_tools.task_file import SECTIONS, TaskFile, TaskFileError

TASKS = CONVENTIONS.tasks
T0 = datetime(2026, 10, 22, 14, 3, 11, tzinfo=UTC)


def sample(**changes: object) -> TaskFile:
    """A task file for issues 301 and 305."""
    values: dict[str, object] = {
        "task": "301-fix-the-fight-goal",
        "issues": (301, 305),
        "kind": "feature",
        "claimant": "Ryubara",
        "claimed_at": T0,
        "scope": ("src/ai/", "0x00322b78"),
        "terms": ("fight", "re target"),
        "sections": {"Goal": "Fix it.", "Done": "A test passes.\n\nWith a second paragraph."},
    }
    values.update(changes)
    return TaskFile(**values)  # type: ignore[arg-type]


def test_a_task_file_survives_its_text() -> None:
    original = sample()
    parsed = task_file.parse_task_file(task_file.render_task_file(original), TASKS)
    assert (parsed.task, parsed.issues, parsed.kind, parsed.claimant) == (
        "301-fix-the-fight-goal",
        (301, 305),
        "feature",
        "Ryubara",
    )
    assert parsed.claimed_at == T0 and parsed.scope == original.scope and parsed.terms == original.terms
    assert parsed.sections["Goal"] == "Fix it." and parsed.sections["Done"].endswith("With a second paragraph.")
    assert list(parsed.sections) == list(SECTIONS)


def test_every_section_is_there_in_order_even_when_empty() -> None:
    text = task_file.render_task_file(sample(sections={}))
    headings = [line for line in text.splitlines() if line.startswith("## ")]
    assert headings == [f"## {name}" for name in SECTIONS]
    assert text.count("(nothing yet)") == len(SECTIONS)


def test_crlf_text_parses() -> None:
    text = task_file.render_task_file(sample()).replace("\n", "\r\n")
    assert task_file.parse_task_file(text, TASKS).task == "301-fix-the-fight-goal"


@pytest.mark.parametrize(
    ("change", "why"),
    [
        (lambda t: t.replace("---\n", "", 1), "does not start with a --- block"),
        (lambda t: t.replace("kind: feature", "kind: epic"), "'epic' is not one of"),
        (lambda t: t.replace("kind: feature\n", ""), "has no 'kind'"),
        (lambda t: t.replace("claimed_at: '2026-10-22T14:03:11Z'", "claimed_at: soon"), "wrong value"),
        (lambda t: t.replace("## Scope", "## Scoped"), "'## Scope' is missing"),
        (lambda t: t.replace("issues: [301, 305]", "issues: []"), "no issue"),
        (lambda t: t.replace("task: 301", "task: [301", 1), "not valid YAML"),
    ],
)
def test_a_damaged_file_names_the_problem(change: object, why: str) -> None:
    text = change(task_file.render_task_file(sample()))  # type: ignore[operator]
    with pytest.raises(TaskFileError, match=why):
        task_file.parse_task_file(text, TASKS)


def test_a_fix_task_names_the_task_it_fixes_and_an_ordinary_one_has_no_parent_line() -> None:
    fix = sample(task="301-fix", kind="fix", issues=(301,), parent="301-fix-the-fight-goal")
    text = task_file.render_task_file(fix)
    assert "parent: 301-fix-the-fight-goal" in text
    assert task_file.parse_task_file(text, TASKS).parent == "301-fix-the-fight-goal"
    ordinary = task_file.render_task_file(sample())
    assert "parent" not in ordinary
    assert task_file.parse_task_file(ordinary, TASKS).parent == ""


@pytest.mark.parametrize(
    ("change", "why"),
    [
        ({"kind": "feature", "parent": "301-x"}, "only a fix task has a parent"),
        ({"kind": "fix", "parent": "Not A Task"}, "'Not A Task' is not a task id"),
    ],
)
def test_a_wrong_parent_is_refused(change: dict[str, object], why: str) -> None:
    text = task_file.render_task_file(sample(**change))
    with pytest.raises(TaskFileError, match=why):
        task_file.parse_task_file(text, TASKS)


def test_sections_out_of_order_are_refused() -> None:
    text = task_file.render_task_file(sample())
    swapped = text.replace("## Goal", "## TMP").replace("## Done", "## Goal").replace("## TMP", "## Done")
    with pytest.raises(TaskFileError, match="out of order"):
        task_file.parse_task_file(swapped, TASKS)


@pytest.mark.parametrize(
    ("title", "words"),
    [
        ("gameplay: Fix the fight goal's re-target", "fix-the-fight-goal-s-re-target"),
        ("tools: Add a very long title that goes on and on and on forever", "add-a-very-long-title-that-goes-on-and"),
        ("no area here", "no-area-here"),
        ("tools: !!!", ""),
    ],
)
def test_the_task_id_words_come_from_the_title(title: str, words: str) -> None:
    assert task_file.slug(title) == words
    assert len(task_file.slug(title)) <= 40
