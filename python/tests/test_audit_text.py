# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.audit_text: the readers of issue and description text the audit uses, on made-up bodies."""

import pytest

from coney_tools import audit_text
from coney_tools.audit_text import (
    Section,
    anchors_of,
    answer,
    broken_relative_links,
    clean,
    find_section,
    is_placeholder,
    plain_doc_paths,
    repo_links,
    sections,
    state_claims,
    unchecked_items,
    word_uses,
)

PLACEHOLDERS = ("x", "tbd", "...", "n/a")

# --- sections -----------------------------------------------------------------------------------------------------


def test_sections_are_read_at_levels_two_and_three_in_order() -> None:
    body = '### Summary\n\nOne.\n\n## Screenshots\n\nNone.\n\n### What "done" means\n\nTwo lines\nof text.\n'
    found = sections(body)
    assert [(s.heading, s.level) for s in found] == [("Summary", 3), ("Screenshots", 2), ('What "done" means', 3)]
    assert found[2].text == "Two lines\nof text."


def test_crlf_line_ends_and_comments_do_not_matter() -> None:
    body = "### Summary\r\n\r\n<!-- hidden -->Real\r\n\r\n### Area\r\n\r\nA-tools\r\n"
    found = sections(body)
    assert [s.heading for s in found] == ["Summary", "Area"] and found[0].text == "Real"


def test_a_heading_inside_a_code_fence_is_not_a_section() -> None:
    body = "### Summary\n\n```\n### Not a heading\n```\n\n### Area\n\nA-ai\n"
    assert [s.heading for s in sections(body)] == ["Summary", "Area"]


def test_deeper_headings_belong_to_the_section_above() -> None:
    found = sections("## Summary\n\ntext\n\n#### Detail\n\nmore\n")
    assert len(found) == 1 and "#### Detail" in found[0].text


def test_text_before_the_first_heading_belongs_to_no_section() -> None:
    assert sections("Preamble\n\n### Summary\n\nx\n") == [Section("Summary", 3, "x")]


def test_find_section_ignores_case_and_spacing() -> None:
    found = sections('### what  "DONE" means\n\nyes\n')
    assert find_section(found, 'What "done" means') is not None
    assert find_section(found, "Summary") is None


def test_clean_drops_comments_and_normalises_line_ends() -> None:
    assert clean("a\r\nb<!-- c\nd -->e\rf") == "a\nbe\nf"


# --- placeholders -------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "text", ["", "   ", "x", "X", "x.", "TBD", "...", "_No response_", "- x", "- [ ] n/a", "\n\nx\n"]
)
def test_empty_and_placeholder_text_is_a_placeholder(text: str) -> None:
    assert is_placeholder(text, PLACEHOLDERS)


@pytest.mark.parametrize("text", ["A real sentence.", "x marks the spot", "- one\n- two", "None (repository tooling)"])
def test_real_text_is_not_a_placeholder(text: str) -> None:
    assert not is_placeholder(text, PLACEHOLDERS)


def test_answer_keeps_the_content_lines() -> None:
    assert answer("- [x] I confirm\n\n_No response_\n") == "I confirm"


def test_answer_strips_a_list_marker_only_when_a_space_follows_it() -> None:
    """A dash that starts a word (the option `--play-level`, a `-5` or `*bold*`) is content, not a bullet."""
    assert answer("--play-level") == "--play-level"
    assert answer("-5 degrees") == "-5 degrees"
    assert answer("*bold* text") == "*bold* text"
    assert answer("- --play-level") == "--play-level"
    assert answer("-\n- item") == "item"


# --- wording ------------------------------------------------------------------------------------------------------


def test_the_avoided_word_is_found_with_its_context() -> None:
    uses = word_uses("Ask the owner before you tag a release.", ["owner"])
    assert len(uses) == 1 and "owner" in uses[0] and "tag a release" in uses[0]


@pytest.mark.parametrize(
    "text",
    [
        "Use `owner` in code.",
        "```\nowner = 1\n```",
        "https://example.invalid/owner/name",
    ],
)
def test_the_word_in_code_or_a_url_is_not_a_use(text: str) -> None:
    assert word_uses(text, ["owner"]) == []


def test_link_text_counts_as_words() -> None:
    assert len(word_uses("[the owner](https://example.invalid)", ["owner"])) == 1


@pytest.mark.parametrize("text", ["owner/repo", "{owner} and {repo}", "the owner_decisions key", "ownership"])
def test_names_that_contain_the_word_are_not_a_use(text: str) -> None:
    assert word_uses(text, ["owner"]) == []


@pytest.mark.parametrize("text", ["The owner's play-test", "Owners decide", "OWNER only", "owner-only decisions"])
def test_plural_possessive_and_capitals_are_uses(text: str) -> None:
    assert len(word_uses(text, ["owner"])) == 1


def test_html_comments_are_not_read() -> None:
    assert word_uses("<!-- the owner decides -->done", ["owner"]) == []


# --- unchecked items ----------------------------------------------------------------------------------------------


def test_unchecked_items_are_found_with_wrapped_lines_joined() -> None:
    body = "- [x] done\n- [ ] first item\n  continued (#12)\n- [ ] second\n\nnot a list\n"
    assert unchecked_items(body) == ["first item continued (#12)", "second"]


def test_unchecked_items_in_code_fences_and_comments_are_ignored() -> None:
    body = "```\n- [ ] example\n```\n<!-- - [ ] hidden -->\n- [ ] real\n"
    assert unchecked_items(body) == ["real"]


@pytest.mark.parametrize(
    ("text", "cited"), [("fix it #12", True), ("fix it", False), ("see a#12", False), ("(#7)", True)]
)
def test_cites_number(text: str, cited: bool) -> None:
    assert audit_text.cites_number(text) is cited


# --- links --------------------------------------------------------------------------------------------------------


def test_repo_links_are_read_with_ref_path_and_anchor() -> None:
    links = repo_links(
        "See [x](../blob/main/docs/guides/a.md#b) and [y](../blob/dev/c.md) and [z](https://e.invalid/q)."
    )
    assert [(link.path, link.ref, link.anchor) for link in links] == [
        ("docs/guides/a.md", "main", "b"),
        ("c.md", "dev", ""),
    ]


def test_a_link_in_code_is_not_a_link() -> None:
    assert repo_links("`[x](../blob/main/a.md)`") == []


def test_a_relative_docs_link_is_broken_on_an_issue_page() -> None:
    assert broken_relative_links("[a](docs/guides/a.md) [b](./research/x.yaml) [c](../blob/main/docs/a.md)") == [
        "docs/guides/a.md",
        "./research/x.yaml",
    ]


def test_a_docs_path_written_out_is_found() -> None:
    body = "Read docs/guides/workspace.md and `research/missions.yaml` first."
    assert plain_doc_paths(body) == ["docs/guides/workspace.md", "research/missions.yaml"]


def test_a_docs_path_that_is_a_link_is_not_plain() -> None:
    body = "[docs/guides/workspace.md](../blob/main/docs/guides/workspace.md) and ../blob/main/docs/x.md"
    assert plain_doc_paths(body) == []


def test_a_docs_path_in_a_code_fence_is_an_example() -> None:
    assert plain_doc_paths("```\ndocs/guides/a.md\n```\n") == []


def test_a_folder_or_an_other_prefix_is_not_a_docs_file() -> None:
    assert plain_doc_paths("the docs/research/ folder, src/docs/a.md, python/docs/b.md") == []


def test_anchors_of_a_page_are_ids_and_heading_slugs() -> None:
    page = "# Title\n\n## Release queue {#release-queue}\n\n### Why we squash?\n\nText\n"
    assert {"release-queue", "why-we-squash", "title"} <= anchors_of(page)


# --- statements about pull requests ---------------------------------------------------------------------------------


def test_state_claims_read_a_number_with_its_state() -> None:
    text = "Blocked by #12 (open) and #13 (merged in #14), #15 (closed), plain #16 and `#17 (open)`."
    assert state_claims(text) == [(12, "open"), (13, "merged"), (15, "closed")]
