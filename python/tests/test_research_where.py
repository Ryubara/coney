# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `research where` (coney_tools.research_where), on a made-up checkout of pages, files and functions."""

from __future__ import annotations

from pathlib import Path

import pytest
from test_research_functions_check import with_conventions

from coney_tools import progress_research, research_where
from coney_tools.config import ConfigError

GRABS = """\
---
kind: topic
system: combat
owns:
  functions: ["0x00100200", "0x00100400"]
---
# Combat: grabs

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

Circle grabs.

## Map

| Behaviour | Function |
| --- | --- |
| Grab | `0x00100200` `Grab_Start` |

## Behaviour

### The grab {#grab}

`Grab_Start` (`0x00100200`) plays the intro (confirmed (code)).

## Open questions

- **The counter chance** {#q-counter}: `0x00100200` rolls a number; unknown how.
"""
COMBAT = """\
---
kind: system
system: combat
owns:
  files: ["Combat/Grab.cpp"]
---
# Combat

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

The state bits.

## Behaviour

### State flags {#state-flags}

`Grab_End` (`0x00100400`) clears them (confirmed (code)); `Grab_Start` (`0x00100200`) sets them.
"""
FUNCTIONS = """\
[[function]]
address = 0x00100200
name = "Grab_Start"
page = "combat/grabs.md#grab"
role = "plays the grab intro"
level = "confirmed-code"
coney = "src/core/combat/grab.cpp:Grab::start"
status = "compared"
"""
SOURCE_FILES = """\
[[file]]
directory = "Combat"
path = "Combat/Grab.cpp"
start = 0x00100000
end = 0x00100600
functions = 3
anchors = [
  {address = 0x00100200, tags = ["Grab"]},
]

[[file]]
directory = "Combat"
path = "Combat/Gone.cpp"
path_string = 0x00540000
functions = 0
"""
GLOSSARY = """\
- term: mount
  page: combat/grabs.md#grab
  functions: [0x00100200]
  also: [pin]
"""
COMBAT_LINE = next(i for i, text in enumerate(COMBAT.splitlines(), start=1) if "Grab_End" in text)
TAG = "// @orig 0x00100200 Grab_Start (Grab.cpp)\nvoid grabStart();\n"
LISTING = [
    progress_research.ListedFunction(0x100200, 0x200, "Grab_Start", True),
    progress_research.ListedFunction(0x100400, 0x100, "Grab_End", True),
    progress_research.ListedFunction(0x100500, 0x40, "", False),
    progress_research.ListedFunction(0x200000, 0x40, "Other_Function", True),
]


def write(root: Path, name: str, text: str) -> None:
    """Write `text` to `name` below `root`, making the folders."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def checkout(tmp_path: Path, *, glossary: str = GLOSSARY) -> Path:
    """A made-up checkout: two pages, a function database, source files, a glossary, a listing and an `@orig` tag."""
    root = with_conventions(tmp_path)
    write(root, "docs/research/combat/grabs.md", GRABS)
    write(root, "docs/research/combat.md", COMBAT)
    write(root, "research/functions/combat.toml", FUNCTIONS)
    write(root, "research/source-files.toml", SOURCE_FILES)
    write(root, "research/glossary.yaml", glossary)
    write(root, "src/core/combat/grab.cpp", TAG)
    write(root, "docs/progress/ghidra-functions.tsv", progress_research.format_listing(LISTING))
    return root


@pytest.fixture
def index(tmp_path: Path) -> research_where.Index:
    """The index of the made-up checkout."""
    return research_where.Index.load(checkout(tmp_path))


def test_a_function_entry_reports_everything_known_about_it(index: research_where.Index) -> None:
    """The name and size, the source file, the owning page and how it owns, the database entry, the port's tag and
    where else it is cited."""
    [report] = index.search("0x00100200").addresses
    assert (report.entry, report.name, report.size, report.offset) == (0x100200, "Grab_Start", 0x200, 0)
    assert report.source_file == "Combat/Grab.cpp"
    assert report.claim is not None
    assert (report.claim.page, report.claim.via) == ("docs/research/combat/grabs.md", "function")
    assert report.record is not None and report.record[0] == "combat"
    assert report.orig == ("src/core/combat/grab.cpp:1",)
    elsewhere = [p for p in report.cited if p.page == "docs/research/combat.md"]
    assert [(p.line, p.anchor) for p in elsewhere] == [(COMBAT_LINE, "state-flags")]
    assert [(p.page, p.anchor) for p in report.questions] == [("docs/research/combat/grabs.md", "q-counter")]


def test_an_address_inside_a_function_is_the_function(index: research_where.Index) -> None:
    """The report is of the function that holds the address, with how far into it the address lies."""
    [report] = index.search("0x00100250").addresses
    assert (report.entry, report.name, report.offset) == (0x100200, "Grab_Start", 0x50)
    assert report.claim is not None and report.claim.page == "docs/research/combat/grabs.md"


def test_an_address_no_page_owns_says_so(index: research_where.Index) -> None:
    """An unnamed function in a file whose page does not own its part: the file's page, then no page at all."""
    [inside] = index.search("0x00100500").addresses
    assert inside.name == "" and inside.claim is not None and inside.claim.via == "file"
    [outside] = index.search("0x00200000").addresses
    assert outside.claim is None and outside.record is None
    lines = research_where.address_lines(outside)
    assert "owning page: none (no page owns this address yet)" in lines
    assert "function database: no entry" in lines


def test_an_address_outside_every_function_is_placed_in_its_section(index: research_where.Index) -> None:
    """Data and addresses past the image have no function."""
    [gap] = index.search("0x00100700").addresses
    assert gap.entry is None
    assert any("not in a function" in line for line in research_where.address_lines(gap))


def test_a_bad_address_is_a_usage_error(index: research_where.Index) -> None:
    """`0x` and something that is not hex."""
    with pytest.raises(ConfigError, match="not an address"):
        index.search("0xzz")


def test_a_ghidra_name_finds_its_function_whatever_the_case(index: research_where.Index) -> None:
    """An exact name first, then the same name in other case; the report is the address report."""
    assert [r.entry for r in index.search("Grab_Start").addresses] == [0x100200]
    assert [r.entry for r in index.search("grab_start").addresses] == [0x100200]


def test_part_of_a_name_lists_the_functions_that_contain_it(index: research_where.Index) -> None:
    """No exact name: the functions whose names hold the text, to narrow down."""
    result = index.search("Grab_")
    assert result.addresses == ()
    assert result.similar == ((0x100200, "Grab_Start"), (0x100400, "Grab_End"))


def test_a_source_file_is_found_by_its_path_or_its_name(index: research_where.Index) -> None:
    """The file, its range, and the page that owns it by listing it."""
    for query in ("Combat/Grab.cpp", "Grab.cpp", "combat/grab.cpp"):
        [report] = index.search(query).files
        assert report.file.path == "Combat/Grab.cpp"
        assert report.claim is not None and (report.claim.via, report.claim.page) == ("file", "docs/research/combat.md")
    [none] = index.search("Gone.cpp").files
    assert none.file.start is None and none.claim is None


def test_a_word_is_found_in_the_glossary_with_its_functions(index: research_where.Index) -> None:
    """The term or another name for it: the page and the functions with their names."""
    for query in ("mount", "PIN"):
        [word] = index.search(query).words
        assert word.term is not None and word.term.page == "combat/grabs.md#grab"
        assert word.names == ((0x100200, "Grab_Start"),)


def test_a_word_not_in_the_glossary_is_looked_for_in_the_headings(index: research_where.Index) -> None:
    """The fallback: pages with a heading that holds the word."""
    [word] = index.search("state flags").words
    assert word.term is None
    assert [(p.page, p.anchor) for p in word.headings] == [("docs/research/combat.md", "state-flags")]


def test_something_nobody_knows_finds_nothing(index: research_where.Index) -> None:
    """No address, name, file or word: not found, and the caller says how to ask."""
    assert not index.search("nothing at all").found


def test_the_text_names_each_fact_on_its_own_line(index: research_where.Index) -> None:
    """The lines a reader gets for a function."""
    text = "\n".join(research_where.render(index.search("Grab_Start")))
    assert text.startswith("0x00100200 Grab_Start (function, 512 bytes)")
    assert "  source file: Combat/Grab.cpp" in text
    assert "  owning page: docs/research/combat/grabs.md (lists function 0x00100200)" in text
    assert "  function database: combat.toml: confirmed-code, described at combat/grabs.md#grab" in text
    assert "    port: src/core/combat/grab.cpp:Grab::start (compared)" in text
    assert "  @orig in: src/core/combat/grab.cpp:1" in text
    assert f"also cited on: docs/research/combat.md:{COMBAT_LINE} (#state-flags)" in text
    assert "open questions naming it: docs/research/combat/grabs.md:" in text


def test_a_place_list_is_cut_after_a_few(tmp_path: Path) -> None:
    """A function every page cites does not fill the screen."""
    root = checkout(tmp_path)
    for number in range(research_where.SHOWN + 3):
        write(root, f"docs/research/more{number}.md", "# More\n\nSee `0x00100200` (confirmed (code)).\n")
    text = "\n".join(research_where.render(research_where.Index.load(root).search("Grab_Start")))
    assert "and 4 more" in text


def test_the_json_has_the_same_facts(index: research_where.Index) -> None:
    """`--json` for a tool: addresses as hex text, the owner as page, via and what."""
    data = research_where.to_json(index.search("0x00100200"))
    [address] = data["addresses"]
    assert address["address"] == "0x00100200"
    assert address["owner"] == {"page": "docs/research/combat/grabs.md", "via": "function", "what": "0x00100200"}
    assert address["database"]["system"] == "combat"
    assert address["orig"] == ["src/core/combat/grab.cpp:1"]
    assert data["files"] == [] and data["words"] == []


def test_the_study_lines_say_where_to_write(tmp_path: Path) -> None:
    """What `original study` prints: a heading and the report indented."""
    lines = research_where.study_lines(checkout(tmp_path), 0x100200)
    assert lines[0] == "research (coney-tools research where):"
    assert any(line.startswith("  owning page: docs/research/combat/grabs.md") for line in lines)


def test_the_study_lines_survive_a_broken_database(tmp_path: Path) -> None:
    """A malformed data file must not stop a study: the lookup says it is unavailable."""
    root = checkout(tmp_path)
    write(root, "research/functions/combat.toml", "surprise = 1\n")
    [line] = research_where.study_lines(root, 0x100200)
    assert line.startswith("research lookup: not available")


def test_without_a_listing_the_address_is_not_called_a_non_function(tmp_path: Path) -> None:
    """A checkout without the Ghidra listing cannot say whether an address is in a function."""
    root = checkout(tmp_path)
    (root / "docs/progress/ghidra-functions.tsv").unlink()
    [report] = research_where.Index.load(root).search("0x00100200").addresses
    assert report.listed is False
    assert not any("not in a function" in line for line in research_where.address_lines(report))
