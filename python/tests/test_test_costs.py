# SPDX-License-Identifier: GPL-3.0-or-later
"""The recorded test durations the build's lane choice reads (coney_tools.test_costs)."""

from __future__ import annotations

from pathlib import Path

from coney_tools.test_costs import TestCosts, parse_cost_data, parse_test_names

COST_DATA = """coney.runs_headless 1 0.508704
the disc's level3 rooftop chase: every seed reaches the gallery 3 1503.25
a parser test 12 0.01
---
a failed test
"""

LISTING = """Test project C:/wt/build/dev
  Test    #1: coney.runs_headless
  Test #2711: the disc's level3 rooftop chase: every seed reaches the gallery

Total Tests: 2
"""


def test_the_cost_file_gives_each_test_its_average_seconds_names_with_spaces_included() -> None:
    assert parse_cost_data(COST_DATA) == {
        "coney.runs_headless": 0.508704,
        "the disc's level3 rooftop chase: every seed reaches the gallery": 1503.25,
        "a parser test": 0.01,
    }


def test_odd_lines_in_the_cost_file_are_skipped() -> None:
    assert parse_cost_data("no numbers here\nx 1 notanumber\ny -1 2.0\nz 1 -3\n\nok 2 1.5\n") == {"ok": 1.5}


def test_the_listing_names_each_selected_test() -> None:
    assert parse_test_names(LISTING) == [
        "coney.runs_headless",
        "the disc's level3 rooftop chase: every seed reaches the gallery",
    ]


def test_a_folders_costs_are_kept_for_every_worktree_and_the_folders_own_win(tmp_path: Path) -> None:
    shared = tmp_path / "claims" / "test-costs.json"
    first = tmp_path / "a" / "CTestCostData.txt"
    first.parent.mkdir()
    first.write_text(COST_DATA, encoding="utf-8")
    TestCosts(shared).record("dev", first)
    # A fresh build folder (no cost file of its own) still sees what another worktree's run recorded.
    fresh = TestCosts(shared).load("dev", tmp_path / "b" / "CTestCostData.txt")
    assert fresh["the disc's level3 rooftop chase: every seed reaches the gallery"] == 1503.25
    # Another preset's times are its own.
    assert TestCosts(shared).load("ci", tmp_path / "b" / "CTestCostData.txt") == {}
    # The folder's own file is newer than the record.
    own = tmp_path / "c" / "CTestCostData.txt"
    own.parent.mkdir()
    own.write_text("a parser test 13 0.02\n", encoding="utf-8")
    assert TestCosts(shared).load("dev", own)["a parser test"] == 0.02


def test_a_damaged_record_reads_as_empty_and_a_record_into_a_bad_folder_raises_nothing(tmp_path: Path) -> None:
    shared = tmp_path / "test-costs.json"
    shared.write_text("{not json", encoding="utf-8")
    assert TestCosts(shared).load("dev", tmp_path / "none.txt") == {}
    blocker = tmp_path / "file"
    blocker.write_text("", encoding="utf-8")
    source = tmp_path / "CTestCostData.txt"
    source.write_text(COST_DATA, encoding="utf-8")
    TestCosts(blocker / "test-costs.json").record("dev", source)  # the folder is a file: nothing written, no error
