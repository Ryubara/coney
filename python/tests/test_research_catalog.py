# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.research_catalog: what an address and a run id are checked against, from made-up data."""

from __future__ import annotations

from pathlib import Path

import pytest
from conventions_support import REPO
from research_support import FUNCTIONS, SECTIONS, catalog, research

from coney_tools.config import ConfigError
from coney_tools.research_catalog import Catalog, Section, load_catalog, parse_sections

LISTING = "# header\n0x00123400\t256\tWidget_Spin\t1\n0x00123500\t128\t\t0\n"
SECTIONS_TOML = (
    "[[section]]\nname = '.text'\nstart = 0x00100000\nend = 0x00300000\ncode = true\n\n"
    "[[section]]\nname = '.data'\nstart = 0x00300000\nend = 0x00400000\n"
)


def test_a_function_is_found_by_its_entry_and_by_any_address_inside_it() -> None:
    found = catalog()
    assert found.entry(0x00123400) == FUNCTIONS[0] and found.entry(0x00123401) is None
    assert found.containing(0x001234FF) == FUNCTIONS[0] and found.containing(0x00123500) == FUNCTIONS[1]
    assert found.containing(0x00123580) is None and found.containing(0x00100000) is None


def test_a_function_of_size_zero_still_holds_its_entry() -> None:
    from coney_tools.progress_research import ListedFunction

    empty = Catalog((ListedFunction(0x00123400, 0, "", False),))
    assert empty.containing(0x00123400) is not None and empty.containing(0x00123401) is None


def test_an_address_resolves_in_a_function_or_a_data_section_but_not_in_code_outside_one() -> None:
    found = catalog()
    assert found.resolves(0x00123456) and found.resolves(0x00350000) and found.resolves(0x00450000)
    assert not found.resolves(0x00150000)  # .text, no function
    assert not found.resolves(0x00500000)  # past the image
    assert not found.resolves(0x00001000)


def test_without_a_listing_every_address_resolves() -> None:
    nothing = Catalog()
    assert not nothing.can_resolve and nothing.resolves(0x00150000) and nothing.resolves(0x7FFFFFFF)


def test_the_image_spans_the_sections() -> None:
    assert catalog().image == (0x00100000, 0x00500000)
    assert Catalog(FUNCTIONS).image is None
    assert catalog().in_image(0x00450000) and not catalog().in_image(0x00500000)


def test_a_run_exists_only_when_the_folder_has_it() -> None:
    # No folder, no record: nothing can back a run id until the first run record creates research/runs/.
    assert not catalog().has_records and not catalog().has_run("anything")
    assert catalog(runs=frozenset()).has_records
    assert catalog(runs=frozenset({"r-1"})).has_run("r-1")
    assert not catalog(runs=frozenset({"r-1"})).has_run("r-2")
    assert not catalog(runs=frozenset()).has_run("r-1")


def test_the_sections_file_is_read_in_address_order() -> None:
    sections = parse_sections(SECTIONS_TOML)
    assert sections == (Section(".text", 0x100000, 0x300000, True), Section(".data", 0x300000, 0x400000))
    assert parse_sections("") == () and SECTIONS[0].code


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("[[section]]\nname = '.text'\nstart = 1\n", "needs a name, a start and an end"),
        ("[[section]]\nname = '.text'\nstart = 5\nend = 4\n", "ends before it starts"),
        ("[[section]\n", "not valid TOML"),
    ],
)
def test_a_sections_file_that_cannot_be_used_is_an_error(text: str, message: str) -> None:
    with pytest.raises(ConfigError, match=message):
        parse_sections(text)


def test_the_catalog_is_read_from_the_checkout(tmp_path: Path) -> None:
    rules = research()
    for name, text in ((rules.files.ghidra_functions, LISTING), (rules.files.elf_sections, SECTIONS_TOML)):
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text, encoding="utf-8")
    runs = tmp_path / rules.files.runs
    runs.mkdir(parents=True)
    (runs / "r-1.toml").write_text("", encoding="utf-8")
    found = load_catalog(tmp_path, rules)
    assert [function.address for function in found.functions] == [0x123400, 0x123500]
    assert found.image == (0x100000, 0x400000) and found.runs == frozenset({"r-1"})


def test_a_checkout_without_the_data_has_an_empty_catalog(tmp_path: Path) -> None:
    found = load_catalog(tmp_path, research())
    assert found.functions == () and found.sections == () and found.runs is None


def test_a_listing_that_is_damaged_is_an_error(tmp_path: Path) -> None:
    rules = research()
    (tmp_path / rules.files.ghidra_functions).parent.mkdir(parents=True)
    (tmp_path / rules.files.ghidra_functions).write_text("0x1\tx\n", encoding="utf-8")
    with pytest.raises(ConfigError, match="expected"):
        load_catalog(tmp_path, rules)


@pytest.mark.reads("docs_only")
def test_this_repositorys_data_covers_the_executable() -> None:
    found = load_catalog(REPO, research())
    assert found.can_resolve and found.image is not None and found.image[0] == 0x00100000
    assert found.entry(0x00100008) is not None and not found.resolves(0x00001000)
    text = next(section for section in found.sections if section.code)
    assert text.name == ".text" and not found.resolves(text.end)
