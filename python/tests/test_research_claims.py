# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools research claims`: the graded blocks a branch changed on the research pages, what backs each
(a `studied` stamp, a run record) and what is wrong with them, over throwaway git repositories."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

import pytest
from conventions_support import REPO
from git_support import git
from stamp_support import rules as stamp_rules

from coney_tools import research_claims, run_records, stamps
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.run_records import RunRecord

PAGE = "docs/research/combat/grabs.md"
RUN = "r-20261023-grab-reach"

GRABS = """\
# Combat: grabs

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

The grab at `0x0026c548` starts the hold (confirmed (code)).

## Behaviour

### The intro {#grab-intro}

`Grab_Start` (`0x0026c548`) plays anim 71 (confirmed (code)).

The catch reach is 1.1 m (confirmed (runtime), run r-20261023-grab-reach).

The counter is a fixed roll, inferred from the table at `0x00500000`.

### Lead line

Confirmed (code) at the cited addresses unless a line says otherwise.

`Grab_Throw` at `0x00111111` throws the man.

`Grab_Drop` at `0x00222222` drops him, inferred from its caller.

### No level here

`Grab_Mount` at `0x00333333` mounts the man.

### Table {#table}

| Function | Role | Evidence |
| --- | --- | --- |
| `0x00444444` | picks the side | confirmed (code) |
| `0x00555555` | rolls the counter | speculative ([q-counter](#q-counter)) |

## In Coney

`Grab::start` is the port of `0x0026c548` (confirmed (code)).

## Open questions

- **The counter chance** {#q-counter}: read as `rand() % N`; find N.
"""


def make_repo(tmp_path: Path, text: str = GRABS, *, records: bool = True) -> Path:
    """A checkout with the research page committed on `main` and the real conventions."""
    root = tmp_path / "ws" / "repos" / "coney"
    (root / ".github").mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    shutil.copy(REPO / ".github" / "conventions.yml", root / ".github" / "conventions.yml")
    (root / PAGE).parent.mkdir(parents=True)
    (root / PAGE).write_text(text, encoding="utf-8", newline="\n")
    git(root, "init", "-q", "-b", "main")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    git(root, "checkout", "-q", "-b", "work")
    if records:
        good = RunRecord(
            id=RUN,
            date="2026-10-23",
            kind="recipe",
            tool="pcsx2 recipe run",
            agent="a",
            agent_type="engineer",
            model="m",
            source="hook",
            pcsx2_version="v2.9.94",
            copy="pcsx2-b",
            state="unknown",
            patches=(),
            name="find-human",
            args={},
            method="",
            values={"reach_m": 1.1},
            addresses={},
        )
        run_records.write(root, good)
    return root


def study(root: Path, *addresses: str) -> Path:
    """A stamp log with a passing `studied` stamp for each address; returns its folder."""
    folder = root.parent / "stamps"
    for address in addresses:
        stamps.append(
            folder,
            rules=stamp_rules(),
            task="t",
            step="studied",
            subject=address,
            result="pass",
            commit="c0ffee" * 6 + "c0ff",
            covers={"decompile_sha256": "ab" * 32},
            detail={"statements": 3},
            who={"kind": "tool", "tool": "test"},
        )
    return folder


def edit(root: Path, old: str, new: str, path: str = PAGE) -> None:
    """Replace `old` by `new` in a page of the checkout (not committed)."""
    text = (root / path).read_text(encoding="utf-8")
    assert old in text
    (root / path).write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def claims_of(root: Path, **options: object) -> research_claims.Report:
    """The report for the whole page (every line counts as changed)."""
    return research_claims.build(root, base=None, **options)  # type: ignore[arg-type]


def by_line(report: research_claims.Report, fragment: str) -> research_claims.Claim:
    """The claim whose source text holds `fragment`."""
    (found,) = [c for c in report.claims if fragment in c.text]
    return found


# --- what counts as a claim -------------------------------------------------------------------------------------------


def test_a_block_with_a_level_and_an_address_is_a_claim(tmp_path: Path) -> None:
    claim = by_line(claims_of(make_repo(tmp_path)), "plays anim 71")
    assert (claim.page, claim.line, claim.levels, claim.targets) == (PAGE, 13, ("code",), ("0x0026c548",))
    assert claim.via == "block" and claim.section == "grab-intro"


def test_summary_in_coney_and_open_questions_grade_nothing(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path))
    assert not [c for c in report.claims if "starts the hold" in c.text or "is the port of" in c.text]


def test_a_heading_lead_line_grades_the_blocks_under_it(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path))
    throw = by_line(report, "Grab_Throw")
    assert (throw.levels, throw.via, throw.targets) == (("code",), "lead", ("0x00111111",))
    # A block with its own level keeps it: this one is inferred, not the lead line's confirmed (code).
    drop = by_line(report, "Grab_Drop")
    assert (drop.levels, drop.via) == (("inferred",), "block")


def test_a_lead_line_covers_its_heading_only(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path))
    assert not [c for c in report.claims if "Grab_Mount" in c.text]
    assert report.ungraded[PAGE] == 1


def test_a_table_row_is_graded_by_its_evidence_cell(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path))
    assert by_line(report, "picks the side").levels == ("code",)
    assert by_line(report, "rolls the counter").levels == ("speculative",)
    assert not [c for c in report.claims if "| Function |" in c.text]


def test_a_locator_is_a_target(tmp_path: Path) -> None:
    root = make_repo(tmp_path, GRABS.replace("`Grab_Start` (`0x0026c548`)", "`level99.lua:f31` sets checkpoint 3 and"))
    claim = by_line(claims_of(root), "sets checkpoint 3")
    assert claim.targets == ("level99.lua:f31",)


def test_only_eight_digit_addresses_are_targets_as_docs_check_reads_them(tmp_path: Path) -> None:
    root = make_repo(tmp_path, GRABS.replace("`0x0026c548`) plays", "`0x26c548`) plays"))
    # It cites no address now, so as the first block under its heading it is that heading's lead line, not a claim.
    assert not [c for c in claims_of(root).claims if "plays anim 71" in c.text]


def test_only_the_changed_lines_are_listed(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    edit(root, "plays anim 71", "plays anim 72")
    report = research_claims.build(root, base="main")
    assert [c.line for c in report.claims] == [13]
    assert report.pages == [PAGE]


def test_a_new_page_counts_whole(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    new = root / "docs/research/combat/throws.md"
    new.write_text("# T\n\n## Behaviour\n\n`Throw` at `0x00123456` (confirmed (code)).\n", encoding="utf-8")
    report = research_claims.build(root, base="main")
    assert [(c.page, c.line) for c in report.claims] == [("docs/research/combat/throws.md", 5)]


MOVED = "docs/research/combat/grabs/index.md"
NO_RUN = GRABS.replace(", run r-20261023-grab-reach", "")


def move_page(root: Path) -> None:
    """Move the page with git into a folder of its own, as a restructure does."""
    (root / MOVED).parent.mkdir(parents=True)
    git(root, "mv", PAGE, MOVED)


def test_a_moved_page_is_graded_on_the_lines_git_changes_not_as_new(tmp_path: Path) -> None:
    # The runtime claim has no run record: an error on a new page, but this text did not change.
    root = make_repo(tmp_path, NO_RUN)
    move_page(root)
    report = research_claims.build(root, base="main")
    assert report.claims == []
    assert report.errors == []


def test_a_moved_page_grades_the_lines_it_changed(tmp_path: Path) -> None:
    root = make_repo(tmp_path, NO_RUN)
    move_page(root)
    edit(root, "plays anim 71", "plays anim 72", MOVED)
    report = research_claims.build(root, base="main")
    assert [(c.page, c.line) for c in report.claims] == [(MOVED, 13)]
    assert report.pages == [MOVED]
    assert report.errors == []


def test_a_new_runtime_claim_on_a_moved_page_is_still_an_error(tmp_path: Path) -> None:
    root = make_repo(tmp_path, NO_RUN)
    move_page(root)
    new = "The throw lands at 2 m (confirmed (runtime)).\n\nThe counter is a fixed roll"
    edit(root, "The counter is a fixed roll", new, MOVED)
    report = research_claims.build(root, base="main")
    assert [c.line for c in report.claims] == [17]
    assert any("a new confirmed (runtime) claim cites a run record" in e for e in report.errors)


THROWS = "docs/research/combat/throws.md"
THROWS_TEXT = "# T\n\n## Behaviour\n\n`Throw` at `0x00123456` (confirmed (code)).\n"
REACH = "The catch reach is 1.1 m (confirmed (runtime))."


def split_repo(tmp_path: Path) -> Path:
    """A checkout whose base holds the grabs page (its runtime claim cites no run record: legacy) and a throws page,
    both of which stay."""
    root = make_repo(tmp_path, NO_RUN)
    git(root, "checkout", "-q", "main")
    (root / THROWS).write_text(THROWS_TEXT, encoding="utf-8", newline="\n")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "throws")
    git(root, "checkout", "-q", "-B", "work")
    return root


def move_reach(root: Path, *, keep: bool = False, edited: str = REACH, source: str = REACH) -> None:
    """Move the reach claim (`source`, as the grabs page has it) to the throws page (copy it when `keep`), as `edited`
    says."""
    if not keep:
        edit(root, f"{source}\n\n", "")
    page = (root / THROWS).read_text(encoding="utf-8")
    (root / THROWS).write_text(f"{page}\n{edited}\n", encoding="utf-8", newline="\n")


def test_a_runtime_block_moved_unchanged_between_two_pages_is_not_new(tmp_path: Path) -> None:
    # The claim has no run record: an error as a new block on the throws page, but it was legacy on the grabs page.
    root = split_repo(tmp_path)
    move_reach(root)
    report = research_claims.build(root, base="main")
    assert report.errors == []
    assert [c.page for c in report.claims] == [THROWS]
    assert any("legacy" in w for w in report.warnings)


def test_a_moved_block_whose_link_targets_were_rewritten_is_not_new(tmp_path: Path) -> None:
    # A move rewrites the relative links of the block; the claim is the same.
    root = split_repo(tmp_path)
    linked = REACH.replace("catch reach", "[catch reach](#reach)")
    edit(root, REACH, linked)
    git(root, "commit", "-q", "-am", "link")
    git(root, "branch", "-f", "main", "HEAD")
    move_reach(root, edited=linked.replace("(#reach)", "(grabs.md#reach)"), source=linked)
    assert research_claims.build(root, base="main").errors == []


def test_a_moved_block_with_new_link_text_is_still_new(tmp_path: Path) -> None:
    root = split_repo(tmp_path)
    linked = REACH.replace("catch reach", "[catch reach](#reach)")
    edit(root, REACH, linked)
    git(root, "commit", "-q", "-am", "link")
    git(root, "branch", "-f", "main", "HEAD")
    move_reach(root, edited=linked.replace("[catch reach]", "[catch range]"), source=linked)
    assert any("a new confirmed (runtime) claim" in e for e in research_claims.build(root, base="main").errors)


def test_a_block_moved_and_edited_is_still_new(tmp_path: Path) -> None:
    root = split_repo(tmp_path)
    move_reach(root, edited=REACH.replace("1.1 m", "1.2 m"))
    report = research_claims.build(root, base="main")
    assert any(f"{THROWS}:" in e and "a new confirmed (runtime) claim" in e for e in report.errors)


def test_a_new_block_next_to_a_moved_one_is_still_new(tmp_path: Path) -> None:
    root = split_repo(tmp_path)
    move_reach(root)
    page = (root / THROWS).read_text(encoding="utf-8")
    (root / THROWS).write_text(f"{page}\nThe throw lands at 2 m (confirmed (runtime)).\n", encoding="utf-8")
    report = research_claims.build(root, base="main")
    assert len(report.claims) == 2
    assert len(report.errors) == 1 and f"{THROWS}:9" in report.errors[0]


def test_a_block_copied_to_another_page_is_new_there(tmp_path: Path) -> None:
    # It is still on the grabs page, so nothing moved.
    root = split_repo(tmp_path)
    move_reach(root, keep=True)
    report = research_claims.build(root, base="main")
    assert any(f"{THROWS}:" in e and "a new confirmed (runtime) claim" in e for e in report.errors)


def test_a_block_moved_from_a_page_the_branch_deleted_is_not_new(tmp_path: Path) -> None:
    root = split_repo(tmp_path)
    move_reach(root, keep=True)
    git(root, "rm", "-q", "-f", PAGE)
    assert research_claims.build(root, base="main").errors == []


def test_a_block_moved_to_a_page_the_base_did_not_have_is_not_new(tmp_path: Path) -> None:
    # The destination is untracked (nothing to compare it with at the base); the source page stays.
    root = split_repo(tmp_path)
    edit(root, f"{REACH}\n\n", "")
    other = root / "docs/research/combat/reach.md"
    other.write_text(f"# R\n\n## Behaviour\n\n{REACH}\n", encoding="utf-8", newline="\n")
    assert research_claims.build(root, base="main").errors == []


def test_a_page_the_branch_did_not_touch_is_not_listed(tmp_path: Path) -> None:
    assert research_claims.build(make_repo(tmp_path), base="main").claims == []


def test_a_base_that_is_not_a_commit_is_one_line(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="origin/nothing"):
        research_claims.build(make_repo(tmp_path), base="origin/nothing")


# --- backing ----------------------------------------------------------------------------------------------------------


def test_a_studied_address_backs_a_code_claim(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    report = claims_of(root, stamp_folder=study(root, "0x0026c548", "0x00111111"))
    assert by_line(report, "plays anim 71").backing == "studied"
    assert by_line(report, "Grab_Throw").backing == "studied"
    assert by_line(report, "picks the side").backing == "none"


def test_a_claim_of_two_addresses_is_partly_backed(tmp_path: Path) -> None:
    root = make_repo(tmp_path, GRABS.replace("(`0x0026c548`) plays", "(`0x0026c548`, `0x0026c1d8`) plays"))
    report = claims_of(root, stamp_folder=study(root, "0x0026c548"))
    assert by_line(report, "plays anim 71").backing == "studied 1 of 2"


def test_without_a_stamp_log_code_claims_are_unbacked(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path))
    assert by_line(report, "plays anim 71").backing == "none"
    assert report.errors == []


def test_require_study_makes_an_unbacked_code_claim_an_error(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    report = claims_of(root, require_study=True, stamp_folder=study(root, "0x0026c548"))
    assert [e for e in report.errors if "0x00444444" in e or "picks the side" in e]
    assert not [e for e in report.errors if "plays anim 71" in e]


def test_a_runtime_claim_is_backed_by_its_run_record(tmp_path: Path) -> None:
    claim = by_line(claims_of(make_repo(tmp_path)), "catch reach")
    assert (claim.levels, claim.backing) == (("runtime",), f"run {RUN}")


def test_a_run_record_that_does_not_exist_is_an_error(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path, records=False))
    assert any(f"run {RUN}" in e and "no run record" in e and f"{PAGE}:15" in e for e in report.errors)


def test_a_run_record_that_breaks_a_rule_is_an_error(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    path = root / "research" / "runs" / f"{RUN}.toml"
    path.write_text(path.read_text(encoding="utf-8").replace("reach_m = 1.1", "reach_m = 9.9"), encoding="utf-8")
    report = claims_of(root)
    assert any("digest" in e for e in report.errors)


def test_every_bad_record_in_the_folder_is_reported(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    (root / "research" / "runs" / "r-20261023-junk.toml").write_text("x = = 1", encoding="utf-8")
    assert any("r-20261023-junk.toml: not valid TOML" in e for e in claims_of(root).errors)


def test_a_new_runtime_claim_without_a_run_record_is_an_error(tmp_path: Path) -> None:
    root = make_repo(tmp_path, GRABS.replace(", run r-20261023-grab-reach", ""))
    report = claims_of(root)
    assert by_line(report, "catch reach").backing == "none"
    assert any("a new confirmed (runtime) claim cites a run record" in e for e in report.errors)


def test_a_new_runtime_claim_in_the_legacy_form_is_still_an_error(tmp_path: Path) -> None:
    legacy = GRABS.replace("run r-20261023-grab-reach", "PCSX2 2.9.94, a watch on the roll")
    root = make_repo(tmp_path, legacy)
    new = root / "docs/research/combat/throws.md"
    page = "# T\n\n## Behaviour\n\nReach is 2 m (confirmed (runtime), PCSX2 2.9.94, a watch).\n"
    new.write_text(page, encoding="utf-8")
    report = research_claims.build(root, base="main")
    assert [c.backing for c in report.claims] == ["none"]
    assert any("a new confirmed (runtime) claim" in e for e in report.errors)


def test_a_touched_legacy_runtime_claim_keeps_its_level_with_a_warning(tmp_path: Path) -> None:
    legacy = GRABS.replace("run r-20261023-grab-reach", "PCSX2 2.9.94, a watch on the roll")
    root = make_repo(tmp_path, legacy, records=False)
    edit(root, "The catch reach is 1.1 m", "The catch reach is 1.2 m")
    report = research_claims.build(root, base="main")
    assert by_line(report, "catch reach").backing == "legacy method" and report.errors == []
    assert any("legacy" in w for w in report.warnings)


def test_a_touched_legacy_claim_with_no_version_is_a_warning_too(tmp_path: Path) -> None:
    legacy = GRABS.replace(", run r-20261023-grab-reach", "")
    root = make_repo(tmp_path, legacy, records=False)
    edit(root, "The catch reach is 1.1 m", "The catch reach is 1.2 m")
    report = research_claims.build(root, base="main")
    assert by_line(report, "catch reach").backing == "none" and report.errors == []
    assert any("no PCSX2 version" in w for w in report.warnings)


def test_a_speculative_claim_links_an_open_question_that_exists(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path))
    assert by_line(report, "rolls the counter").backing == "question q-counter"
    broken = claims_of(make_repo(tmp_path / "b", GRABS.replace("#q-counter)", "#q-nothing)")))
    assert any("q-nothing" in e and "open question" in e for e in broken.errors)


def test_a_speculative_claim_with_no_question_is_a_warning(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path, GRABS.replace(" ([q-counter](#q-counter))", "")))
    assert any("links no open question" in w for w in report.warnings) and report.errors == []


def test_the_form_of_a_level_is_left_to_docs_check(tmp_path: Path) -> None:
    # A bare `inferred` is `docs check`'s warning; the claims list says only what a claim rests on.
    report = claims_of(make_repo(tmp_path, GRABS.replace("inferred from its caller", "inferred")))
    assert by_line(report, "Grab_Drop").levels == ("inferred",)
    assert not [w for w in report.warnings if "basis" in w]


# --- the tests that cite a claim --------------------------------------------------------------------------------------


def test_a_test_that_expects_a_section_is_listed_with_its_claims(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    (root / "tests").mkdir()
    (root / "tests" / "grab_test.cpp").write_text(
        '// header\n// Expected: research/combat/grabs.md#grab-intro, confirmed (code)\nTEST_CASE("x") {}\n',
        encoding="utf-8",
    )
    (root / "tests" / "other_test.cpp").write_text("// Expected: research/combat/grabs.md#table\n", encoding="utf-8")
    report = claims_of(root)
    assert by_line(report, "plays anim 71").tests == ("tests/grab_test.cpp:2",)
    assert by_line(report, "picks the side").tests == ("tests/other_test.cpp:1",)
    assert by_line(report, "Grab_Throw").tests == ()


def test_a_citation_without_the_research_folder_is_seen_too(tmp_path: Path) -> None:
    # The form is `standards.tests.citation` in conventions.yml: the page below docs/research, `research/` optional.
    root = make_repo(tmp_path)
    (root / "tests").mkdir()
    (root / "tests" / "grab_test.cpp").write_text(
        'TEST_CASE("x") {\n    // Expected: combat/grabs.md#grab-intro, confirmed (code)\n}\n'
        'TEST_CASE("y") {\n    // Expected: unverified, #12\n}\n',
        encoding="utf-8",
    )
    report = claims_of(root)
    assert by_line(report, "plays anim 71").tests == ("tests/grab_test.cpp:2",)


def test_a_runtime_lead_line_names_the_run_record_of_the_blocks_under_it(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    lead = "### Reach\n\nConfirmed (runtime), run r-20261023-grab-reach.\n\nThe reach is 1.5 at `0x00123456`.\n\n"
    edit(root, "### No level here\n", lead + "### No level here\n")
    report = claims_of(root)
    claim = by_line(report, "The reach is 1.5")
    assert (claim.levels, claim.via, claim.backing) == (("runtime",), "lead", f"run {RUN}")
    assert not [e for e in report.errors if "a new confirmed (runtime) claim" in e]


def test_a_heading_anchor_is_the_one_mkdocs_makes(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    edit(root, "### No level here\n", "### Mount  the man - fast\n")
    edit(root, "mounts the man.", "mounts the man (confirmed (code)).")
    (root / "tests").mkdir()
    (root / "tests" / "mount_test.cpp").write_text(
        "// Expected: research/combat/grabs.md#mount-the-man-fast\n", encoding="utf-8"
    )
    claim = by_line(claims_of(root), "Grab_Mount")
    assert (claim.section, claim.tests) == ("mount-the-man-fast", ("tests/mount_test.cpp:1",))


def test_a_second_heading_of_the_same_name_has_the_numbered_anchor(tmp_path: Path) -> None:
    root = make_repo(tmp_path)
    edit(root, "### No level here\n", "### Lead line\n")
    edit(root, "mounts the man.", "mounts the man (confirmed (code)).")
    (root / "tests").mkdir()
    (root / "tests" / "first_test.cpp").write_text(
        "// Expected: research/combat/grabs.md#lead-line\n", encoding="utf-8"
    )
    (root / "tests" / "second_test.cpp").write_text(
        "// Expected: research/combat/grabs.md#lead-line_1\n", encoding="utf-8"
    )
    report = claims_of(root)
    first, second = by_line(report, "Grab_Throw"), by_line(report, "Grab_Mount")
    assert (first.section, first.tests) == ("lead-line", ("tests/first_test.cpp:1",))
    assert (second.section, second.tests) == ("lead-line_1", ("tests/second_test.cpp:1",))


# --- output -----------------------------------------------------------------------------------------------------------


def test_the_text_lists_a_line_per_claim_and_the_problems(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path, records=False))
    text = research_claims.render(report)
    assert PAGE in text and "L13" in text and "confirmed (code)" in text and "0x0026c548" in text
    assert f"error: {PAGE}:15" in text and "claims in 1 page" in text and "1 address block with no level" in text


def test_the_json_has_the_same_rows(tmp_path: Path) -> None:
    report = claims_of(make_repo(tmp_path))
    data = json.loads(research_claims.render_json(report))
    row = next(r for r in data["claims"] if r["line"] == 13)
    assert row["levels"] == ["code"] and row["targets"] == ["0x0026c548"] and row["backing"] == "none"
    assert data["errors"] == [] and data["ungraded"] == {PAGE: 1}


def test_the_command_lists_the_branch_and_fails_on_an_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = make_repo(tmp_path)
    monkeypatch.chdir(root)
    edit(root, "plays anim 71", "plays anim 72")
    assert main(["research", "claims", "--base", "main", "--task", "t"]) == 0
    assert "L13" in capsys.readouterr().out
    edit(root, "run r-20261023-grab-reach", "run r-20261023-gone")
    assert main(["research", "claims", "--base", "main", "--task", "t"]) == 1
    assert "r-20261023-gone" in capsys.readouterr().out
    assert main(["research", "claims", "--base", "main", "--task", "t", "--json"]) == 1
    assert json.loads(capsys.readouterr().out)["errors"]


def test_the_command_takes_a_stamp_folder_and_require_study(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = make_repo(tmp_path)
    monkeypatch.chdir(root)
    edit(root, "plays anim 71", "plays anim 72")
    log = study(root, "0x0026c548")
    assert main(["research", "claims", "--base", "main", "--stamps", str(log), "--require-study"]) == 0
    assert "studied" in capsys.readouterr().out
