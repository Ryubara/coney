# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the glossary of player words (coney_tools.research_glossary), on made-up terms, pages and addresses."""

from __future__ import annotations

from pathlib import Path

import pytest

from coney_tools import research_glossary as rg

TEXT = """\
- term: mount
  page: combat/grabs.md#mount
  functions: [0x0026f008, "0x0027ec20"]
  also: [pin, ground and pound]
- term: sprint
  page: movement.md
"""
GLOSSARY = Path("research/glossary.yaml")


def problems(text: str) -> list[str]:
    """The problems `parse` reports for `text`, one per line."""
    with pytest.raises(rg.GlossaryError) as caught:
        rg.parse(text)
    return str(caught.value).splitlines()


def test_terms_are_read_with_their_functions_and_other_names() -> None:
    """Addresses may be YAML hex integers or text; a term with only a term and a page has the defaults."""
    mount, sprint = rg.parse(TEXT).terms
    assert (mount.term, mount.page) == ("mount", "combat/grabs.md#mount")
    assert mount.functions == (0x26F008, 0x27EC20)
    assert mount.also == ("pin", "ground and pound")
    assert mount.words == ("mount", "pin", "ground and pound")
    assert (sprint.functions, sprint.also) == ((), ())


def test_a_word_finds_its_term_whatever_its_case() -> None:
    """The term and each other name find the term; a word nobody listed finds nothing."""
    glossary = rg.parse(TEXT)
    found = glossary.find("Ground And Pound")
    assert found is not None and found.term == "mount"
    assert glossary.find("SPRINT") is not None
    assert glossary.find("stamina") is None


def test_an_empty_file_is_no_terms_and_a_missing_one_too(tmp_path: Path) -> None:
    """Before anyone lists a word the glossary is empty."""
    assert rg.parse("").terms == ()
    assert rg.load(tmp_path, GLOSSARY).terms == ()
    (tmp_path / "research").mkdir()
    (tmp_path / GLOSSARY).write_text(TEXT, encoding="utf-8")
    assert len(rg.load(tmp_path, GLOSSARY).terms) == 2


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("{a: 1}\n", "must be a list"),
        ("- 3\n", "not a mapping"),
        ("- {page: a.md}\n", "term must be"),
        ("- {term: x}\n", "page must be"),
        ("- {term: x, page: 'Not A Page'}\n", "page must be"),
        ("- {term: x, page: a.md, functions: [5.5]}\n", "is not an address"),
        ("- {term: x, page: a.md, functions: ['0x1234']}\n", "is not an address"),
        ("- {term: x, page: a.md, functions: 5}\n", "functions must be a list"),
        ("- {term: x, page: a.md, also: [1]}\n", "also must be a list of words"),
        ("- {term: x, page: a.md, extra: 1}\n", "unknown key 'extra'"),
        ("- {term: x, page: a.md}\n- {term: X, page: b.md}\n", "a name of both"),
        ("- {term: x, page: a.md}\n- {term: y, page: b.md, also: [x]}\n", "a name of both"),
        ("- [unclosed\n", "not valid YAML"),
    ],
)
def test_a_faulty_glossary_is_refused_with_what_to_fix(text: str, message: str) -> None:
    """Each fault names the file and the entry."""
    found = problems(text)
    assert any(message in line for line in found), found
    assert all(line.startswith("research/glossary.yaml") for line in found)


def test_the_check_against_the_checkout_names_a_missing_page_section_or_function() -> None:
    """A page and section that do not exist, and an address that is no function, are reported by term."""
    glossary = rg.parse(TEXT)

    def page_problem(target: str) -> str:
        return "" if target == "movement.md" else f"the page {target} does not exist"

    found = rg.check(glossary, page_problem, lambda address: address == 0x26F008)
    assert found == [
        "research/glossary.yaml: mount: the page combat/grabs.md#mount does not exist",
        "research/glossary.yaml: mount: 0x0027ec20 is no function entry in the Ghidra listing",
    ]
    assert rg.check(glossary, lambda target: "", None) == []
