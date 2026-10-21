# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the daily progress history: days from git, snapshots by today's tracker, the cache and the chart."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest
from test_progress import ONE_FUNCTION, ONE_TAG, make_checkout

from coney_tools import progress, progress_history
from coney_tools.progress_history import Point

pytestmark = pytest.mark.skipif(shutil.which("git") is None, reason="needs git")

SECOND_FUNCTION = '\n[[function]]\naddress = 0x00001210\nname = "Other::Do"\nsubsystem = "Beta"\nsize = 64\n'
SECOND_TAG = "/// @orig 0x00001210 Other::Do (Other.cpp)\nvoid other();\n"


def git(repo: Path, *args: str, when: str | None = None) -> str:
    """Run git in `repo`; `when` is the commit's UTC time."""
    env = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_SYSTEM=os.devnull)
    if when:
        env |= {"GIT_AUTHOR_DATE": f"{when} +0000", "GIT_COMMITTER_DATE": f"{when} +0000"}
    done = subprocess.run(
        ["git", "-C", str(repo), "-c", "user.name=T", "-c", "user.email=t@example.invalid", *args],
        capture_output=True,
        text=True,
        check=True,
        env=env,
    )
    return done.stdout.strip()


def commit(repo: Path, when: str, message: str = "x") -> str:
    """Commit everything at `when` and return the abbreviated hash."""
    git(repo, "add", "-A")
    git(repo, "commit", "-m", message, "--allow-empty", when=when)
    return git(repo, "rev-parse", "--short", "HEAD")


def make_repo(tmp_path: Path) -> tuple[Path, dict[str, str]]:
    """A repository with four commits on three days: no tracker yet, one function, one function, two functions."""
    repo = tmp_path / "repo"
    repo.mkdir()
    git(repo, "init", "-q", "-b", "main")
    (repo / "README.md").write_text("hello\n", encoding="utf-8")
    shas = {"early": commit(repo, "2026-10-04T10:00:00")}
    make_checkout(repo, ONE_FUNCTION, ONE_TAG)
    shas["d5a"] = commit(repo, "2026-10-05T09:00:00")
    shas["d5b"] = commit(repo, "2026-10-05T18:00:00")  # the last of its day: this one counts
    (repo / "docs" / "progress" / "functions.toml").write_text(ONE_FUNCTION + SECOND_FUNCTION, encoding="utf-8")
    (repo / "src" / "core" / "thing.h").write_text(ONE_TAG + SECOND_TAG, encoding="utf-8")
    shas["d6"] = commit(repo, "2026-10-06T12:00:00")
    return repo, shas


def current_point(repo: Path, date: str = "2026-10-07") -> Point:
    return progress_history.point_of(progress.load(repo), date, "head")


def test_days_are_the_last_commit_of_each_utc_day(tmp_path: Path) -> None:
    repo, shas = make_repo(tmp_path)
    assert progress_history.day_commits(repo) == [
        ("2026-10-04", shas["early"]),
        ("2026-10-05", shas["d5b"]),
        ("2026-10-06", shas["d6"]),
    ]


def test_each_day_is_measured_at_its_commit_and_unreadable_days_are_left_out(tmp_path: Path) -> None:
    """The first day has no tracker inputs, so it is skipped; the others use the functions listed on that day."""
    repo, shas = make_repo(tmp_path)
    points, skipped = progress_history.build(repo, current_point(repo))
    assert skipped == [("2026-10-04", shas["early"])]
    assert [(p.date, p.commit, p.reimplemented_functions) for p in points] == [
        ("2026-10-05", shas["d5b"], 1),
        ("2026-10-06", shas["d6"], 2),
        ("2026-10-07", "head", 2),
    ]
    assert points[0].reimplemented_bytes == 0x80 and points[1].reimplemented_bytes == 0x80 + 64
    assert points[0].understood_bytes is None  # no Ghidra listing in this checkout
    assert points[0].share("reimplemented") == pytest.approx(100 * 0x80 / points[0].game_bytes)
    assert points[0].share("understood") is None


def test_today_is_the_builds_own_point_and_later_days_are_not_measured(tmp_path: Path) -> None:
    repo, _ = make_repo(tmp_path)
    points, _ = progress_history.build(repo, current_point(repo, "2026-10-06"))
    assert [p.date for p in points] == ["2026-10-05", "2026-10-06"]
    assert points[-1].commit == "head"  # the current state replaces the day's last commit


def test_a_cached_day_is_reused_only_for_the_commit_it_was_measured_at(tmp_path: Path) -> None:
    repo, shas = make_repo(tmp_path)
    calls: list[str] = []

    def measure(root: Path, date: str, commit: str) -> Point | None:
        calls.append(date)
        return Point(date, commit, 100, 1, 1, 1)

    fresh = Point("2026-10-05", shas["d5b"], 100, 50, 7, 1)
    rewritten = Point("2026-10-06", "0000000", 100, 50, 7, 1)
    points, _ = progress_history.build(repo, current_point(repo), [fresh, rewritten], measure)
    by_date = {p.date: p for p in points}
    assert by_date["2026-10-05"] is fresh and "2026-10-05" not in calls  # same commit: kept
    assert "2026-10-06" in calls and by_date["2026-10-06"].reimplemented_functions == 1  # other commit: measured again
    assert calls.count("2026-10-04") == 1  # a day the cache lacks is measured


def test_without_git_history_the_history_is_today(tmp_path: Path) -> None:
    """A folder that is not a repository (a copy of the files) still builds: one point."""
    root = make_checkout(tmp_path / "plain", ONE_FUNCTION, ONE_TAG)
    points, skipped = progress_history.build(root, current_point(root))
    assert [p.date for p in points] == ["2026-10-07"] and skipped == []


def test_the_history_file_round_trips_and_a_foreign_schema_is_ignored(tmp_path: Path) -> None:
    repo, _ = make_repo(tmp_path)
    points, skipped = progress_history.build(repo, current_point(repo))
    text = json.dumps(progress_history.to_json(points, skipped))
    assert progress_history.load_cache(text) == (points, skipped)
    good = json.loads(text)
    point = good["points"][0]
    for bad in (
        "",
        "[]",
        "{",
        json.dumps({**good, "schema": 99}),
        json.dumps({**good, "points": [{"x": 1}]}),
        json.dumps({**good, "points": [{**point, "date": 5}]}),
        json.dumps({**good, "points": [{**point, "commit": ""}]}),
        json.dumps({**good, "points": [{**point, "game_bytes": "100"}]}),
        json.dumps({**good, "points": [{**point, "game_bytes": True}]}),
        json.dumps({**good, "points": [{**point, "game_bytes": None}]}),  # only the listing fields may be unknown
        json.dumps({**good, "points": "x"}),
        json.dumps({**good, "skipped": ["2026-10-04"]}),
    ):
        assert progress_history.load_cache(bad) == ([], [])  # ignored, never an exception that fails the build


def test_a_change_to_the_measuring_code_invalidates_the_cache(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The cache is keyed on a digest of the tracker's code, so a new definition never mixes with old days."""
    repo, _ = make_repo(tmp_path)
    points, skipped = progress_history.build(repo, current_point(repo))
    text = json.dumps(progress_history.to_json(points, skipped))
    assert progress_history.load_cache(text)[0] == points
    code = tmp_path / "measure.py"
    code.write_bytes(b"COUNT = 1\n")
    fake = type("Module", (), {"__file__": str(code)})
    monkeypatch.setattr(progress_history, "DEFINITION_MODULES", (fake,))
    before = progress_history.definition()
    assert progress_history.load_cache(text) == ([], [])  # written under another definition: rebuilt
    code.write_bytes(b"COUNT = 1\r\n")
    assert progress_history.definition() == before  # a Windows checkout measures by the same definition as CI
    code.write_bytes(b"COUNT = 2\n")
    assert progress_history.definition() != before


def test_the_definition_covers_every_module_that_measures() -> None:
    from coney_tools import progress, progress_research

    assert set(progress_history.DEFINITION_MODULES) == {progress, progress_research}


def test_a_left_out_day_is_not_tried_again_at_the_same_commit(tmp_path: Path) -> None:
    repo, shas = make_repo(tmp_path)
    calls: list[str] = []

    def measure(root: Path, date: str, commit: str) -> Point | None:
        calls.append(date)
        return None if date == "2026-10-04" else Point(date, commit, 100, 1, 1, 1)

    _, skipped = progress_history.build(repo, current_point(repo), [], measure, [("2026-10-04", shas["early"])])
    assert "2026-10-04" not in calls and skipped == [("2026-10-04", shas["early"])]
    calls.clear()
    progress_history.build(repo, current_point(repo), [], measure, [("2026-10-04", "0000000")])
    assert "2026-10-04" in calls  # recorded at another commit: tried again


def test_an_empty_commit_matches_nothing() -> None:
    assert not progress_history.same_commit("", "abc1234") and not progress_history.same_commit("abc1234", "")
    assert progress_history.same_commit("abc1234", "abc1234def") and not progress_history.same_commit("abc", "abd")
    days = [("2026-10-05", "abc1234")]
    cached = Point("2026-10-05", "", 100, 50, 7, 1)
    points, _ = progress_history.reconstruct(Path(), days, {cached.date: cached}, lambda r, d, c: None)
    assert points == []  # the cached point without a commit was not reused


def test_chart_has_a_titled_marker_per_day_and_measure(tmp_path: Path) -> None:
    repo, _ = make_repo(tmp_path)
    points, _ = progress_history.build(repo, current_point(repo))
    svg = progress_history.render_chart(points)
    root = ET.fromstring(svg)
    titles = [t.text or "" for t in root.iter("title")]
    # Two measures are known on every day here (the third needs a Ghidra listing); each has one marker per day.
    assert sum("Reimplemented" in t for t in titles) == 3 and sum("Researched" in t for t in titles) == 3
    assert not any("Understood" in t for t in titles)
    assert len(list(root.iter("polyline"))) == 2
    assert "\n\n" not in svg


def test_chart_and_table_handle_a_single_point() -> None:
    point = Point("2026-10-21", "abc", 1000, 100, 3, 500, 900, 40, 50, 1000)
    section = progress_history.render_section([point], [])
    ET.fromstring(section.split("\n\n")[0].replace('<div class="progress-grid">', "<div>").replace("</div>", "</div>"))
    assert "| 2026-10-21 | `abc` | 10.0% | 3 | 90.0% | 50.0% |" in section


def test_the_table_lists_the_newest_day_first_and_says_what_was_left_out() -> None:
    older = Point("2026-10-20", "a", 1000, 100, 3, 500)
    newer = Point("2026-10-21", "b", 1000, 200, 4, 500)
    section = progress_history.render_section([older, newer], ["2026-10-04"])
    assert section.index("2026-10-21") < section.index("| 2026-10-20 |")
    assert "| n/a |" in section and "left out" in section and "1." in section
