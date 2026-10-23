# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for reading a research page's headings, anchors and links (coney_tools.research_links)."""

from __future__ import annotations

import pytest

from coney_tools import research_links

PAGE = """\
# Tasks (the TaskEngine)

## Purpose

Text.

## Code index {#code-index}

### Cars: Parts {#code-cars-parts}

```markdown
## Not a heading {#inside-a-fence}
```

### `Foo_Bar` and the [link](x.md) *style*

## Purpose

- **A question** {#q-counter}: what is unknown.
"""


def test_headings_carry_explicit_ids_and_slugs() -> None:
    """A heading's anchor is its `{#id}` when it has one, else MkDocs's slug, numbered when it repeats."""
    found = [(h.level, h.text, h.anchor) for h in research_links.headings(PAGE)]
    assert found == [
        (1, "Tasks (the TaskEngine)", "tasks-the-taskengine"),
        (2, "Purpose", "purpose"),
        (2, "Code index", "code-index"),
        (3, "Cars: Parts", "code-cars-parts"),
        (3, "`Foo_Bar` and the [link](x.md) *style*", "foo_bar-and-the-link-style"),
        (2, "Purpose", "purpose_1"),
    ]


def test_anchors_include_ids_given_outside_headings() -> None:
    """A `{#id}` on a list item counts as an anchor, and a fenced block's headings do not."""
    anchors = research_links.anchors(PAGE)
    assert {"purpose", "purpose_1", "code-index", "code-cars-parts", "q-counter"} <= anchors
    assert "inside-a-fence" not in anchors
    assert "not-a-heading" not in anchors


def test_line_numbers_of_headings() -> None:
    """Each heading knows its line, so a table can be tied to the heading above it."""
    lines = {h.anchor: h.line for h in research_links.headings(PAGE)}
    assert lines["code-index"] == 7


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
