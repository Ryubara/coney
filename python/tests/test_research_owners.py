# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for who owns an address (coney_tools.research_owners), on made-up pages, files and addresses."""

from __future__ import annotations

from dataclasses import replace

import research_support
from research_support import RESEARCH_PATH, findings, front, page

from coney_tools import research_check, research_owners
from coney_tools.research_functions import Entry
from coney_tools.research_pages import Corpus
from coney_tools.research_subject import ResearchEnv
from coney_tools.source_files import FileRange

FILES = (
    FileRange("Combat/Grab.cpp", 0x100000, 0x100400),
    FileRange("Combat/Hold.cpp", 0x100400, 0x100600),
)


def pages(**owns: str) -> Corpus:
    """A corpus of made-up pages by name (below docs/research), each owning what its value says."""
    texts = {
        f"docs/research/{name}.md": page(meta=front("survey", owns=text), kind="survey") for name, text in owns.items()
    }
    return Corpus.from_texts(texts, research_support.research())


def test_a_page_owns_the_functions_ranges_and_files_it_lists() -> None:
    """Each way of owning, and what the claim says about it."""
    owners = research_owners.Owners(
        pages(
            grabs="{functions: ['0x00100200']}",
            holds="{ranges: ['0x00300000-0x00300100']}",
            combat="{files: ['Combat/Hold.cpp']}",
        ),
        FILES,
    )
    function = owners.claim(0x100200)
    assert function == research_owners.Claim("docs/research/grabs.md", "function", "0x00100200")
    assert owners.claim(0x300050) == research_owners.Claim("docs/research/holds.md", "range", "0x00300000-0x00300100")
    assert owners.claim(0x100500) == research_owners.Claim("docs/research/combat.md", "file", "Combat/Hold.cpp")
    assert owners.claim(0x100100) is None
    assert owners.owner(0x100100) == ""


def test_the_most_specific_entry_wins() -> None:
    """A function beats the range it lies in, and a narrower range or file beats a wider one."""
    owners = research_owners.Owners(
        pages(
            wide="{ranges: ['0x00100000-0x00200000']}",
            narrow="{files: ['Combat/Grab.cpp']}",
            one="{functions: ['0x00100200']}",
        ),
        FILES,
    )
    assert owners.owner(0x100200) == "docs/research/one.md"
    assert owners.owner(0x100204) == "docs/research/narrow.md"
    assert owners.owner(0x100500) == "docs/research/wide.md"


def test_a_file_name_matches_without_regard_to_case_and_a_file_with_no_range_owns_nothing() -> None:
    """The source map's names are compared as the `owns` check compares them."""
    owners = research_owners.Owners(pages(a="{files: ['combat/grab.CPP', 'Nowhere/None.cpp']}"), FILES)
    assert owners.owner(0x100010) == "docs/research/a.md"
    assert owners.owner(0x100500) == ""


def test_a_page_without_owns_owns_nothing() -> None:
    """A page that says nothing about what it owns is not asked."""
    corpus = Corpus.from_texts({"docs/research/x.md": "# X\n"}, research_support.research())
    assert research_owners.Owners(corpus, FILES).claim(0x100010) is None


def entry(address: int, page_name: str) -> Entry:
    """A function database entry pointing at `page_name`."""
    return Entry(address, "Some_Function", page_name, "inferred", role="r")


def test_an_entry_is_checked_against_the_owner_of_its_address() -> None:
    """The entry's page must be the owner; a page that has front matter and owns nothing is told so."""
    owners = research_owners.Owners(pages(grabs="{functions: ['0x00100200']}", holds="{functions: ['0x00100300']}"))
    assert owners.problem(entry(0x100200, "grabs.md#x")) == ""
    assert "docs/research/holds.md owns it, not docs/research/grabs.md" in owners.problem(entry(0x100300, "grabs.md#x"))
    assert "does not own it" in owners.problem(entry(0x100400, "grabs.md#x"))
    assert owners.problem(entry(0x100400, "unlisted.md#x")) == ""


def env_with_files() -> ResearchEnv:
    """The made-up environment with the source files of this module."""
    return replace(research_support.env(), source_files=FILES)


def test_docs_check_refuses_an_owned_file_the_data_does_not_have() -> None:
    """`owns.files` names a source file with a range in research/source-files.toml, or the page owns nothing."""
    meta = front("topic", owns="{files: ['Combat/Grab.cpp', 'Combat/Gone.cpp']}")
    found = findings(RESEARCH_PATH, page(meta=meta), env_with_files())
    messages = [item.message for item in found if item.rule == "owns"]
    assert len(messages) == 1
    assert "Combat/Gone.cpp" in messages[0] and "source-files.toml" in messages[0]


def test_the_refusal_suggests_the_name_it_is_near() -> None:
    """A path with the directory left out is the usual slip."""
    meta = front("topic", owns="{files: ['Combat/Grab.cp']}")
    found = findings(RESEARCH_PATH, page(meta=meta), env_with_files())
    assert any("did you mean Combat/Grab.cpp?" in item.message for item in found if item.rule == "owns")


def test_without_source_files_data_an_owned_file_is_not_checked() -> None:
    """A checkout that has no data file (a made-up one) cannot say which names are right."""
    meta = front("topic", owns="{files: ['Anything/At.all']}")
    found = findings(RESEARCH_PATH, page(meta=meta))
    assert [item for item in found if item.rule == "owns"] == []


def test_the_check_reports_under_the_existing_owns_rule() -> None:
    """The legacy file waives rules by name; the file check adds none, it reports as `owns`."""
    assert "owns" in research_check.RULES


def conflicts(mine: str, theirs: str) -> list[str]:
    """The `owns` messages for a topic page owning `mine` when a survey page owns `theirs` (files of this module)."""
    others = {"docs/research/other.md": page(meta=front("survey", owns=theirs), kind="survey")}
    env = replace(research_support.env(others), source_files=FILES)
    found = findings(RESEARCH_PATH, page(meta=front("survey", owns=mine), kind="survey"), env)
    return [item.message for item in found if item.rule == "owns"]


def test_a_file_and_an_identical_range_owned_by_two_pages_are_refused() -> None:
    """A file is its range: owning `Combat/Grab.cpp` and owning `0x00100000-0x00100400` is owning the same code."""
    for mine, theirs in (
        ("{files: ['Combat/Grab.cpp']}", "{ranges: ['0x00100000-0x00100400']}"),
        ("{ranges: ['0x00100000-0x00100400']}", "{files: ['Combat/Grab.cpp']}"),
    ):
        messages = conflicts(mine, theirs)
        assert len(messages) == 1, (mine, theirs)
        assert "docs/research/other.md" in messages[0]


def test_a_file_and_a_crossing_range_owned_by_two_pages_are_refused() -> None:
    """A range that starts inside the file and ends outside it takes part of two owners' code."""
    assert conflicts("{files: ['Combat/Grab.cpp']}", "{ranges: ['0x001003f0-0x00100500']}")
    assert conflicts("{ranges: ['0x001003f0-0x00100500']}", "{files: ['Combat/Grab.cpp']}")


def test_a_range_inside_a_file_or_a_file_inside_a_range_is_one_page_taking_part_of_the_other() -> None:
    """The most specific entry settles these, so they are not conflicts."""
    assert conflicts("{files: ['Combat/Grab.cpp']}", "{ranges: ['0x00100100-0x00100200']}") == []
    assert conflicts("{ranges: ['0x00100100-0x00100200']}", "{files: ['Combat/Grab.cpp']}") == []
    assert conflicts("{files: ['Combat/Grab.cpp']}", "{ranges: ['0x00100000-0x00100800']}") == []


def test_two_different_files_do_not_conflict() -> None:
    """Their ranges are disjoint."""
    assert conflicts("{files: ['Combat/Grab.cpp']}", "{files: ['Combat/Hold.cpp']}") == []
