# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the background task outputs of Claude Code sessions that `workspace clean` reports and removes
(coney_tools.task_outputs): `<temp>/claude/<hash>/<session>/tasks/*.output`, never a live session's (issue 1141)."""

import os
import shutil
import time
from collections import namedtuple
from pathlib import Path

import pytest
from test_workspace_clean import Space
from test_workspace_clean_runs import machine as machine

from coney_tools import task_outputs
from coney_tools.workspace_clean import run_clean, run_disk

HOUR = 3600.0
Usage = namedtuple("Usage", "total used free")


def session(
    root: Path, name: str, outputs: dict[str, int], age_hours: float, scratchpad_age: float | None = None
) -> Path:
    """A session folder `<root>/claude/h/<name>` with the `outputs` (file name to bytes), all last written
    `age_hours` ago, and a scratchpad whose file is `scratchpad_age` old when given."""
    folder = root / "claude" / "h" / name
    (folder / "tasks").mkdir(parents=True)
    when = time.time() - age_hours * HOUR
    for file, size in outputs.items():
        path = folder / "tasks" / file
        path.write_bytes(b"x" * size)
        os.utime(path, (when, when))
    if scratchpad_age is not None:
        (folder / "scratchpad").mkdir()
        note = folder / "scratchpad" / "note.txt"
        note.write_text("n", encoding="utf-8")
        moment = time.time() - scratchpad_age * HOUR
        os.utime(note, (moment, moment))
        os.utime(folder / "scratchpad", (moment, moment))
    # The folders were made now: a folder's own time counts as a write in it, so give them the outputs' age.
    for path in (folder / "tasks", folder):
        os.utime(path, (when, when) if scratchpad_age is None else (min(when, moment), min(when, moment)))
    return folder


def test_a_session_idle_for_longer_than_the_limit_is_stale_and_a_recent_one_is_live(tmp_path: Path) -> None:
    old = session(tmp_path, "old", {"a.output": 1000, "b.output": 500}, age_hours=100)
    new = session(tmp_path, "new", {"c.output": 700}, age_hours=1)
    found = {s.path: s for s in task_outputs.survey(tmp_path / "claude", time.time(), stale_hours=72)}
    assert found[old].stale and found[old].bytes == 1500 and found[old].files == 2
    assert not found[new].stale and found[new].bytes == 700


def test_a_scratchpad_touched_recently_keeps_the_session_live(tmp_path: Path) -> None:
    kept = session(tmp_path, "busy", {"a.output": 10}, age_hours=200, scratchpad_age=2)
    [found] = task_outputs.survey(tmp_path / "claude", time.time(), stale_hours=72)
    assert found.path == kept and not found.stale


def test_removing_stale_outputs_never_touches_a_live_session_or_a_scratchpad(tmp_path: Path) -> None:
    old = session(tmp_path, "old", {"a.output": 1000}, age_hours=100, scratchpad_age=100)
    new = session(tmp_path, "new", {"c.output": 700}, age_hours=1)
    sessions = task_outputs.survey(tmp_path / "claude", time.time(), stale_hours=72)
    freed, failures = task_outputs.remove_stale(sessions, time.time(), 72)
    assert (freed, failures) == (1000, [])
    assert not (old / "tasks").exists() or not list((old / "tasks").iterdir())
    assert (old / "scratchpad" / "note.txt").is_file()
    assert (new / "tasks" / "c.output").is_file()


def test_a_session_that_turns_live_between_the_survey_and_the_removal_is_left(tmp_path: Path) -> None:
    old = session(tmp_path, "old", {"a.output": 1000}, age_hours=100)
    sessions = task_outputs.survey(tmp_path / "claude", time.time(), stale_hours=72)
    (old / "tasks" / "later.output").write_bytes(b"y")  # the session writes again
    freed, failures = task_outputs.remove_stale(sessions, time.time(), stale_hours=72)
    assert freed == 0 and failures == [] and (old / "tasks" / "a.output").is_file()


def test_no_claude_folder_names_nothing(tmp_path: Path) -> None:
    assert task_outputs.survey(tmp_path / "claude", time.time(), stale_hours=72) == []


def test_a_session_folder_without_tasks_is_skipped(tmp_path: Path) -> None:
    (tmp_path / "claude" / "h" / "s" / "scratchpad").mkdir(parents=True)
    (tmp_path / "claude" / "stray.txt").parent.mkdir(exist_ok=True)
    (tmp_path / "claude" / "stray.txt").write_text("x", encoding="utf-8")
    assert task_outputs.survey(tmp_path / "claude", time.time(), stale_hours=72) == []


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path / "ws")


def test_clean_reports_sizes_and_removes_only_stale_outputs_with_apply(
    space: Space, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    temp = tmp_path / "temp"
    old = session(temp, "stale-session", {"big.output": 5000}, age_hours=100)
    new = session(temp, "live-session", {"a.output": 300}, age_hours=0.5)
    assert run_clean(space.checkout, space.root, False, merged_heads=lambda b: None, claude_temp=temp / "claude") == 0
    out = capsys.readouterr().out
    assert "Background task outputs" in out and "stale-session" in out and "would remove" in out
    assert "live-session" in out and "live, kept" in out
    assert (old / "tasks" / "big.output").is_file()  # a dry run changes nothing
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda b: None, claude_temp=temp / "claude") == 0
    assert not (old / "tasks" / "big.output").exists()
    assert (new / "tasks" / "a.output").is_file()


def test_a_live_session_over_the_warning_size_is_reported_not_removed(tmp_path: Path) -> None:
    live = session(tmp_path, "huge", {"x.output": 2048}, age_hours=0.1)
    sessions = task_outputs.survey(tmp_path / "claude", time.time(), stale_hours=72)
    text = " | ".join(task_outputs.lines(sessions, apply=True, warn_bytes=1000))
    assert "huge" in text and "live, kept; over" in text
    assert task_outputs.remove_stale(sessions, time.time(), 72) == (0, [])
    assert (live / "tasks" / "x.output").is_file()
    assert task_outputs.summary(sessions, warn_bytes=1000) is not None
    assert task_outputs.summary(sessions, warn_bytes=10**9) is None


def test_check_space_tells_what_the_stale_outputs_hold(
    space: Space, tmp_path: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    temp = tmp_path / "temp"
    session(temp, "stale-session", {"big.output": 5000}, age_hours=100)
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * 1024**3, 0, 500 * 1024**3))
    assert run_disk(space.checkout, space.root, claude_temp=temp / "claude") == 0
    assert "stale background task output" in capsys.readouterr().out
