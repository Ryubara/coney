# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the unit record `task start` writes (coney_tools.unit_record)."""

import json
from datetime import UTC, datetime
from pathlib import Path

import pytest

from coney_tools.config import ConfigError
from coney_tools.unit_record import UnitRecord, UnitRecordError, read_record, write_record

T0 = datetime(2026, 10, 23, 9, 30, 0, tzinfo=UTC)


def record(**changes: object) -> UnitRecord:
    """A started feature unit."""
    values: dict[str, object] = {
        "task": "10-fix-the-fight-goal",
        "kind": "feature",
        "issues": (10, 11),
        "claimant": "Ryubara",
        "branch": "10-fix-the-fight-goal",
        "worktree": Path("/ws/worktrees/coney/10-fix-the-fight-goal"),
        "base_ref": "origin/main",
        "base_commit": "a" * 40,
        "started": T0,
    }
    values.update(changes)
    return UnitRecord(**values)  # type: ignore[arg-type]


def test_a_record_survives_its_file(tmp_path: Path) -> None:
    path = tmp_path / "unit.json"
    write_record(path, record())
    assert read_record(path) == record()


def test_the_file_is_plain_json_with_a_version_and_posix_paths(tmp_path: Path) -> None:
    path = tmp_path / "unit.json"
    write_record(path, record(worktree=Path("C:/ws/worktrees/coney/x")))
    data = json.loads(path.read_text(encoding="utf-8"))
    assert data["v"] == 1 and data["issues"] == [10, 11] and data["started"] == "2026-10-23T09:30:00Z"
    assert "\\" not in data["worktree"]
    assert data["parent"] == "" and data["progress_posted"] is False


def test_writing_replaces_the_file_whole(tmp_path: Path) -> None:
    path = tmp_path / "unit.json"
    write_record(path, record())
    write_record(path, record(progress_posted=True))
    assert read_record(path).progress_posted is True
    assert [p.name for p in tmp_path.iterdir()] == ["unit.json"]  # no temporary file is left


def test_a_fix_unit_logs_in_its_parents_stamp_log() -> None:
    assert record().log_task == "10-fix-the-fight-goal"
    fix = record(task="10-fix", kind="fix", parent="10-fix-the-fight-goal")
    assert fix.log_task == "10-fix-the-fight-goal"


@pytest.mark.parametrize(
    ("text", "why"),
    [
        ("not json", "not valid JSON"),
        ("[]", "not a JSON object"),
        ('{"v": 2}', "version 2"),
        ('{"v": 1, "task": "x"}', "has no 'kind'"),
    ],
)
def test_a_damaged_record_names_the_problem(tmp_path: Path, text: str, why: str) -> None:
    path = tmp_path / "unit.json"
    path.write_text(text, encoding="utf-8")
    with pytest.raises(UnitRecordError, match=why) as caught:
        read_record(path)
    assert isinstance(caught.value, ConfigError) and str(path) in str(caught.value)


def test_a_wrong_value_is_refused(tmp_path: Path) -> None:
    path = tmp_path / "unit.json"
    write_record(path, record())
    data = json.loads(path.read_text(encoding="utf-8"))
    data["started"] = "yesterday"
    path.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(UnitRecordError, match="wrong value"):
        read_record(path)
