# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for a legacy research page that is moved with git: it keeps its entry's grading exemption (the `evidence`
rule) and nothing else, and a new page can never pass for a moved one (docs/guides/writing-docs.md#legacy-pages)."""

from __future__ import annotations

from pathlib import Path

import pytest
from git_support import git
from research_support import (
    IN_FUNCTION,
    LEGACY_FILE,
    RESEARCH_PATH,
    VERIFIED,
    checkout,
    context,
    env,
    findings,
    page,
    rules_of,
    write,
)

from coney_tools import docs_check, docs_legacy
from coney_tools.docs_legacy import LEGACY_RULES
from coney_tools.research_legacy import Legacy, Touch, format_legacy, parse_legacy

LISTING = "docs/progress/ghidra-functions.tsv"
OLD = "docs/research/widget.md"
NEW = "docs/research/widget/index.md"
#: Prose that is only on the page so that git still sees a rename after the page gains front matter.
FILLER = "".join(
    f"Note {n} of the widget, written long enough to be told apart from every other note.\n\n" for n in range(60)
)
#: An address with no level: the grading finding a legacy page is excused from.
CLAIM = f"The widget spins at {IN_FUNCTION}.\n\n"


def old_text(extra: str = "") -> str:
    """The legacy page: no front matter and an ungraded address."""
    return page(CLAIM + FILLER + extra, meta="")


def new_text(extra: str = "", head: str = VERIFIED, data: str = "Nothing more to say.\n") -> str:
    """The same page restructured to the standard but for the grading: a system page of its own. `extra` ends the
    Behaviour section, which holds the ungraded address, and `data` is the body of the Data section."""
    return page(CLAIM + FILLER + extra, kind="system", head=head, sections={"Data": data})


def listed(root: Path) -> dict[str, dict[str, int]]:
    """The pages of the legacy file of `root`, each with its rules."""
    text = (root / LEGACY_FILE).read_text(encoding="utf-8")
    return {path: dict(rules) for path, rules in parse_legacy(text, LEGACY_RULES).pages.items()}


def moved_checkout(tmp_path: Path, text: str | None = None) -> Path:
    """`OLD` listed under every rule it fails, committed on `main`, with a new branch checked out."""
    root = checkout(tmp_path, files={LISTING: "0x00123400\t256\tWidget_Spin\t1\n", OLD: text or old_text()})
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "legacy")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "move")
    return root


def move(root: Path, text: str, old: str = OLD, new: str = NEW) -> None:
    """Move `old` to `new` with git and give it `text`, staged as a rename."""
    (root / new).parent.mkdir(parents=True, exist_ok=True)
    git(root, "mv", old, new)
    write(root, new, text)
    git(root, "add", "-A")


def follow(root: Path) -> None:
    """Move the legacy entry along with the page, as `docs legacy --prune` does."""
    write(root, LEGACY_FILE, format_legacy(docs_legacy.pruned(root, context(root), "main")))


def errors_of(root: Path, name: str) -> list[str]:
    """The rules `docs check` fails the page `name` on in the change since `main`."""
    result = docs_check.check_changed(root, "main", context(root))
    return [item.rule for item in result.findings.get(name, []) if item.severity == "error"]


# --- what a moved page is excused from --------------------------------------------------------------------------------


MOVED = "docs/research/widget-old.md"


def test_a_moved_page_is_graded_where_the_move_changed_it_and_excused_elsewhere() -> None:
    text = page(f"The widget spins at {IN_FUNCTION}.\n", sections={"Data": f"The table sits at {IN_FUNCTION}.\n"})
    lines = text.splitlines()
    behaviour = next(i for i, line in enumerate(lines, 1) if line.startswith("The widget"))
    data = next(i for i, line in enumerate(lines, 1) if line.startswith("The table"))
    listed = env(legacy={RESEARCH_PATH: {"evidence": 2}})
    # Only the Data section changed in the move: its address owes a level, the one in Behaviour is still excused.
    changed = findings(RESEARCH_PATH, text, listed, Touch(frozenset({data}), moved_from=MOVED))
    graded = sorted((item.line, item.severity) for item in changed if item.rule == "evidence")
    assert graded == sorted([(data, "error"), (behaviour, "warning")])
    # A move that changed no line (a pure rename) leaves the grading excused page-wide.
    pure = findings(RESEARCH_PATH, text, listed, Touch(frozenset(), moved_from=MOVED))
    assert rules_of(pure) == [] and rules_of(pure, "warning") == ["evidence"]


def test_a_moved_page_is_excused_the_grading_of_the_sections_it_did_not_change_and_no_other_rule() -> None:
    # The entry lists more than 'evidence' and 'length', yet a moved page keeps only the grading.
    text = page("The widget spins.\n", head="")
    listed = env(legacy={RESEARCH_PATH: {"evidence": 1, "verified": 1, "length": 4}})
    errored = rules_of(findings(RESEARCH_PATH, text, listed, Touch(frozenset(), moved_from=MOVED)))
    assert "verified" in errored


def test_a_moved_page_keeps_an_old_runtime_claim_and_the_missing_run_records() -> None:
    claim = f"It spins at {IN_FUNCTION} (confirmed (runtime)).\n"
    text = page(claim)
    listed = env(legacy={RESEARCH_PATH: {"evidence": 1}})
    moved = findings(RESEARCH_PATH, text, listed, Touch(moved_from="docs/research/widget-old.md"))
    assert rules_of(moved) == []
    assert rules_of(moved, "warning") == ["evidence"]
    # The same page, new: a runtime claim has no record to cite.
    assert rules_of(findings(RESEARCH_PATH, text, env())) == ["evidence"]


def test_a_moved_page_whose_entry_is_the_old_form_claims_keeps_it_until_they_cite_records() -> None:
    claim = f"It spins at {IN_FUNCTION} (confirmed (runtime)).\n"
    text = page(claim, sections={"How this was checked": "Seen in PCSX2 before run records existed.\n"})
    listed = env(legacy={RESEARCH_PATH: {"evidence": 1}})
    moved = Touch(moved_from="docs/research/widget-old.md")
    found = findings(RESEARCH_PATH, text, listed, moved)
    assert rules_of(found) == [] and rules_of(found, "warning") == ["evidence"]
    cited = page(
        f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n",
        sections={"How this was checked": "Seen in PCSX2.\n"},
    )
    recorded = env(legacy={RESEARCH_PATH: {"evidence": 1}}, runs=frozenset({"r-1"}))
    assert rules_of(findings(RESEARCH_PATH, cited, recorded, moved)) == ["legacy"]


def test_a_moved_page_is_held_to_the_run_ids_of_the_levels_and_the_other_rules() -> None:
    text = page(f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n", head="")
    listed = env(legacy={RESEARCH_PATH: {"evidence": 1, "verified": 1}}, runs=frozenset({"r-2"}))
    errored = rules_of(findings(RESEARCH_PATH, text, listed, Touch(frozenset({1}), moved_from=MOVED)))
    # The entry's 'verified' is ignored for a moved page, and a run id that names no record is no grading.
    assert "verified" in errored and "evidence" in errored


# --- the page moved with git ------------------------------------------------------------------------------------------


def test_a_moved_page_passes_with_its_grading_still_a_warning(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text())
    follow(root)
    assert listed(root)[NEW] == {"evidence": 1} and OLD not in listed(root)
    result = docs_check.check_changed(root, "main", context(root))
    assert result.failed == {}
    assert [item.severity for item in result.findings[NEW] if item.rule == "evidence"] == ["warning"]


def test_a_section_the_move_leaves_alone_keeps_its_grading_excused(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text(data="The Data section gained a line in the move.\n"))
    follow(root)
    touch = docs_check.change_touches(root, "main", [NEW])[NEW]
    assert touch.moved_from == OLD and touch.lines
    result = docs_check.check_changed(root, "main", context(root))
    assert result.failed == {}
    assert [item.severity for item in result.findings[NEW] if item.rule == "evidence"] == ["warning"]


def test_a_section_the_move_changes_is_graded(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    # A line added to the Behaviour section, beside the address that has no level.
    move(root, new_text("The move added this line.\n\n"))
    follow(root)
    assert errors_of(root, NEW) == ["evidence"]


def test_an_address_the_move_adds_owes_its_level_and_the_untouched_ones_do_not(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text(data=f"A fact the move added, at {IN_FUNCTION}.\n"))
    follow(root)
    result = docs_check.check_changed(root, "main", context(root))
    graded = sorted((item.line, item.severity) for item in result.findings[NEW] if item.rule == "evidence")
    assert [severity for _, severity in graded] == ["error", "warning"] and "evidence" in errors_of(root, NEW)


def test_a_moved_page_is_checked_in_full_for_everything_but_the_grading(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text(head=""))
    follow(root)
    assert errors_of(root, NEW) == ["verified"]


def test_a_moved_page_that_is_too_long_fails_the_length_its_old_entry_excused(tmp_path: Path) -> None:
    limit = 1500
    long_text = old_text("x\n\n" * limit)
    root = moved_checkout(tmp_path, long_text)
    assert "length" in listed(root)[OLD]
    move(root, new_text("x\n\n" * limit))
    follow(root)
    assert listed(root)[NEW] == {"evidence": 1}
    assert errors_of(root, NEW) == ["length"]


def test_the_entry_must_follow_the_move(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text())
    found = docs_check.check_changed(root, "main", context(root))
    assert any(f"{OLD} moved to {NEW}" in item.message for item in found.failed[LEGACY_FILE])
    follow(root)
    assert LEGACY_FILE not in docs_check.check_changed(root, "main", context(root)).failed


def test_a_hand_written_entry_for_the_moved_page_is_accepted(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text())
    rules = {path: rules for path, rules in listed(root).items() if path != OLD}
    write(root, LEGACY_FILE, format_legacy(Legacy({**rules, NEW: {"evidence": 1}})))
    assert docs_check.check_changed(root, "main", context(root)).failed == {}


def test_a_moved_entry_may_list_only_the_grading(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text())
    write(root, LEGACY_FILE, f'["{NEW}"]\nevidence = 1\nfront-matter = 1\n')
    found = docs_check.check_changed(root, "main", context(root)).failed[LEGACY_FILE]
    assert any("front-matter" in item.message and "a moved page keeps only" in item.message for item in found)


def test_a_page_whose_entry_did_not_excuse_the_grading_gains_no_exemption(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path, page("Nothing at an address.\n", meta=""))
    assert "evidence" not in listed(root)[OLD]
    move(root, page(CLAIM + "Nothing at an address.\n"))
    write(root, LEGACY_FILE, f'["{NEW}"]\nevidence = 1\n')
    found = docs_check.check_changed(root, "main", context(root))
    assert any("excused from 'evidence' that it was not before" in item.message for item in found.failed[LEGACY_FILE])


def test_pruning_without_a_base_leaves_a_moved_entry_to_the_gone_rule(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text())
    assert docs_legacy.pruned(root, context(root)).pages.get(NEW) is None


# --- a new page is never a moved one ----------------------------------------------------------------------------------


def test_a_copy_of_a_legacy_page_is_a_new_page_and_cannot_be_listed(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    write(root, NEW, new_text())
    git(root, "add", "-A")
    write(root, LEGACY_FILE, (root / LEGACY_FILE).read_text(encoding="utf-8") + f'\n["{NEW}"]\nevidence = 1\n')
    result = docs_check.check_changed(root, "main", context(root))
    assert any("a new page cannot be legacy" in item.message for item in result.failed[NEW])
    assert any(f"{NEW} is added" in item.message for item in result.failed[LEGACY_FILE])


def test_a_new_page_is_not_excused_when_the_legacy_page_it_copies_is_deleted_elsewhere(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    # Delete the legacy page and write a different one: too little of it is the old page for git to call it a move.
    git(root, "rm", "-q", OLD)
    write(root, NEW, new_text().replace(FILLER, "".join(f"Other fact {n}, all of it new.\n\n" for n in range(60))))
    git(root, "add", "-A")
    write(root, LEGACY_FILE, f'["{NEW}"]\nevidence = 1\n')
    result = docs_check.check_changed(root, "main", context(root))
    assert any("a new page cannot be legacy" in item.message for item in result.failed[NEW])
    assert "evidence" in errors_of(root, NEW)


@pytest.mark.parametrize("staged", [True, False])
def test_an_unstaged_new_page_is_new(tmp_path: Path, staged: bool) -> None:
    root = moved_checkout(tmp_path)
    (root / NEW).parent.mkdir(parents=True)
    git(root, "rm", "-q", OLD)
    write(root, NEW, new_text())
    if staged:
        git(root, "add", "-A")
    # Staged, git pairs the deletion with the new file; unstaged, the new file is untracked and git cannot.
    touch = docs_check.change_touches(root, "main", [NEW])[NEW]
    assert (touch.moved_from == OLD) is staged and touch.new is (not staged)


def test_an_old_entry_left_beside_the_entry_that_followed_is_to_be_removed(tmp_path: Path) -> None:
    root = moved_checkout(tmp_path)
    move(root, new_text())
    kept = listed(root)
    write(root, LEGACY_FILE, format_legacy(Legacy({**kept, NEW: {"evidence": 1}})))
    found = docs_check.check_changed(root, "main", context(root)).failed[LEGACY_FILE]
    assert any(f"{OLD} moved to {NEW}, which is listed: remove the old entry" in item.message for item in found)
