# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.docs_beyond: the page of how Coney goes beyond the original, held to the `@coney-only limit:`
markers of the sources (docs/guides/coney-tools-build.md#beyond).

The markers are read from made-up sources and the page is a made-up page, so each rule is shown failing on the smallest
change from a passing pair. The fixtures hold no game data.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from conventions_support import REMOVE, REPO, changed
from research_support import checkout, write

from coney_tools import docs_beyond, docs_check
from coney_tools.conventions import BeyondPage, ConventionsError, load_conventions, parse_conventions
from coney_tools.docs_beyond import Marker, markers_in

BEYOND = load_conventions(REPO).standards.beyond
PAGE = BEYOND.page
MARKER = BEYOND.marker

#: A header comment, a marker and a declaration, as the sources write them (docs/guides/conventions.md#original-limits).
FUNCTION = f"""\
/// The tokens of one update.
/// {MARKER} the original gives 5 an update; past 60 brains Coney gives 5 per 60
[[nodiscard]] int scanTokens(std::size_t scanningBrains);
"""


def marker_keys(text: str, path: str = "src/core/crowd.h") -> list[str]:
    """The keys of the markers `text` carries, in order."""
    return [marker.key for marker in markers_in(path, text, MARKER)]


# --- reading the markers from the sources ---------------------------------------------------------------------------


def test_a_marker_is_keyed_by_the_file_and_the_name_the_comment_documents() -> None:
    assert marker_keys(FUNCTION) == ["src/core/crowd.h:scanTokens"]


@pytest.mark.parametrize(
    ("declaration", "name"),
    [
        ("inline constexpr std::size_t kCap = 20;", "kCap"),
        ("struct Pool {", "Pool"),
        ("class Pool : public Base {", "Pool"),
        ("template <typename T>\n[[nodiscard]] T limitOf(T value);", "limitOf"),
        ("std::size_t Crowd::cap() const {", "cap"),
        ("static constexpr int kSlots{60};", "kSlots"),
        ("enum class Size : int {", "Size"),
        ("using Handles = std::array<int, 60>;", "Handles"),
    ],
)
def test_the_name_is_the_last_word_before_the_declarations_parameters_value_or_body(
    declaration: str, name: str
) -> None:
    text = f"/// What it is.\n/// {MARKER} the original holds 60\n{declaration}\n"
    assert marker_keys(text) == [f"src/core/crowd.h:{name}"]


def test_a_marker_may_run_over_several_comment_lines_and_a_declaration_over_several_lines() -> None:
    text = (
        f"/// {MARKER} the original's lists hold 60 handles,\n"
        "/// one per human slot; past 60 Coney's hold every human\n"
        "[[nodiscard]] std::size_t\n"
        "humanListCap(std::size_t humans);\n"
    )
    assert marker_keys(text) == ["src/core/crowd.h:humanListCap"]


def test_a_marker_in_a_block_comment_is_read_too() -> None:
    text = f"/**\n * {MARKER} the original holds 60\n */\nint cap();\n"
    assert marker_keys(text) == ["src/core/crowd.h:cap"]


def test_a_trailing_marker_documents_the_declaration_on_its_line() -> None:
    text = f"inline constexpr int kRoutes = 32; // {MARKER} the original keeps a pool of 32\n"
    assert marker_keys(text) == ["src/core/crowd.h:kRoutes"]


def test_a_marker_above_no_declaration_stands_for_the_whole_file() -> None:
    text = f"// {MARKER} the original holds 60; this file lifts them\n#pragma once\n"
    assert marker_keys(text) == ["src/core/crowd.h:(file)"]
    assert marker_keys(f"// {MARKER} the original holds 60\nnamespace coney {{\n") == ["src/core/crowd.h:(file)"]


def test_every_marker_of_a_file_is_listed_with_its_line() -> None:
    text = FUNCTION + "\n" + FUNCTION.replace("scanTokens", "humanListCap")
    found = markers_in("src/core/crowd.h", text, MARKER)
    assert [(marker.symbol, marker.line) for marker in found] == [("scanTokens", 2), ("humanListCap", 6)]


def test_a_marker_counts_only_at_the_start_of_a_comment() -> None:
    assert marker_keys(f"/// The rule is {MARKER} the original holds 60\nint cap();\n") == []
    assert marker_keys(f"// See {MARKER} in conventions.md\nint cap();\n") == []
    assert marker_keys(f'const char* kText = "{MARKER} x";\n') == []
    assert marker_keys(f"/** {MARKER} the original holds 60 */\nint cap();\n") == ["src/core/crowd.h:cap"]
    assert marker_keys(f"//! {MARKER} the original holds 60\nint cap();\n") == ["src/core/crowd.h:cap"]


def test_a_trailing_block_comment_marker_documents_the_declaration_on_its_line() -> None:
    text = f"inline constexpr int kRoutes = 32; /* {MARKER} the original keeps a pool of 32 */\n"
    assert marker_keys(text) == ["src/core/crowd.h:kRoutes"]


def test_an_operator_is_named_with_its_sign() -> None:
    text = f"/// {MARKER} the original holds 60\nbool operator==(const Pool& other) const;\n"
    assert marker_keys(text) == ["src/core/crowd.h:operator=="]
    text = f"/// {MARKER} the original holds 60\nint operator()(int slot) const;\n"
    assert marker_keys(text) == ["src/core/crowd.h:operator()"]


def test_overloads_of_one_name_have_keys_of_their_own() -> None:
    one = f"/// {MARKER} the original holds 60\nint cap(int humans);\n"
    two = f"/// {MARKER} the original holds 60\nint cap(double scale);\n"
    assert marker_keys(one + two + two) == [
        "src/core/crowd.h:cap",
        "src/core/crowd.h:cap#2",
        "src/core/crowd.h:cap#3",
    ]


def test_an_entry_names_an_operator_and_an_overload_in_its_cell() -> None:
    cell = "`src/core/crowd.h:operator==`, `src/core/crowd.h:cap#2`"
    text = page((row("Pool", marker=cell),))
    assert check(text, "src/core/crowd.h:operator==", "src/core/crowd.h:cap#2") == {}


def test_other_coney_only_markers_are_not_limits() -> None:
    text = "/// @coney-only trace bookkeeping; the original keeps no such count\nint targetChanges();\n"
    assert marker_keys(text) == []
    assert marker_keys("/// Mentions the limit: marker in prose only.\nint x();\n") == []


def test_the_sources_are_read_from_the_configured_folders_and_endings(tmp_path: Path) -> None:
    write(tmp_path, "src/core/crowd.h", FUNCTION)
    write(tmp_path, "src/ai/gangs.cpp", FUNCTION.replace("scanTokens", "gangSlots"))
    write(tmp_path, "src/core/notes.md", FUNCTION)
    write(tmp_path, "tests/crowd_test.cpp", FUNCTION)
    found = docs_beyond.scan_markers(tmp_path, ("src/",), (".h", ".cpp"), MARKER)
    assert [marker.key for marker in found] == ["src/ai/gangs.cpp:gangSlots", "src/core/crowd.h:scanTokens"]


# --- the page -------------------------------------------------------------------------------------------------------

HEADER = "| Difference | The original | Coney | Switch and test mode | Status | Details |"
SECTION_TITLES = {
    "lifted-limits": "Lifted limits",
    "picture-and-sound": "Picture and sound",
    "xbox-native": "Xbox-native features",
    "multiplayer": "Multiplayer",
    "mods-and-studio": "Mods and Studio",
    "debug-and-player-tools": "Debug and player tools",
}


def row(name: str, status: str = "on main", marker: str | None = None) -> str:
    """One table row: `marker` is the extra cell of the lifted limits table (None for the other tables)."""
    cells = [name, "the original", "Coney", "the option; test mode keeps it", status, "[Page](x.md)"]
    if marker is not None:
        cells.append(marker)
    return "| " + " | ".join(cells) + " |"


def page(limits: tuple[str, ...] = (), *, others: dict[str, tuple[str, ...]] | None = None, drop: str = "") -> str:
    """A page with the six sections and one table each: `limits` are the rows of the lifted limits table (with a
    Marker cell), `others` the rows by section anchor; `drop` leaves one section out."""
    text = "# Beyond the original\n\nIntroduction.\n\n"
    for anchor, title in SECTION_TITLES.items():
        if anchor == drop:
            continue
        text += f"## {title} {{#{anchor}}}\n\nWhat this section holds.\n\n"
        if anchor == "lifted-limits":
            text += HEADER + " Marker |\n| --- | --- | --- | --- | --- | --- | --- |\n"
            text += "".join(f"{line}\n" for line in limits) + "\n"
        else:
            text += HEADER + "\n| --- | --- | --- | --- | --- | --- |\n"
            text += "".join(f"{line}\n" for line in (others or {}).get(anchor, ())) + "\n"
    return text


def check(text: str | None, *keys: str) -> dict[str, list[docs_check.Finding]]:
    """The findings for the page `text` and the markers `keys` (`path:Symbol`) in the sources."""
    markers = [Marker(key.rsplit(":", 1)[0], 10, key.rsplit(":", 1)[1]) for key in keys]
    return docs_beyond.check(PAGE, text, markers, BEYOND)


def messages(found: dict[str, list[docs_check.Finding]]) -> list[str]:
    """Every message, whatever the file."""
    return [finding.message for findings in found.values() for finding in findings]


def test_a_page_with_an_entry_for_each_marker_passes() -> None:
    text = page((row("Scan tokens", marker="`src/core/crowd.h:scanTokens`"),))
    assert check(text, "src/core/crowd.h:scanTokens") == {}


def test_a_page_with_no_markers_and_no_entries_passes() -> None:
    assert check(page()) == {}


def test_a_marker_with_no_entry_fails_at_the_marker() -> None:
    found = check(page(), "src/core/crowd.h:scanTokens")
    assert list(found) == ["src/core/crowd.h"]
    finding = found["src/core/crowd.h"][0]
    assert finding.rule == "beyond" and finding.severity == "error" and finding.line == 10
    assert "scanTokens" in finding.message and "Marker" in finding.message and PAGE in finding.message


def test_every_marker_with_no_entry_is_reported_once() -> None:
    found = check(page(), "src/core/crowd.h:a", "src/core/crowd.h:b", "src/ai/gangs.cpp:c")
    assert sorted(found) == ["src/ai/gangs.cpp", "src/core/crowd.h"]
    assert len(found["src/core/crowd.h"]) == 2


def test_one_row_may_name_several_markers() -> None:
    text = page((row("Lists", marker="`src/core/crowd.h:a`, `src/core/crowd.h:b`"),))
    assert check(text, "src/core/crowd.h:a", "src/core/crowd.h:b") == {}


def test_an_entry_naming_a_marker_that_no_longer_exists_fails_at_its_row() -> None:
    entry = row("Scan tokens", marker="`src/core/crowd.h:scanTokens`")
    text = page((entry,))
    found = check(text)
    assert list(found) == [PAGE]
    finding = found[PAGE][0]
    assert finding.rule == "beyond" and finding.line == text.splitlines().index(entry) + 1
    assert "src/core/crowd.h:scanTokens" in finding.message and "no marker" in finding.message


def test_an_entry_for_a_marker_that_moved_to_another_name_fails_both_ways() -> None:
    text = page((row("Scan tokens", marker="`src/core/crowd.h:scanTokens`"),))
    found = check(text, "src/core/crowd.h:scanBudget")
    assert sorted(found) == [PAGE, "src/core/crowd.h"]


def test_a_planned_entry_names_no_marker_and_a_marker_it_finds_is_not_planned() -> None:
    assert check(page((row("Gangs", "planned, [#1569](issue:1569)", "-"),))) == {}
    text = page((row("Gangs", "planned, [#1569](issue:1569)", "`src/ai/gangs.h:kGangSlots`"),))
    found = check(text, "src/ai/gangs.h:kGangSlots")
    assert any("planned" in message and "marker exists" in message for message in messages(found))


def test_an_entry_on_main_in_the_lifted_limits_names_its_marker() -> None:
    found = check(page((row("Gangs", "on main", "-"),)))
    assert any("on main" in message and "marker" in message for message in messages(found))


def test_a_marker_listed_by_two_entries_fails() -> None:
    text = page((row("A", marker="`src/core/crowd.h:a`"), row("B", marker="`src/core/crowd.h:a`")))
    assert any("twice" in message for message in messages(check(text, "src/core/crowd.h:a")))


def test_a_missing_page_fails_when_the_sources_have_markers_and_not_when_they_have_none() -> None:
    found = check(None, "src/core/crowd.h:scanTokens")
    assert any("missing" in message for message in messages(found))
    assert check(None) == {}


@pytest.mark.parametrize("anchor", list(SECTION_TITLES))
def test_every_section_has_to_be_on_the_page(anchor: str) -> None:
    found = check(page(drop=anchor))
    assert any(anchor in message and "section" in message for message in messages(found))


def test_a_table_needs_every_column() -> None:
    text = page().replace("| Switch and test mode |", "|", 1)
    assert any("Switch and test mode" in message for message in messages(check(text)))


def test_the_lifted_limits_table_needs_its_marker_column() -> None:
    text = page().replace(" Marker |", "", 1)
    assert any("Marker" in message and "column" in message for message in messages(check(text)))


def test_a_status_starts_with_on_main_or_planned() -> None:
    text = page(others={"picture-and-sound": (row("Mips", "someday"),)})
    found = check(text)
    assert any("someday" in message and "on main" in message for message in messages(found))
    assert check(page(others={"picture-and-sound": (row("Mips", "planned, [#1](issue:1)"), row("Fog"))})) == {}


def test_a_section_without_a_table_fails() -> None:
    text = page().replace(HEADER + "\n| --- | --- | --- | --- | --- | --- |\n", "", 1)
    assert any("table" in message for message in messages(check(text)))


def test_a_row_with_too_few_cells_fails() -> None:
    text = page(others={"multiplayer": ("| Co-op | the original's | Coney's |",)})
    assert any("cells" in message for message in messages(check(text)))


def test_a_pipe_inside_a_code_span_does_not_split_a_cell() -> None:
    cells = docs_beyond.split_row("| `a | b` | two |")
    assert cells == ["`a | b`", "two"]


# --- the check on a whole change ---------------------------------------------------------------------------------


#: The engine section's landing page, which the check of an engine page needs beside it.
INDEX = "docs/engine/index.md"
INDEX_TEXT = "# Engine\n"


def test_a_change_that_adds_a_lift_without_its_entry_fails_docs_check(tmp_path: Path) -> None:
    root = checkout(tmp_path, files={PAGE: page(), INDEX: INDEX_TEXT})
    write(root, "src/core/crowd.h", FUNCTION)
    result = docs_check.check_changed(root, "main", docs_check.context_for(root))
    assert list(result.failed) == ["src/core/crowd.h"]
    assert result.failed["src/core/crowd.h"][0].rule == "beyond"


def test_a_change_that_adds_the_entry_with_the_lift_passes(tmp_path: Path) -> None:
    root = checkout(tmp_path, files={PAGE: page(), INDEX: INDEX_TEXT})
    write(root, "src/core/crowd.h", FUNCTION)
    write(root, PAGE, page((row("Scan tokens", marker="`src/core/crowd.h:scanTokens`"),)))
    result = docs_check.check_changed(root, "main", docs_check.context_for(root))
    assert result.failed == {}


def test_a_change_that_deletes_a_lift_leaves_its_entry_to_fail(tmp_path: Path) -> None:
    entry = row("Scan tokens", marker="`src/core/crowd.h:scanTokens`")
    root = checkout(tmp_path, files={PAGE: page((entry,)), "src/core/crowd.h": FUNCTION, INDEX: INDEX_TEXT})
    (root / "src/core/crowd.h").unlink()
    result = docs_check.check_changed(root, "main", docs_check.context_for(root))
    assert list(result.failed) == [PAGE]


def test_a_page_that_is_changed_and_fails_is_listed_once(tmp_path: Path) -> None:
    entry = row("Scan tokens", marker="`src/core/crowd.h:scanTokens`")
    root = checkout(tmp_path, files={PAGE: page((entry,)), INDEX: INDEX_TEXT})
    write(root, PAGE, page((entry,), others={"multiplayer": (row("Co-op"),)}))
    result = docs_check.check_changed(root, "main", docs_check.context_for(root))
    assert result.pages.count(PAGE) == 1 and list(result.failed) == [PAGE]


def test_a_page_named_on_the_command_line_is_held_to_the_sources(tmp_path: Path) -> None:
    root = checkout(tmp_path, files={PAGE: page(), "src/core/crowd.h": FUNCTION, INDEX: INDEX_TEXT})
    result = docs_check.check_paths(root, [PAGE], docs_check.context_for(root))
    assert "src/core/crowd.h" in result.failed


def test_a_checkout_without_the_page_or_markers_is_left_alone(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, "docs/guides/new.md", "# New\n\nHello.\n")
    assert docs_check.check_changed(root, "main", docs_check.context_for(root)).failed == {}


# --- the conventions ---------------------------------------------------------------------------------------------


def test_the_real_section_names_the_page_and_the_marker() -> None:
    assert BEYOND.page == "docs/engine/beyond-the-original.md"
    assert BEYOND.marker == "@coney-only limit:"
    assert BEYOND.limits_section in BEYOND.sections and BEYOND.status_column in BEYOND.columns
    assert isinstance(BEYOND, BeyondPage)


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("standards.beyond.page", "../beyond.md", "a Markdown page inside the checkout"),
        ("standards.beyond.page", "docs/engine/beyond.txt", "a Markdown page inside the checkout"),
        ("standards.beyond.limits_section", "elsewhere", "not one of the sections"),
        ("standards.beyond.status_column", "Result", "not one of the columns"),
        ("standards.beyond.marker_column", "Status", "an extra column"),
        ("standards.beyond.statuses", [], "expected a non-empty list"),
        ("standards.beyond.marker", REMOVE, "missing the key 'marker'"),
        ("standards.beyond.unknown", 1, "unknown key 'unknown'"),
    ],
)
def test_a_wrong_beyond_section_is_refused(path: str, value: object, message: str) -> None:
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(changed((path, value)))
