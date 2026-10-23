# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for moving a research page's links between folders (coney_tools.research_links)."""

from __future__ import annotations

import pytest

from coney_tools import research_links


@pytest.mark.parametrize(
    ("target", "from_dir", "to_dir", "expected"),
    [
        ("physics.md#step", "", "combat", "../physics.md#step"),
        ("grabs.md", "combat", "", "combat/grabs.md"),
        ("../guides/testing.md#x", "", "combat", "../../guides/testing.md#x"),
        ("../x.md", "combat", "ai", "../x.md"),
        ("https://example.org/a", "combat", "", "https://example.org/a"),
        ("repo:LEGAL.md#a", "combat", "", "repo:LEGAL.md#a"),
        ("mailto:a@b.c", "", "ai", "mailto:a@b.c"),
    ],
)
def test_a_link_is_rebased_to_another_folder(target: str, from_dir: str, to_dir: str, expected: str) -> None:
    """A relative link keeps pointing at the same file when the text moves from one folder to another."""
    text = f"see [here]({target}) and `code`"
    assert research_links.rebase_links(text, from_dir, to_dir, "page.md") == f"see [here]({expected}) and `code`"


def test_a_link_to_an_anchor_of_the_same_page_names_the_page() -> None:
    """`#anchor` means the page the text was written on, so it must name that page once the text moves."""
    moved = research_links.rebase_links("[the score](#enemy-score)", "ai", "", "ai/goals.md")
    assert moved == "[the score](ai/goals.md#enemy-score)"
    back = research_links.rebase_links(moved, "", "ai", "ai/goals.md")
    assert back == "[the score](goals.md#enemy-score)"
