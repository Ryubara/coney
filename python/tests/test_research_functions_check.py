# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the checks that read the checkout around the function database (coney_tools.research_functions_check)."""

from __future__ import annotations

from pathlib import Path

from test_research_functions import write_systems

from coney_tools import progress_research, research_functions_check

GRABS = """\
# Combat: grabs

## The grab {#grab}

Text.

## Open questions

- **The counter chance** {#q-counter}: unknown.
"""

ENTRY = """\
[[function]]
address = 0x00100200
name = "Grab_Start"
page = "combat/grabs.md#grab"
role = "plays the grab intro"
level = "confirmed-code"
coney = "src/core/combat/grab.cpp:Grab::start"
"""


def make_root(
    tmp_path: Path,
    functions: str = ENTRY,
    listing: dict[int, str] | None = None,
    extra_pages: dict[str, str] | None = None,
) -> Path:
    """A made-up checkout with one page, one source file, a Ghidra listing and a `combat` system file."""
    pages = {"combat/grabs.md": GRABS, **(extra_pages or {})}
    for name, text in pages.items():
        path = tmp_path / "docs" / "research" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    source = tmp_path / "src" / "core" / "combat" / "grab.cpp"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text("// stand-in\n", encoding="utf-8")
    functions_listing = {0x100200: "Grab_Start"} if listing is None else listing
    listed = [progress_research.ListedFunction(a, 64, n, True) for a, n in functions_listing.items()]
    listing_path = tmp_path / progress_research.LISTING_FILE
    listing_path.parent.mkdir(parents=True, exist_ok=True)
    listing_path.write_text(progress_research.format_listing(listed), encoding="utf-8")
    write_systems(tmp_path, {"combat": functions})
    return tmp_path


def run(root: Path) -> research_functions_check.Findings:
    """The findings for the checkout at `root`."""
    return research_functions_check.check_root(root)


def test_a_consistent_checkout_has_no_findings(tmp_path: Path) -> None:
    """The page, the section, the Ghidra name, the source file and the order all agree."""
    findings = run(make_root(tmp_path))
    assert findings.errors == []
    assert findings.warnings == []


def test_no_system_files_is_fine(tmp_path: Path) -> None:
    """A checkout that has no function database yet has nothing to find fault with."""
    assert run(tmp_path).errors == []


def test_a_missing_page_is_an_error(tmp_path: Path) -> None:
    """The entry's page must be a file below docs/research."""
    root = make_root(tmp_path, ENTRY.replace("combat/grabs.md#grab", "combat/throws.md#grab"))
    [error] = run(root).errors
    assert "combat.toml" in error
    assert "Grab_Start" in error
    assert "docs/research/combat/throws.md" in error


def test_a_missing_section_is_an_error(tmp_path: Path) -> None:
    """The entry's section must be an anchor of its page."""
    root = make_root(tmp_path, ENTRY.replace("#grab", "#grabbing"))
    [error] = run(root).errors
    assert "grabbing" in error
    assert "combat/grabs.md" in error


def test_a_page_without_a_section_is_accepted(tmp_path: Path) -> None:
    """A function that belongs to a page as a whole names no section."""
    root = make_root(tmp_path, ENTRY.replace("#grab", ""))
    assert run(root).errors == []


def test_an_address_ghidra_does_not_list_is_an_error(tmp_path: Path) -> None:
    """The address must be a function entry in the committed Ghidra listing."""
    root = make_root(tmp_path, listing={0x100300: "Other"})
    [error] = run(root).errors
    assert "0x00100200" in error
    assert "ghidra-functions.tsv" in error


def test_a_name_that_differs_from_ghidra_is_an_error(tmp_path: Path) -> None:
    """The name is Ghidra's: the fix is the page or a refreshed listing, never a second name."""
    root = make_root(tmp_path, listing={0x100200: "Grab_Begin"})
    [error] = run(root).errors
    assert "Grab_Start" in error
    assert "Grab_Begin" in error


def test_a_name_ghidra_has_not_got_yet_is_a_warning(tmp_path: Path) -> None:
    """While the listing still has Ghidra's default name, the listing may be stale: say so without failing."""
    findings = run(make_root(tmp_path, listing={0x100200: "FUN_00100200"}))
    assert findings.errors == []
    [warning] = findings.warnings
    assert "FUN_00100200" in warning


def test_no_listing_means_no_name_check(tmp_path: Path) -> None:
    """A checkout without the listing cannot check names, and says nothing about them."""
    root = make_root(tmp_path)
    (root / progress_research.LISTING_FILE).unlink()
    assert run(root).errors == []


def test_a_port_in_a_file_that_is_missing_is_an_error(tmp_path: Path) -> None:
    """`coney` points at a source file that exists; the symbol after the colon is for the reader."""
    root = make_root(tmp_path, ENTRY.replace("grab.cpp", "gone.cpp"))
    [error] = run(root).errors
    assert "src/core/combat/gone.cpp" in error


def test_a_gap_question_must_exist(tmp_path: Path) -> None:
    """A question a gap links is a section of a page that exists."""
    gap = "[[function.gaps]]\nwhat = 'fixed chance'\nrelation = 'stand-in'\nquestion = 'combat/grabs.md#q-%s'\n"
    assert run(make_root(tmp_path, ENTRY + gap % "counter")).errors == []
    errors = run(make_root(tmp_path, ENTRY + gap % "nothing")).errors
    assert len(errors) == 1
    assert "q-nothing" in errors[0]


def test_a_committed_generated_page_is_an_error(tmp_path: Path) -> None:
    """The list page is made by the docs build; a hand-committed one at its path would hide it."""
    root = make_root(tmp_path, extra_pages={"combat/functions.md": "# Stale\n"})
    [error] = run(root).errors
    assert "docs/research/combat/functions.md" in error
    assert "generated" in error


def test_a_broken_file_is_reported_as_an_error(tmp_path: Path) -> None:
    """A schema problem comes back as an error line, so the checks and the schema share one report."""
    root = make_root(tmp_path, ENTRY.replace("confirmed-code", "sure"))
    findings = run(root)
    assert any("level" in e for e in findings.errors)
