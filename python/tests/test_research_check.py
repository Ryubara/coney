# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.research_check: the research page standard of docs/guides/writing-docs.md, on made-up pages.

Each rule is shown failing on the smallest page that breaks it and passing on the nearest page that keeps it. The
executable is made up (`research_support`), and so is every page.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from research_support import (
    FUNCTIONS,
    IN_DATA,
    IN_FUNCTION,
    NOWHERE,
    RESEARCH_PATH,
    env,
    errors,
    findings,
    front,
    page,
    research,
    rules_of,
)

from coney_tools import research_check
from coney_tools.docs_check import Context, check_page
from coney_tools.docs_page import Finding
from coney_tools.research_check import EXTRA_RULES, Subject
from coney_tools.research_evidence import cites, levels_in

# --- a page that meets the standard -----------------------------------------------------------------------------------


@pytest.mark.parametrize("kind", ["system", "topic", "format", "mission", "feature", "survey"])
def test_a_complete_page_of_each_kind_passes(kind: str) -> None:
    text = page(kind=kind)
    assert findings(RESEARCH_PATH, text) == [], kind


def test_without_research_data_a_research_page_is_held_to_the_style_rules_only() -> None:
    assert check_page(RESEARCH_PATH, "# Widget\n\nNothing at all.\n", Context()) == []


# --- front matter -----------------------------------------------------------------------------------------------------


def test_a_page_without_front_matter_is_reported_once() -> None:
    text = page(meta="")
    assert errors(text) == ["front-matter"]


def test_a_page_without_front_matter_is_read_as_the_kind_of_its_folder() -> None:
    topic = page(kind="format", meta="").replace("## Layout", "## Data")
    assert errors(topic, path="docs/research/formats/wad.md") == ["front-matter"]
    assert "skeleton" in errors(topic, path="docs/research/other.md")


@pytest.mark.parametrize(
    ("meta", "message"),
    [
        ("---\nkind: topc\nsystem: widget\nowns: {functions: ['0x00123400']}\n---\n", "'kind' must be one of"),
        ("---\nsystem: widget\nowns: {functions: ['0x00123400']}\n---\n", "'kind' must be one of"),
        ("---\nkind: [a\n---\n", "not valid YAML"),
        ("---\n- a\n---\n", "must be a mapping"),
        ("---\nkind: topic\nsystem: widget\ncolour: red\nowns: {functions: ['0x00123400']}\n---\n", "unknown front"),
        ("---\nkind: topic\nowns: {functions: ['0x00123400']}\n---\n", "names its 'system'"),
        ("---\nkind: format\nsystem: widget\n---\n", "belongs to no system"),
    ],
)
def test_front_matter_faults_are_reported(meta: str, message: str) -> None:
    found = findings(RESEARCH_PATH, page(meta=meta))
    assert [item.message for item in found if item.rule == "front-matter" and message in item.message], found


def test_a_page_that_owns_addresses_says_so_and_a_page_that_owns_none_does_not() -> None:
    assert errors(page(meta=front("topic", owns=""))) == ["owns"]
    assert errors(page(kind="mission", meta=front("mission", owns="{files: [a.cpp]}"))) == ["owns"]
    assert errors(page(kind="survey", meta=front("survey", owns="{files: [a.cpp]}"))) == []


@pytest.mark.parametrize(
    ("owns", "message"),
    [
        ("{ranges: ['0x00123400-0x00123500']}", None),
        ("{ranges: ['0x00123500-0x00123400']}", "ends before it starts"),
        ("{ranges: ['0x123400-0x123500']}", "eight digits"),
        ("{ranges: ['0x00123400-0x09000000']}", "not inside the executable"),
        ("{functions: ['0x00123401']}", "no function entry"),
        ("{functions: ['0x123400']}", "eight digits"),
        ("{functions: ['0x00123400', '0x00123500']}", None),
        ("{files: ['Human/cnsplayertag.cpp']}", None),
        ("{files: [5]}", "list of strings"),
        ("{symbols: [a]}", "unknown 'owns' key"),
        ("[a]", "must be a mapping"),
    ],
)
def test_owns_is_read_and_resolved(owns: str, message: str | None) -> None:
    found = findings(RESEARCH_PATH, page(meta=front("topic", owns=owns)))
    owns_messages = [item.message for item in found if item.rule == "owns"]
    if message is None:
        assert owns_messages == []
    else:
        assert any(message in text for text in owns_messages), owns_messages


def other(owns: str, kind: str = "survey") -> str:
    """Another page of the corpus with the given `owns` (a survey page by default: it needs no system page)."""
    meta = front(kind, owns=owns) if kind == "survey" else front(kind, owns=owns, system="widget")
    return page(kind=kind, meta=meta)


def mine(owns: str) -> str:
    """The page under test: a survey page that owns `owns`."""
    return page(kind="survey", meta=front("survey", owns=owns))


def test_two_pages_cannot_own_the_same_function_file_or_range() -> None:
    for owns in ("{functions: ['0x00123400']}", "{files: ['A/B.cpp']}", "{ranges: ['0x00123400-0x00123500']}"):
        pages = {"docs/research/other.md": other(owns)}
        found = findings(RESEARCH_PATH, mine(owns), env(pages))
        assert rules_of(found) == ["owns"], owns
        assert "docs/research/other.md" in found[0].message


def test_a_file_name_is_compared_without_regard_to_case() -> None:
    pages = {"docs/research/other.md": other("{files: ['Human/Thing.cpp']}")}
    assert errors(mine("{files: ['human/thing.cpp']}"), env(pages)) == ["owns"]


def test_a_page_may_take_a_function_or_a_smaller_range_out_of_a_range_another_page_owns() -> None:
    system = other("{ranges: ['0x00123000-0x00124000']}", "system")
    pages = {"docs/research/widget/index.md": system}
    assert errors(page(meta=front("topic", owns="{functions: ['0x00123400']}")), env(pages)) == []
    inner = page(meta=front("topic", owns="{ranges: ['0x00123400-0x00123500']}"))
    assert errors(inner, env(pages)) == []


def test_two_ranges_that_cross_are_refused() -> None:
    pages = {"docs/research/other.md": other("{ranges: ['0x00123400-0x00123480']}")}
    assert errors(mine("{ranges: ['0x00123440-0x00123500']}"), env(pages)) == ["owns"]


def test_a_system_has_one_page_and_a_topic_has_a_system() -> None:
    system = other("{functions: ['0x00200000']}", "system")
    pages = {"docs/research/widget/index.md": system}
    again = page(kind="system", meta=front("system", owns="{functions: ['0x00123400']}"))
    assert errors(again, env(pages)) == ["front-matter"]
    assert errors(page(), env(pages)) == []
    stray = page(meta=front("topic", system="gadget"))
    assert errors(stray, env(pages)) == ["front-matter"]


def test_a_topic_in_an_empty_corpus_is_not_refused_for_want_of_a_system() -> None:
    assert errors(page(), env()) == []


# --- sections ---------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("kind", ["system", "topic", "format", "mission", "feature", "survey"])
def test_each_required_section_is_named_when_missing(kind: str) -> None:
    for name in research().kinds[kind].required:
        found = findings(RESEARCH_PATH, page(kind=kind, drop=(name,)))
        assert rules_of(found) == ["skeleton"], (kind, name)
        assert f"'{name}'" in found[0].message


def test_the_optional_sections_may_be_left_out_and_may_be_there() -> None:
    assert errors(page(sections={"How this was checked": "Read in PCSX2.\n"})) == []
    assert errors(page(kind="format", sections={"Behaviour": "How the game uses it.\n"})) == []


def test_a_section_the_kind_does_not_have_is_refused() -> None:
    found = findings(RESEARCH_PATH, page(extra="## Background\n\nWhere it came from.\n\n"))
    assert rules_of(found) == ["skeleton"]
    assert "'Background'" in found[0].message and "subsection" in found[0].message
    # A mission page has no Data section, a topic has no Flow.
    assert errors(page(kind="mission", extra="## Data\n\nFields.\n\n")) == ["skeleton"]


def test_the_sections_must_be_in_order() -> None:
    text = page().replace("## Data", "## Temp").replace("## Behaviour", "## Data").replace("## Temp", "## Behaviour")
    found = findings(RESEARCH_PATH, text)
    assert rules_of(found) == ["skeleton"] and "out of order" in found[0].message


def test_an_empty_section_is_reported() -> None:
    found = findings(RESEARCH_PATH, page(sections={"Open questions": ""}))
    assert rules_of(found) == ["skeleton"]
    assert "Open questions" in found[0].message and "one line" in found[0].message


def test_a_section_with_only_subsections_is_not_empty() -> None:
    assert errors(page(sections={"Data": "### The header\n\nFour bytes.\n"})) == []


def test_a_section_written_twice_is_reported() -> None:
    assert errors(page(extra="## Data\n\nAgain.\n\n")) == ["skeleton", "skeleton"]


@pytest.mark.parametrize(
    ("old", "new"),
    [("Purpose", "Summary"), ("Original structure", "Map"), ("Coney's implementation", "In Coney")],
)
def test_the_old_section_names_are_accepted(old: str, new: str) -> None:
    assert errors(page().replace(f"## {new}", f"## {old}")) == []


def test_an_old_name_for_a_section_the_kind_lacks_is_refused() -> None:
    assert errors(page(kind="mission").replace("## Summary", "## Original structure")) == ["skeleton", "skeleton"]


def test_data_is_the_layout_of_a_format_page() -> None:
    assert errors(page(kind="format").replace("## Layout", "## Data")) == []
    assert errors(page(kind="topic").replace("## Data", "## Layout")) == ["skeleton", "skeleton"]


def test_a_heading_with_more_words_than_the_section_name_is_not_the_section() -> None:
    assert errors(page().replace("## Data", "## Data layout")) == ["skeleton", "skeleton"]


def test_a_heading_anchor_does_not_hide_a_section() -> None:
    assert errors(page().replace("## Data", "## Data {#data}")) == []


def test_a_heading_inside_a_code_block_is_not_a_section() -> None:
    assert errors(page(drop=("Data",), extra="```\n## Data\n```\n\n")) == ["skeleton"]


def test_a_title_is_required_and_there_is_only_one() -> None:
    assert errors(page().replace("# Widget\n", "")) == ["skeleton"]
    assert errors(page(extra="# Another\n\n")) == ["skeleton"]


# --- the Verified against line ----------------------------------------------------------------------------------------


def test_the_verified_against_line_is_required_and_says_something() -> None:
    assert errors(page(head="")) == ["verified"]
    assert errors(page(head="Verified against:\n")) == ["verified"]


def test_a_verified_against_line_in_bold_or_lower_case_counts() -> None:
    assert errors(page(head="**Verified against:** the Xbox executable.\n")) == []
    assert errors(page(head="verified against `SLUS_212.15`.\n")) == []


def test_a_verified_against_line_below_the_first_section_does_not_count() -> None:
    assert errors(page(head="") + "\nVerified against: `SLUS_212.15`.\n") == ["verified"]


def test_the_verified_against_line_is_one_line() -> None:
    wrapped = "Verified against: `SLUS_212.15` (NTSC-U);\nruntime claims with PCSX2 2.9.\n"
    found = findings(RESEARCH_PATH, page(head=wrapped))
    assert rules_of(found) == ["verified"] and "one line" in found[0].message


def test_the_verified_against_line_is_short() -> None:
    limit = research().verified_max_chars
    assert errors(page(head="Verified against: " + "x" * (limit - 18) + "\n")) == []
    found = findings(RESEARCH_PATH, page(head="Verified against: " + "x" * (limit - 17) + "\n"))
    assert rules_of(found) == ["verified"] and str(limit) in found[0].message


# --- evidence levels: where the level sits ----------------------------------------------------------------------------


def test_an_address_with_no_level_is_reported_on_its_line() -> None:
    text = page(f"The widget spins at {IN_FUNCTION}.\n")
    found = findings(RESEARCH_PATH, text)
    assert rules_of(found) == ["evidence"] and IN_FUNCTION in found[0].message
    assert text.splitlines()[found[0].line - 1].startswith("The widget spins")


@pytest.mark.parametrize(
    "level",
    [
        "confirmed (code)",
        "Confirmed (code)",
        "inferred from the vtable layout",
        "speculative ([q-spin](#q-spin))",
        "not needed: debug stub",
    ],
)
def test_each_level_in_the_block_covers_the_claim(level: str) -> None:
    text = page(
        f"The widget spins at {IN_FUNCTION} ({level}).\n", sections={"Open questions": "- **Spin** {#q-spin}: x\n"}
    )
    assert errors(text) == []


def test_the_level_in_the_neighbouring_block_does_not_cover_the_claim() -> None:
    before = f"All of this is confirmed (code).\n\nThe widget spins at {IN_FUNCTION}.\n"
    after = f"The widget spins at {IN_FUNCTION}.\n\nThe above is inferred.\n"
    assert errors(page(before)) == ["evidence"]
    assert errors(page(after)) == ["evidence"]


def test_a_page_wide_sentence_does_not_grade_the_claims() -> None:
    sentence = "Every claim here is confirmed (code) at the cited address unless it says otherwise.\n"
    assert errors(page(f"The widget at {IN_FUNCTION}.\n", extra=sentence + "\n")) == ["evidence"]


def test_a_list_item_is_a_claim_of_its_own() -> None:
    items = f"- Spins at {IN_FUNCTION} (inferred from its name).\n- Stops at 0x00123460.\n- Waits.\n"
    found = findings(RESEARCH_PATH, page(items))
    assert rules_of(found) == ["evidence"] and "0x00123460" in found[0].message


def test_a_lead_line_under_a_subheading_covers_the_blocks_under_it() -> None:
    lead = (
        f"### The spin\n\nConfirmed (code) at the cited addresses unless a line says otherwise.\n\n"
        f"It spins at {IN_FUNCTION}.\n\nIt stops at 0x00123460.\n"
    )
    assert errors(page(lead)) == []


def test_a_lead_line_covers_its_own_heading_only() -> None:
    lead = (
        "### The spin\n\nConfirmed (code) at the cited addresses.\n\nIt spins.\n\n"
        f"### The stop\n\nIt stops at {IN_FUNCTION}.\n"
    )
    assert errors(page(lead)) == ["evidence"]
    deeper = f"### The spin\n\nConfirmed (code) at the cited addresses.\n\n#### Detail\n\nIt spins at {IN_FUNCTION}.\n"
    assert errors(page(deeper)) == ["evidence"]


def test_a_lead_line_under_a_section_heading_covers_nothing() -> None:
    text = page(f"Confirmed (code) at the cited addresses.\n\nIt spins at {IN_FUNCTION}.\n")
    assert errors(text) == ["evidence"]


def test_a_lead_line_must_come_first_and_cite_no_address() -> None:
    late = f"### The spin\n\nIt spins.\n\nConfirmed (code) here.\n\nIt stops at {IN_FUNCTION}.\n"
    assert errors(page(late)) == ["evidence"]
    cited = f"### The spin\n\nConfirmed (code) at {IN_FUNCTION}.\n\nIt stops at 0x00123460.\n"
    assert errors(page(cited)) == ["evidence"]


def test_the_level_in_the_heading_does_not_grade_the_section() -> None:
    text = page(f"### The spin (confirmed (code))\n\nIt spins at {IN_FUNCTION}.\n")
    assert errors(text) == ["evidence"]


def test_a_table_with_an_evidence_column_needs_a_level_in_each_row() -> None:
    table = (
        "| Address | Name | Evidence |\n| --- | --- | --- |\n"
        f"| {IN_FUNCTION} | Spin | confirmed (code) |\n| 0x00123460 | Stop | |\n"
    )
    found = findings(RESEARCH_PATH, page(table))
    assert rules_of(found) == ["evidence"] and "0x00123460" in found[0].message
    assert "Evidence cell" in found[0].message


def test_the_level_must_be_in_the_evidence_cell_not_elsewhere_in_the_row() -> None:
    table = f"| Name | Evidence |\n| --- | --- |\n| inferred {IN_FUNCTION} | |\n"
    assert errors(page(table)) == ["evidence"]


def test_a_lead_line_does_not_grade_a_row_of_a_table_with_an_evidence_column() -> None:
    table = f"### Spin\n\nConfirmed (code) here.\n\n| Address | Evidence |\n| --- | --- |\n| {IN_FUNCTION} | |\n"
    assert errors(page(table)) == ["evidence"]


def test_a_table_without_an_evidence_column_is_graded_in_each_row_or_by_a_lead_line() -> None:
    table = f"| Address | Name |\n| --- | --- |\n| {IN_FUNCTION} | Spin |\n| 0x00123460 | Stop (inferred) |\n"
    found = findings(RESEARCH_PATH, page(table))
    assert rules_of(found) == ["evidence"] and IN_FUNCTION in found[0].message
    lead = "### The table\n\nConfirmed (code) for every row.\n\n" + table
    assert errors(page(lead)) == []


def test_a_map_table_with_no_level_column_grades_nothing() -> None:
    table = (
        f"| Behaviour | Function | Section |\n| --- | --- | --- |\n| Spin | `{IN_FUNCTION}` `Widget_Spin` | [x](#x) |\n"
    )
    assert errors(page(sections={"Map": table})) == []
    graded = f"| Function | Evidence |\n| --- | --- |\n| `{IN_FUNCTION}` | |\n"
    assert errors(page(sections={"Map": graded})) == ["evidence"]
    assert errors(page(sections={"Map": f"Entry at {IN_FUNCTION}.\n"})) == ["evidence"]


def test_a_level_column_must_be_headed_evidence_or_confidence() -> None:
    game_levels = f"| Level | Address |\n| --- | --- |\n| level80 | {IN_FUNCTION} |\n"
    graded = f"| Address | Evidence level |\n| --- | --- |\n| {IN_FUNCTION} | |\n"
    assert errors(page(game_levels + "\nAll inferred.\n")) == ["evidence"]
    assert errors(page(graded)) == ["evidence"]


def test_the_ungraded_sections_need_no_level() -> None:
    text = page(
        sections={
            "Summary": f"It starts at {IN_FUNCTION}.\n",
            "In Coney": f"`@orig {IN_FUNCTION} Widget_Spin` in `src/widget/`.\n",
            "Open questions": f"What is {IN_FUNCTION} for?\n",
            "How this was checked": f"Break at {IN_FUNCTION} in PCSX2.\n",
        }
    )
    assert errors(text) == []


def test_an_address_in_a_code_block_or_above_the_first_section_needs_no_level() -> None:
    assert errors(page(f"```text\n{IN_FUNCTION} jal Widget_Spin\n```\n")) == []
    assert errors(page(extra=f"Starts at {IN_FUNCTION}.\n\n")) == []


def test_short_numbers_and_offsets_are_not_addresses() -> None:
    assert errors(page("The type is at `+0xd0`, mask 0xff, flag 0x1000, mask 0x123500.\n")) == []


def test_only_addresses_in_main_memory_are_claims() -> None:
    for number in ("0x7e23a6f2", "0x18300000", "0x80000000", "0x00001000"):
        assert errors(page(f"The value {number}.\n")) == [], number


def test_a_script_locator_is_cited_like_an_address() -> None:
    assert errors(page(kind="mission", behaviour="Checkpoint 3 is set by `level99.lua:f31`.\n")) == ["evidence"]
    graded = "Checkpoint 3 is set by `level99.lua:f31` (confirmed (code)).\n"
    assert errors(page(kind="mission", behaviour=graded)) == []


def test_a_row_of_a_mission_flow_table_is_graded_in_its_evidence_cell() -> None:
    table = "| Step | Trigger | Evidence |\n| --- | --- | --- |\n| 2 | `level99.lua:f31` sets it | confirmed (code) |\n"
    assert errors(page(kind="mission", behaviour=table)) == []


# --- evidence levels: how they are spelled ----------------------------------------------------------------------------


@pytest.mark.parametrize(
    "spelling",
    [
        "confirmed (disc)",
        "confirmed (name hash)",
        "confirmed (runtime, coney)",
        "Confirmed (disc data)",
        "Confidence: confirmed from code",
    ],
)
def test_a_spelling_that_is_no_level_is_refused(spelling: str) -> None:
    found = findings(RESEARCH_PATH, page(f"The widget spins at {IN_FUNCTION} ({spelling}).\n"))
    assert rules_of(found).count("evidence") >= 1 and "no evidence level" in found[0].message


def test_a_refused_spelling_does_not_grade_the_claim() -> None:
    found = findings(RESEARCH_PATH, page(f"It spins at {IN_FUNCTION} (confirmed (disc)).\n"))
    assert [item.message.split()[0] for item in found] == ["'confirmed", IN_FUNCTION]


def test_a_disc_check_beside_a_graded_claim_is_fine() -> None:
    text = page(f"It spins at {IN_FUNCTION} (confirmed (code)). Disc check: the size matches.\n")
    assert errors(text) == []


def test_an_evidence_cell_states_a_level_not_a_hedge() -> None:
    table = f"| Address | Evidence |\n| --- | --- |\n| {IN_FUNCTION} | probably confirmed (code) |\n"
    assert errors(page(table)) == ["evidence"]


def test_confirmed_runtime_cites_the_run_that_observed_it() -> None:
    bare = page(f"It spins at {IN_FUNCTION} (confirmed (runtime)).\n")
    found = findings(RESEARCH_PATH, bare, env(runs=frozenset()))
    assert rules_of(found) == ["evidence"] and "run <id>" in found[0].message
    recorded = env(runs=frozenset({"r-20261023-spin"}))
    claim = page(f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-20261023-spin).\n")
    assert errors(claim, recorded) == []


def test_a_run_record_must_exist_once_there_are_run_records() -> None:
    text = page(f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n")
    assert errors(text, env(runs=frozenset({"r-1"}))) == []
    found = findings(RESEARCH_PATH, text, env(runs=frozenset({"r-2"})))
    assert rules_of(found) == ["evidence"] and "research/runs/r-1.toml" in found[0].message


def test_a_legacy_page_may_keep_its_old_runtime_claims_when_it_names_the_emulator() -> None:
    old = f"It spins at {IN_FUNCTION} (confirmed (runtime), PCSX2 2.9.94, a watch on the field).\n"
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1}})
    head = "Verified against: `SLUS_212.15`; PCSX2 2.9.94.\n"
    assert errors(page(old, head=head, meta=""), listed) == []
    # The same claim on a page that is not legacy needs the run record, and a legacy page must say where PCSX2 is named.
    assert errors(page(old, head=head)) == ["evidence"]
    assert errors(page(old, meta=""), listed) == ["evidence"]


def test_speculative_links_the_question_that_says_how_to_test_it() -> None:
    bare = page(f"It might spin at {IN_FUNCTION} (speculative).\n")
    found = findings(RESEARCH_PATH, bare)
    assert rules_of(found) == ["evidence"] and "open question" in found[0].message
    linked = page(
        f"It might spin at {IN_FUNCTION} (speculative, [q-spin](#q-spin)).\n",
        sections={"Open questions": "- **Does it spin?** {#q-spin}: watch the field.\n"},
    )
    assert errors(linked) == []
    missing = page(f"It might spin at {IN_FUNCTION} (speculative, [q-spin](#q-spin)).\n")
    found = findings(RESEARCH_PATH, missing)
    assert rules_of(found) == ["evidence"] and "{#q-spin}" in found[0].message


def test_a_speculative_link_to_another_page_is_taken_on_trust() -> None:
    text = page(f"It might spin at {IN_FUNCTION} (speculative, [q-spin](other.md#q-spin)).\n")
    assert errors(text) == []


def test_a_bare_inferred_is_a_warning_and_a_basis_silences_it() -> None:
    found = findings(RESEARCH_PATH, page(f"It spins at {IN_FUNCTION} (inferred).\n"))
    assert rules_of(found) == [] and rules_of(found, "warning") == ["evidence"]
    assert findings(RESEARCH_PATH, page(f"It spins at {IN_FUNCTION}, inferred from the call order.\n")) == []
    assert findings(RESEARCH_PATH, page(f"It spins at {IN_FUNCTION}, inferred because nothing else writes it.\n")) == []


def test_levels_in_reads_each_spelling() -> None:
    assert levels_in("confirmed  (CODE)").kinds == {"code"}
    both = levels_in("confirmed (code); role inferred")
    assert both.kinds == {"code", "inferred"}
    runtime = levels_in("confirmed (runtime), run r-1 and confirmed (runtime)")
    assert runtime.runs == ("r-1",) and runtime.bare_runtime
    assert levels_in("confirmed (disc)").refused == ("confirmed (disc)",)
    assert levels_in("a confirmed bug").kinds == frozenset()


def test_cites_finds_addresses_and_locators_only() -> None:
    assert cites(f"at {IN_FUNCTION}") is not None and cites("`level99.lua:f2`") is not None
    assert cites("+0xb4 and 0xff and 0x149160") is None


# --- addresses --------------------------------------------------------------------------------------------------------


def test_a_short_form_of_a_function_entry_is_refused() -> None:
    found = findings(RESEARCH_PATH, page(sections={"Summary": "The function at 0x123400 spins.\n"}))
    assert rules_of(found) == ["address-form"] and "0x00123400" in found[0].message


def test_a_short_number_that_is_no_named_function_entry_is_a_mask_or_an_offset() -> None:
    assert errors(page(sections={"Summary": "Mask 0x123500, field 0x123456 and `+0x123400`.\n"})) == []


def test_an_address_must_resolve() -> None:
    summary = {"Summary": f"A function at {NOWHERE}.\n"}
    found = findings(RESEARCH_PATH, page(sections=summary))
    assert rules_of(found) == ["address-resolve"] and NOWHERE in found[0].message
    assert errors(page(sections={"Summary": f"A function at {IN_FUNCTION} and a global at {IN_DATA}.\n"})) == []


def test_the_first_byte_of_a_function_and_an_unnamed_function_resolve() -> None:
    assert errors(page(sections={"Summary": "Entries 0x00123400 and 0x00123500, 0x0012357f.\n"})) == []
    assert errors(page(sections={"Summary": "Past the end: 0x00123580.\n"})) == ["address-resolve"]


def test_an_address_in_the_code_section_outside_any_function_is_a_miss_but_in_data_it_is_not() -> None:
    assert errors(page(sections={"Summary": "At 0x00150000.\n"})) == ["address-resolve"]
    assert errors(page(sections={"Summary": "At 0x00350000 and 0x00450000.\n"})) == []


def test_an_address_beyond_the_image_is_a_miss() -> None:
    assert errors(page(sections={"Summary": "On the heap at 0x01800000.\n"})) == ["address-resolve"]


def test_the_end_of_a_range_need_not_be_a_function() -> None:
    summary = "The range `0x00123400`-`0x00123580`, or 0x00123400 to 0x00400000.\n"
    assert errors(page(sections={"Summary": summary})) == []


def test_an_address_is_reported_once_per_page() -> None:
    summary = f"At {NOWHERE} and again at {NOWHERE}.\n"
    assert errors(page(sections={"Summary": summary})) == ["address-resolve"]


def test_a_name_that_is_not_ghidras_is_a_warning() -> None:
    wrong = page(sections={"Summary": "Called `0x00123400` `Widget_Turn`.\n"})
    found = findings(RESEARCH_PATH, wrong)
    assert rules_of(found) == [] and rules_of(found, "warning") == ["address-name"]
    assert "Widget_Spin" in found[0].message
    after = page(sections={"Summary": "`Widget_Turn` (`0x00123400`) spins.\n"})
    assert rules_of(findings(RESEARCH_PATH, after), "warning") == ["address-name"]


def test_a_matching_name_or_a_name_for_an_unnamed_function_is_fine() -> None:
    summary = "`0x00123400` `Widget_Spin`, `Widget::Widget_Spin` (`0x00123400`) and `0x00123500` `AnyName`.\n"
    assert findings(RESEARCH_PATH, page(sections={"Summary": summary})) == []


def test_without_a_function_listing_no_address_is_held_to_it() -> None:
    summary = {"Summary": f"At {NOWHERE} and 0x123400.\n"}
    assert errors(page(sections=summary), env(with_catalog=False)) == []


# --- copies -----------------------------------------------------------------------------------------------------------

LONG = (
    "The game state is held in one object that every task reads on each update, and it is written only by the "
    "game mode, which sets the flags before any task runs, so that a task sees one frame's state throughout."
)


def survey(summary: str) -> str:
    """A survey page (no system, no owner) whose Summary is `summary`."""
    return page(kind="survey", sections={"Summary": summary})


def test_a_long_paragraph_on_two_pages_is_a_copy() -> None:
    assert len(LONG) >= research().copy_min_chars
    pages = {"docs/research/other.md": survey(LONG + "\n")}
    found = findings(RESEARCH_PATH, survey(LONG + "\n"), env(pages))
    assert rules_of(found) == ["copied"] and "docs/research/other.md" in found[0].message


def test_a_copy_is_found_through_other_whitespace_and_a_list_marker() -> None:
    pages = {"docs/research/other.md": survey("- " + LONG + "\n")}
    wrapped = LONG.replace(", and", ",\nand").replace("one object", "one\nobject")
    assert errors(survey(wrapped + "\n"), env(pages)) == ["copied"]


def test_a_short_paragraph_or_a_paragraph_twice_on_one_page_is_not_a_copy() -> None:
    short = "A short sentence that two pages share.\n"
    pages = {"docs/research/other.md": survey(short)}
    assert errors(survey(short), env(pages)) == []
    twice = survey(LONG + "\n\n" + LONG + "\n")
    assert errors(twice, env({RESEARCH_PATH: twice})) == []


def test_a_table_row_is_not_a_paragraph() -> None:
    row = "| " + LONG + " | x |\n"
    pages = {"docs/research/other.md": survey(row)}
    assert errors(survey(row), env(pages)) == []


# --- dates and issue numbers ------------------------------------------------------------------------------------------


def test_a_date_or_an_issue_number_outside_in_coney_is_refused() -> None:
    for text in ("Checked on 2026-10-12.", "Fixed in #84.", "See issue #84.", "See pull request 84."):
        assert errors(page(sections={"Summary": text + "\n"})) == ["history"], text


def test_a_date_or_an_issue_number_may_stand_in_coney_and_in_how_this_was_checked() -> None:
    text = page(sections={"In Coney": "Fixed in #84 on 2026-10-12.\n", "How this was checked": "Run on 2026-10-12.\n"})
    assert errors(text) == []
    old = page(sections={"In Coney": "Fixed in #84.\n"}).replace("## In Coney", "## Coney's implementation")
    assert errors(old) == []


def test_a_date_in_code_or_a_link_target_is_not_history() -> None:
    summary = "The file `2026-10-12.csv` and [a page](other.md#12) and `#84`.\n"
    assert errors(page(sections={"Summary": summary})) == []


# --- length -----------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(("kind", "limit"), [("system", 1500), ("topic", 1500), ("format", 1000), ("mission", 1000)])
def test_a_page_over_the_limit_of_its_kind_fails(kind: str, limit: int) -> None:
    assert research().kinds[kind].max_lines == limit
    text = page(kind=kind)
    padding = limit - len(text.replace(front(kind), "").splitlines())
    at_limit = text + "x\n" * padding
    over = text + "x\n" * (padding + 1)
    assert "length" not in rules_of(findings(RESEARCH_PATH, at_limit)), kind
    found = findings(RESEARCH_PATH, over)
    assert "length" in rules_of(found), kind
    assert str(limit + 1) in [item.message.split()[0] for item in found if item.rule == "length"]


def test_the_front_matter_does_not_count_towards_the_length() -> None:
    limit = research().kinds["topic"].max_lines
    text = page()
    padding = limit - len(text.replace(front("topic"), "").splitlines())
    assert "length" not in rules_of(findings(RESEARCH_PATH, text + "x\n" * padding))


# --- the pages that are not research pages ----------------------------------------------------------------------------


@pytest.mark.parametrize(
    "path", ["docs/research/index.md", "docs/research/find/addresses.md", "docs/research/combat/functions.md"]
)
def test_the_landing_page_and_the_generated_pages_are_not_checked_as_research(path: str) -> None:
    assert check_page(path, "no title, not a page of a kind\n", Context(research=env())) == []


def test_the_example_topic_page_of_the_design_passes() -> None:
    text = (
        "---\nkind: topic\nsystem: widget\nowns: {functions: ['0x00123400', '0x00123500']}\n---\n"
        "# Widget: spin, the hold\n\n"
        "Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.\n\n"
        "## Summary\n\nCircle catches the man in front; held, it runs him down.\n\n"
        "## Map\n\n| Behaviour | Function | Section |\n| --- | --- | --- |\n"
        "| Circle: spin | `0x00123400` `Widget_Spin` | [Spin](#spin) |\n\n"
        "Every function: [the list](functions.md).\n\n"
        "## Data\n\n| Field | Meaning | Evidence |\n| --- | --- | --- |\n"
        "| record `+0xb4` | the base id, set by `Widget_Spin` `0x00123400` | confirmed (code) |\n\n"
        "## Behaviour\n\n### The spin {#spin}\n\n`Widget_Spin` (`0x00123400`) plays the anim (confirmed (code)).\n\n"
        "## In Coney\n\n`src/widget.cpp`. Stand-in: a fixed chance ([q-chance](#q-chance)).\n\n"
        "## Open questions\n\n- **The chance** {#q-chance}: read as a roll; find it by a watch.\n"
    )
    found = findings(RESEARCH_PATH, text)
    assert found == [], found
    assert FUNCTIONS[0].name == "Widget_Spin"


# --- pasted decompiles ------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("name", ["param_1", "uVar2", "pcVar3", "local_1c", "DAT_005127e4", "FUN_00123456", "in_v0"])
def test_a_name_ghidra_made_up_is_the_mark_of_a_pasted_decompile(name: str) -> None:
    found = findings(RESEARCH_PATH, page(sections={"Summary": f"The global `{name}` holds it.\n"}))
    assert rules_of(found) == ["decompile"] and name in found[0].message


def test_a_decompile_in_a_code_block_is_found_too() -> None:
    text = page(
        sections={"Summary": "Shown.\n\n```c\nvoid f(int param_1) {\n  iVar1 = *(int *)(param_1 + 8);\n}\n```\n"}
    )
    found = findings(RESEARCH_PATH, text)
    assert [item.rule for item in found] == ["decompile", "decompile"]


def test_names_that_only_look_like_ghidras_are_fine() -> None:
    assert errors(page(sections={"Summary": "The `parameter`, a variable `unit_var` and the local frame.\n"})) == []


def test_a_name_after_a_comma_belongs_to_the_next_address_not_the_last() -> None:
    summary = "The entries `0x00123400`, `Other_Name`, `0x00123500` and `Spin` `0x00123400`.\n"
    assert findings(RESEARCH_PATH, page(sections={"Summary": summary})) == []


def test_the_method_name_alone_stands_for_ghidras_class_and_method() -> None:
    assert (
        findings(RESEARCH_PATH, page(sections={"Summary": "`Spin` `0x00123400` and `Widget::Spin` (`0x00123400`).\n"}))
        == []
    )
    found = findings(RESEARCH_PATH, page(sections={"Summary": "`Turn` `0x00123400`.\n"}))
    assert rules_of(found, "warning") == ["address-name"]


# --- run ids before there are run records -----------------------------------------------------------------------------


@pytest.mark.parametrize("claim", ["confirmed (runtime), run r-1", "confirmed (runtime)"])
def test_a_runtime_claim_cannot_pass_while_there_are_no_run_records(claim: str) -> None:
    # No research/runs/ folder: a run id would name nothing, so it must not pass on its form (T4 creates the folder).
    found = findings(RESEARCH_PATH, page(f"It spins at {IN_FUNCTION} ({claim}).\n"))
    assert rules_of(found) == ["evidence"] and "run records arrive with T4" in found[0].message


def test_a_legacy_page_keeps_its_run_ids_while_there_are_no_run_records() -> None:
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1}})
    text = page(f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n", meta="")
    assert errors(text, listed) == []


def test_once_the_folder_exists_the_record_is_what_counts() -> None:
    text = page(f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n")
    assert errors(text, env(runs=frozenset({"r-1"}))) == []
    found = findings(RESEARCH_PATH, text, env(runs=frozenset()))
    assert "no run record research/runs/r-1.toml" in found[0].message


# --- short forms of globals, and the method section -------------------------------------------------------------------


@pytest.mark.parametrize("short", ["0x410000", "0x310000"])
def test_a_short_form_inside_a_data_section_is_refused(short: str) -> None:
    # `.data` and `.bss` of the made-up executable: a global written without its leading zeros.
    found = findings(RESEARCH_PATH, page(sections={"Summary": f"The flag is the word at {short}.\n"}))
    assert rules_of(found) == ["address-form"] and "0x00" + short[2:] in found[0].message


def test_a_flag_or_a_mask_that_lies_outside_the_data_sections_is_not_an_address() -> None:
    text = "The flags are 0x1000, 0x240000 and the mask 0x7e796f.\n"
    assert errors(page(sections={"Summary": text})) == []


def test_how_this_was_checked_may_name_an_address_that_is_no_global() -> None:
    watched = f"Watched the heap word at {NOWHERE} for ten frames.\n"
    assert errors(page(sections={"How this was checked": watched})) == []
    assert errors(page(sections={"Summary": watched})) == ["address-resolve"]


# --- Ghidra's names in a survey ---------------------------------------------------------------------------------------


def test_a_survey_may_name_ghidras_registers_in_a_code_span() -> None:
    text = survey("Ghidra shows the registers as `in_EAX` and `unaff_ESI` in its output.\n")
    assert errors(text) == []
    assert errors(survey("Ghidra shows the registers as unaff_ESI in its output.\n")) == ["decompile"]
    assert errors(survey("A line reads `uVar2 = param_1 + 4` in the output.\n")) == ["decompile"]
    assert errors(survey("```\nuVar2 = 4;\n```\n")) == ["decompile"]


def test_any_other_page_refuses_a_name_in_a_code_span_too() -> None:
    assert errors(page(sections={"Summary": "The register `unaff_ESI` is read.\n"})) == ["decompile"]


# --- the hook for the later rules -------------------------------------------------------------------------------------


def test_a_later_rule_takes_the_public_subject_of_the_check(monkeypatch: pytest.MonkeyPatch) -> None:
    def extra(subject: Subject) -> list[Finding]:
        return [Finding("owns", 1, f"extra rule saw a {subject.kind} page at {subject.path}")]

    monkeypatch.setattr(research_check, "EXTRA_RULES", (*EXTRA_RULES, extra))
    found = findings(RESEARCH_PATH, page())
    assert [item.message for item in found] == [f"extra rule saw a topic page at {RESEARCH_PATH}"]


def test_the_modules_of_the_check_stay_near_the_size_of_a_file() -> None:
    folder = Path(research_check.__file__).parent
    for name in ("research_check", "research_evidence", "research_subject", "docs_check", "docs_style"):
        lines = len((folder / f"{name}.py").read_text(encoding="utf-8").splitlines())
        assert lines <= 520, f"{name}.py is {lines} lines: split it (docs/guides/conventions.md#file-size)"
