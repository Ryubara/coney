# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.build_history: how long each kind of build step took the last few times, kept in the claims
folder so a queued run can tell whether it would still fit its time budget."""

import json
from pathlib import Path

from coney_tools.build_history import KEEP, History, step_key


def test_nothing_is_expected_without_history(tmp_path: Path) -> None:
    assert History(tmp_path / "durations.json").expected("ci/build/full") is None


def test_the_median_of_the_recorded_durations_is_expected(tmp_path: Path) -> None:
    history = History(tmp_path / "durations.json")
    for seconds in (100.0, 900.0, 300.0):
        history.record("ci/build/full", seconds)
    assert history.expected("ci/build/full") == (300.0, 3)  # the middle one, not the mean: one odd run does not move it
    history.record("ci/build/full", 500.0)
    assert history.expected("ci/build/full") == (400.0, 4)  # even count: the mean of the two in the middle


def test_only_the_recent_runs_count(tmp_path: Path) -> None:
    history = History(tmp_path / "durations.json")
    for _ in range(KEEP):
        history.record("dev/test/full", 3000.0)
    for _ in range(KEEP):
        history.record("dev/test/full", 60.0)
    assert history.expected("dev/test/full") == (60.0, KEEP)


def test_each_preset_step_and_size_has_its_own_history(tmp_path: Path) -> None:
    history = History(tmp_path / "durations.json")
    history.record(step_key("ci", "test", "full"), 600.0)
    history.record(step_key("ci", "test", "filtered"), 20.0)
    history.record(step_key("dev", "test", "full"), 900.0)
    assert history.expected("ci/test/full") == (600.0, 1)
    assert history.expected("ci/test/filtered") == (20.0, 1)
    assert history.expected("dev/test/full") == (900.0, 1)
    assert history.expected("ci/build/full") is None


def test_it_is_kept_between_runs(tmp_path: Path) -> None:
    History(tmp_path / "durations.json").record("ci/build/full", 700.0)
    assert History(tmp_path / "durations.json").expected("ci/build/full") == (700.0, 1)


def test_a_damaged_file_is_history_that_is_not_there(tmp_path: Path) -> None:
    path = tmp_path / "durations.json"
    path.write_text("{ not json", encoding="utf-8")
    history = History(path)
    assert history.expected("ci/build/full") is None
    history.record("ci/build/full", 5.0)  # and it is written afresh
    assert History(path).expected("ci/build/full") == (5.0, 1)
    path.write_text(json.dumps({"ci/build/full": ["x", 4.0, None, -1]}), encoding="utf-8")
    assert History(path).expected("ci/build/full") == (4.0, 1)  # only the usable numbers


def test_a_folder_that_cannot_be_written_loses_the_record_not_the_run(tmp_path: Path) -> None:
    blocker = tmp_path / "file"
    blocker.write_text("", encoding="utf-8")
    History(blocker / "durations.json").record("ci/build/full", 5.0)  # no exception


def test_no_temporary_file_is_left_behind(tmp_path: Path) -> None:
    History(tmp_path / "durations.json").record("ci/build/full", 5.0)
    assert [p.name for p in tmp_path.iterdir()] == ["durations.json"]
