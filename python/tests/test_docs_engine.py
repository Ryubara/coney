# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the engine page kind: `docs/engine/`, Coney's own internals and port notes, never what the original does
(docs/guides/writing-docs.md#engine-pages)."""

from __future__ import annotations

from pathlib import Path

import pytest
from research_support import IN_DATA, IN_FUNCTION, NOWHERE, checkout, context, env, rules_of, write

from coney_tools import docs_check
from coney_tools.docs_check import Context, check_page
from coney_tools.docs_page import kind_of

PATH = "docs/engine/boot.md"
INDEX = "docs/engine/index.md"


def engine(text: str, path: str = PATH) -> list[docs_check.Finding]:
    """Every finding for the engine page `text` at `path`, with the made-up executable."""
    return check_page(path, text, Context(research=env()))


def page(body: str = "Coney starts its subsystems in a fixed order.\n") -> str:
    """A small engine page: a title and some prose, with no front matter, sections or Verified line."""
    return f"# Boot\n\n{body}"


# --- the kind ---------------------------------------------------------------------------------------------------------


def test_every_page_of_the_engine_folder_is_an_engine_page() -> None:
    assert kind_of("docs/engine/index.md") == "engine"
    assert kind_of("docs/engine/boot.md") == "engine"
    assert kind_of("docs/engine/port-notes/combat.md") == "engine"
    assert kind_of("docs/engine/diagram.png") == "other"
    assert kind_of("docs/research/combat.md") == "research"
    assert kind_of("docs/guides/building.md") == "guide"


def test_an_engine_page_needs_no_front_matter_sections_or_levels() -> None:
    body = f"It reads the table at {IN_FUNCTION} and {IN_DATA}, and says nothing about its evidence.\n"
    assert engine(page(body)) == []


# --- history ----------------------------------------------------------------------------------------------------------


def test_a_date_is_refused_anywhere_on_an_engine_page() -> None:
    found = engine(page("Coney stopped doing this on 2026-10-05.\n"))
    assert rules_of(found) == ["history"] and found[0].line == 3
    # There are no sections that may hold a date, as In Coney does on a research page.
    assert rules_of(engine(page("## In Coney\n\nChanged 2026-10-05.\n"))) == ["history"]


def test_a_date_in_inline_code_is_refused_and_one_in_a_fenced_block_is_not() -> None:
    assert rules_of(engine(page("It stopped on `2026-10-05`.\n"))) == ["history"]
    assert rules_of(engine(page("It stopped on 2026-10-05 (see `main`).\n"))) == ["history"]
    assert engine(page("The format:\n\n```text\n2026-10-05\n```\n")) == []
    assert engine(page("A file name: [x](../guides/a-2026-10-05.md).\n")) == []


def test_an_issue_number_is_refused_unless_it_is_a_link() -> None:
    assert rules_of(engine(page("The stand-in waits for #123.\n"))) == ["history"]
    assert rules_of(engine(page("The stand-in waits for issue 123.\n"))) == ["history"]
    assert engine(page("The stand-in waits for [#123](issue:123).\n")) == []
    assert engine(page("The stand-in waits for [issue 123](issue:123).\n")) == []
    assert engine(page("The stand-in waits for `#123`.\n")) == []


@pytest.mark.parametrize(
    "link",
    [
        "[#123](issue:123)",
        "[issue #123](issue:123)",
        "[Issue #123](issue:123)",
        "[issue 123](issue:123)",
        "[issues 123](issue:123)",
        "[PR #45](issue:45)",
        "[PR 45](issue:45)",
        "[pull request #45](issue:45)",
        "[ #123 ](issue:123)",
        "[#123][tracker]\n\n[tracker]: issue:123",
        "[#123][Tracker]\n\n[tracker]: issue:123",
        "[#123][]\n\n[#123]: issue:123",
        "[#123]\n\n[#123]: issue:123",
        "[issue #123]\n\n[issue #123]: issue:123",
    ],
)
def test_the_text_of_a_link_may_name_an_issue_or_pull_request(link: str) -> None:
    assert engine(page(f"The stand-in waits for {link}\n")) == []


@pytest.mark.parametrize(
    "link",
    [
        "[#123](../roadmap.md)",
        "[#123](https://example.org/tracker/123)",
        "[#123](repo:ROADMAP.md)",
        "[#123](issue:abc)",
        "[#123](issue:)",
        "[#123](issue:123x)",
        "[#123](issue:124)",
        "[PR #45](issue:123)",
        "[#123](ISSUE:123)",
        '[#123](issue:123 "title")',
        "[#123](#123)",
        "[#123]()",
        "[#123][nowhere]",
        "[#123][]",
        "[#123]",
        "[#123][tracker]\n\n[tracker]: issue:124",
        "[#123][tracker]\n\n[tracker]: ../roadmap.md",
        "[#123][tracker]\n\n[tracker]: issue:abc",
        '[#123][tracker]\n\n[tracker]: issue:123 "title"',
        "[#123]\n\n[#123]: issue:123 fixed on 2026-10-05 #9",
    ],
)
def test_a_link_that_is_not_to_the_issue_it_names_does_not_excuse_the_number(link: str) -> None:
    found = rules_of(engine(page(f"The stand-in waits for {link}\n")))
    assert found and set(found) == {"history"}


def test_a_definition_in_a_fenced_block_defines_nothing() -> None:
    text = page("It waits for [#123][tracker].\n\n```text\n[tracker]: issue:123\n```\n")
    assert rules_of(engine(text)) == ["history"]


def test_a_shortcut_reference_is_a_link_only_when_the_page_defines_it() -> None:
    assert rules_of(engine(page("The stand-in waits for [#123].\n"))) == ["history"]
    assert rules_of(engine(page("The stand-in waits for [#124].\n\n[#123]: issue:123\n"))) == ["history"]


def test_the_words_of_a_link_text_beyond_the_number_are_not_excused() -> None:
    assert rules_of(engine(page("It waits for [the tracker, #123](issue:123).\n"))) == ["history"]
    assert rules_of(engine(page("It waits for [#123 and #124](issue:123).\n"))) == ["history"]


@pytest.mark.parametrize(
    "text",
    [
        "player #1",
        "Player #2 holds the pad",
        "pad #0",
        "gamepad #1",
        "slot #3",
        "controller #1",
        "the buttons #4",
        "port #2",
    ],
)
def test_a_number_after_a_counted_thing_is_not_an_issue(text: str) -> None:
    assert engine(page(f"Coney reads {text} first.\n")) == []


def test_a_number_after_any_other_word_is_still_an_issue() -> None:
    for text in ("It waits for #123", "It is fixed in #84", "see (#84)", "issue #123", "PR #45", "players #1 and #2"):
        assert rules_of(engine(page(f"{text}.\n"))) == ["history"], text


def test_a_counted_number_does_not_excuse_an_issue_on_the_same_line() -> None:
    assert rules_of(engine(page("Player #1 waits for #123.\n"))) == ["history"]


def test_a_link_to_an_issue_does_not_excuse_the_rest_of_the_line() -> None:
    assert rules_of(engine(page("[#123](issue:123) was fixed on 2026-10-05.\n"))) == ["history"]
    assert rules_of(engine(page("[#123](issue:123) and #124.\n"))) == ["history"]


# --- addresses --------------------------------------------------------------------------------------------------------


def test_an_address_is_written_in_full_and_resolves() -> None:
    assert rules_of(engine(page("It sits at 0x123400.\n"))) == ["address-form"]
    assert rules_of(engine(page(f"It sits at {NOWHERE}.\n"))) == ["address-resolve"]
    assert engine(page(f"It sits at {IN_FUNCTION}.\n")) == []


def test_without_the_executable_nothing_is_resolved() -> None:
    assert check_page(PATH, page(f"It sits at {NOWHERE}.\n"), Context()) == []


# --- the guide rules and the length -----------------------------------------------------------------------------------


def test_the_style_rules_of_a_guide_hold() -> None:
    found = engine(page("The owner of the repository runs it from C:\\Users\\someone\\work\\coney.\n"))
    assert "machine-path" in rules_of(found)


def test_a_long_engine_page_is_a_warning_as_a_long_guide_is() -> None:
    found = engine(page("A line.\n\n" * 450))
    assert rules_of(found) == [] and rules_of(found, "warning") == ["length"]
    assert "800" in found[0].message
    assert engine(page("A line.\n\n" * 200)) == []


# --- the index page ---------------------------------------------------------------------------------------------------


def test_an_engine_page_needs_the_sections_index_page(tmp_path: Path) -> None:
    root = checkout(tmp_path, files={PATH: page()})
    result = docs_check.check_files(root, [PATH], context(root))
    assert [(item.rule, item.severity) for item in result.findings[PATH]] == [("index", "error")]
    assert "docs/engine/index.md" in result.findings[PATH][0].message
    write(root, INDEX, page("What the engine section holds.\n"))
    assert docs_check.check_files(root, [PATH, INDEX], context(root)).findings == {}


def test_the_index_page_is_needed_when_a_change_removes_it(tmp_path: Path) -> None:
    root = checkout(tmp_path, files={PATH: page(), INDEX: page("What the engine section holds.\n")})
    assert docs_check.check_changed(root, "main", context(root)).findings == {}
    (root / INDEX).unlink()
    found = docs_check.check_changed(root, "main", context(root))
    assert [item.rule for item in found.failed[INDEX]] == ["index"]


def test_engine_pages_are_counted_as_a_kind(tmp_path: Path) -> None:
    root = checkout(tmp_path, files={INDEX: page("What the engine section holds.\n"), PATH: page()})
    result = docs_check.check_files(root, [INDEX, PATH], context(root))
    assert result.by_kind() == {"engine": (2, 2)}


# --- Coney-side hex constants -----------------------------------------------------------------------------------------


def test_a_coney_side_constant_of_an_address_shape_fails_unless_fenced_or_short() -> None:
    # No clean exemption exists: 0x00ffffff is in the range of the executable's addresses.
    assert rules_of(engine(page("The mask is `0x00ffffff`.\n"))) == ["address-resolve"]
    assert rules_of(engine(page("A heap of 0x01000000 bytes.\n"))) == ["address-resolve"]
    assert engine(page("The mask is `0xffffff`, or 16777215.\n")) == []
    assert engine(page("The mask:\n\n```cpp\nconstexpr auto mask = 0x00ffffff;\n```\n")) == []
