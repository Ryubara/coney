# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the build record: what a run of `coney-tools build` leaves for `task check` and `pr verify` to read
(docs/guides/stamps.md#build-record)."""

import hashlib
from pathlib import Path
from typing import Any

import pytest

from coney_tools import build_record
from coney_tools.build_record import BuildRecordError

TIP = "ab" * 20


def make(**more: Any) -> dict[str, Any]:
    """A record of a whole, clean `ci` run with the disc, and what `problems` is told about the checkout."""
    values: dict[str, Any] = {
        "task": "757-fix-widget",
        "agent": "lane-a",
        "tip": TIP,
        "dirty": False,
        "preset": "ci",
        "filters": [],
        "command_line": ["coney-tools", "build", "--preset", "ci", "--test"],
        "exit_code": 0,
        "ran": ["configure", "build", "test"],
        "tests": {"total": 412, "passed": 412, "failed": 0, "skipped": 0, "skipped_disc": 0},
        "disc": "game_dir in coney.local.toml",
        "log_sha256": "cd" * 32,
        "started": "2026-10-23T12:00:00Z",
        "ended": "2026-10-23T12:31:00Z",
    }
    values.update(more)
    return build_record.make(**values)


def test_a_record_is_written_under_the_hash_of_its_bytes(tmp_path: Path) -> None:
    path = build_record.write(tmp_path / "build-runs", make())
    assert path.parent == tmp_path / "build-runs" and path.suffix == ".json"
    assert path.stem == hashlib.sha256(path.read_bytes()).hexdigest()
    assert build_record.read(tmp_path / "build-runs", path.stem)["tip"] == TIP


def test_the_same_record_is_the_same_file_and_another_run_another_file(tmp_path: Path) -> None:
    folder = tmp_path / "build-runs"
    assert build_record.write(folder, make()) == build_record.write(folder, make())
    assert build_record.write(folder, make(ended="2026-10-23T12:40:00Z")) != build_record.write(folder, make())
    assert len(list(folder.glob("*.json"))) == 2


def test_a_record_that_was_edited_no_longer_reads(tmp_path: Path) -> None:
    folder = tmp_path / "build-runs"
    path = build_record.write(folder, make())
    path.write_bytes(path.read_bytes().replace(b'"failed":0', b'"failed":1'))
    with pytest.raises(BuildRecordError, match="does not hash to its name"):
        build_record.read(folder, path.stem)


def test_a_missing_or_malformed_record_is_an_error_naming_it(tmp_path: Path) -> None:
    with pytest.raises(BuildRecordError, match="not in"):
        build_record.read(tmp_path, "ab" * 32)
    with pytest.raises(BuildRecordError, match="64 hex"):
        build_record.read(tmp_path, "../x")
    name = hashlib.sha256(b"not json").hexdigest()
    (tmp_path / f"{name}.json").write_bytes(b"not json")
    with pytest.raises(BuildRecordError, match="not valid JSON"):
        build_record.read(tmp_path, name)


def test_a_whole_clean_ci_run_has_no_problems() -> None:
    assert build_record.problems(make(), tip=TIP, preset="ci") == []


@pytest.mark.parametrize(
    ("change", "text"),
    [
        ({"preset": "dev"}, "preset dev, Check needs ci"),
        ({"filters": ["-R", "Widget"]}, "a filtered run (-R Widget); Check needs the full suite"),
        ({"filters": ["-L", "disc"]}, "a filtered run (-L disc)"),
        ({"exit_code": 1}, "run failed (exit 1)"),
        ({"ran": ["configure", "build"]}, "no test run"),
        ({"tests": None}, "no test run"),
        ({"tests": {"total": 0, "passed": 0, "failed": 0, "skipped": 0, "skipped_disc": 0}}, "no tests ran"),
        ({"tests": {"total": 5, "passed": 4, "failed": 1, "skipped": 0, "skipped_disc": 0}}, "1 test failed"),
        (
            {"tests": {"total": 50, "passed": 9, "failed": 0, "skipped": 41, "skipped_disc": 41}},
            "41 disc tests skipped",
        ),
        ({"dirty": True}, "made from a worktree with uncommitted changes"),
        ({"tip": "cd" * 20}, "is for another commit"),
    ],
)
def test_each_way_a_record_is_not_a_whole_run(change: dict[str, Any], text: str) -> None:
    found = build_record.problems(make(**change), tip=TIP, preset="ci")
    assert any(text in line for line in found), found


def test_every_problem_is_reported_not_the_first() -> None:
    found = build_record.problems(make(preset="dev", dirty=True, exit_code=1), tip=TIP, preset="ci")
    assert len(found) >= 3


def test_the_log_is_checked_against_its_hash_when_it_still_exists(tmp_path: Path) -> None:
    log = tmp_path / "build.log"
    log.write_text("all fine\n", encoding="utf-8")
    good = make(log=str(log), log_sha256=hashlib.sha256(log.read_bytes()).hexdigest())
    assert build_record.problems(good, tip=TIP, preset="ci") == []
    log.write_text("edited\n", encoding="utf-8")
    assert any("log" in line for line in build_record.problems(good, tip=TIP, preset="ci"))
    log.unlink()
    assert build_record.problems(good, tip=TIP, preset="ci") == []  # a deleted log is only a missing log


def test_a_record_of_another_version_is_refused() -> None:
    record = make()
    record["record_version"] = 99
    assert any("version" in line for line in build_record.problems(record, tip=TIP, preset="ci"))


def test_the_record_holds_the_revision_of_the_tool() -> None:
    assert make(tool_revision="ef" * 20)["tool_revision"] == "ef" * 20
    assert make()["tool_revision"] is None


def test_prune_keeps_the_newest_records_and_leaves_other_files(tmp_path: Path) -> None:
    import os

    names = [f"{number:064x}.json" for number in range(5)]
    for number, name in enumerate(names):
        (tmp_path / name).write_text("{}", encoding="utf-8")
        os.utime(tmp_path / name, (1_000 + number, 1_000 + number))
    (tmp_path / "keep.txt").write_text("x", encoding="utf-8")
    build_record.prune(tmp_path, 2)
    assert sorted(p.name for p in tmp_path.iterdir()) == [*names[3:], "keep.txt"]


def test_a_record_covered_by_a_stamp_of_any_task_is_never_pruned(tmp_path: Path) -> None:
    import os

    scratch = tmp_path / "scratch"
    folder = scratch / "one" / "build-runs"
    folder.mkdir(parents=True)
    names = [f"{number:064x}" for number in range(5)]
    for number, name in enumerate(names):
        (folder / f"{name}.json").write_text("{}", encoding="utf-8")
        os.utime(folder / f"{name}.json", (1_000 + number, 1_000 + number))
    other = scratch / "two" / "stamps"
    other.mkdir(parents=True)
    line = '{"covers":{"build_record_sha256":"' + names[0] + '","diff_sha256":"ab"},"step":"checked"}\n'
    (other / "0001.jsonl").write_text(line, encoding="utf-8")
    assert build_record.covered(scratch) == {names[0]}
    build_record.prune(folder, 2, build_record.covered(scratch))
    assert sorted(p.stem for p in folder.glob("*.json")) == [names[0], names[3], names[4]]
