# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for reading the Done items of a task file and what each says about the pull request: met, left for a named
follow-up, or marked wrong. Invented task text only."""

import pytest

from coney_tools import pr_done
from coney_tools.pr_done import DoneItem


def read(done: str) -> list[DoneItem]:
    """The items of a Done section written as `task create` writes it: the issue's text, quoted line by line."""
    return pr_done.read("\n".join(f"> {line}" if line else ">" for line in done.splitlines()))


def test_a_plain_bullet_or_a_ticked_one_is_met() -> None:
    items = read("- The widget waits.\n- [x] A test proves it.\n* [X] Docs say so.")
    assert [(i.text, i.left_for, i.problem) for i in items] == [
        ("The widget waits.", None, None),
        ("A test proves it.", None, None),
        ("Docs say so.", None, None),
    ]
    assert all(i.met for i in items)


@pytest.mark.parametrize(
    ("line", "text", "target"),
    [
        ("- [ ] Reference pictures. (left for #1500)", "Reference pictures.", "#1500"),
        ("- Reference pictures (left for #1500).", "Reference pictures", "#1500"),
        (
            "- [ ] The required check (Left For task 1501-add-the-check)",
            "The required check",
            "task 1501-add-the-check",
        ),
        ("- Pictures (left to #12)", "Pictures", "#12"),
    ],
)
def test_an_item_that_says_where_it_goes_is_left(line: str, text: str, target: str) -> None:
    (item,) = read(line)
    assert (item.text, item.left_for, item.problem, item.met) == (text, target, None, False)


def test_the_note_may_come_after_a_wrap() -> None:
    (item,) = read("- [ ] Reference pictures of\n  the widget (left for #1500)")
    assert item.text == "Reference pictures of the widget" and item.left_for == "#1500"


def test_an_unticked_item_with_no_follow_up_is_a_problem() -> None:
    (item,) = read("- [ ] The required check")
    assert item.left_for is None and item.problem is not None and "names no follow-up" in item.problem
    assert not item.met


def test_a_ticked_item_that_is_also_left_is_a_problem() -> None:
    (item,) = read("- [x] The required check (left for #1500)")
    assert item.problem is not None and "ticked and also left" in item.problem


@pytest.mark.parametrize("note", ["(left for later)", "(left for #)", "(left for task Bad_Name)", "left for #12"])
def test_a_note_that_names_no_issue_or_task_is_no_note(note: str) -> None:
    (item,) = read(f"- [ ] The required check {note}")
    assert item.left_for is None and item.problem is not None


def test_prose_gives_one_item_per_paragraph_and_the_placeholder_none() -> None:
    assert [i.text for i in read("The widget waits for\nits frame.\n\nA test proves it.")] == [
        "The widget waits for its frame.",
        "A test proves it.",
    ]
    assert read("See issue #757.") == []


def test_what_the_done_items_say_about_a_task_that_closes_its_issue() -> None:
    items = read("- A (left for #9)\n- B")
    assert pr_done.problems(items, finishes=False) == []
    (problem,) = pr_done.problems(items, finishes=True)
    assert "A" in problem and "#9" in problem and "finishes: false" in problem


def test_every_item_left_is_a_problem_because_the_pull_request_finishes_nothing() -> None:
    items = read("- A (left for #9)\n- B (left for #10)")
    (problem,) = pr_done.problems(items, finishes=False)
    assert "finishes nothing" in problem


def test_the_marking_problems_come_with_the_item() -> None:
    items = read("- [ ] A\n- [x] B (left for #9)\n- C")
    assert len(pr_done.problems(items, finishes=False)) == 2
