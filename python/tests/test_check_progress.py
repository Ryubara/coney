# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_progress.py: the live state of a unit's `task check` in one small file (waiting, running with and
without a fraction, finished, abandoned), the reader's view of it (stalled with a fake clock, a dead process), and the
short and full forms every reader shows. No real suite and no real time: the clock and the liveness test are fakes."""

import json
from collections.abc import Callable
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest

from coney_tools import check_progress, live_progress
from coney_tools.check_progress import (
    ABANDONED,
    FAILED,
    FILE_NAME,
    PASSED,
    RUNNING,
    WAITING,
    Fraction,
    Progress,
    read_view,
)

START = datetime(2026, 10, 31, 12, 0, 0, tzinfo=UTC)


class Clock:
    """A clock the test moves by hand."""

    def __init__(self) -> None:
        self.now = START

    def __call__(self) -> datetime:
        return self.now

    def move(self, **span: float) -> None:
        self.now += timedelta(**span)


@pytest.fixture
def clock() -> Clock:
    return Clock()


@pytest.fixture
def path(tmp_path: Path) -> Path:
    return tmp_path / "unit" / FILE_NAME


def attached(path: Path, clock: Clock, pid: int = 4242) -> Progress:
    progress = Progress(clock=clock, pid=pid)
    progress.attach(path)
    return progress


def stored(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))  # type: ignore[no-any-return]


def alive(*pids: int) -> Callable[[int], bool]:
    return lambda pid: pid in pids


def view(path: Path, clock: Clock, *, stall: float = 30, running: tuple[int, ...] = (4242,)) -> dict[str, Any]:
    found = read_view(path.parent, clock(), stall, alive=alive(*running))
    assert found is not None
    return found


# --- the writer ------------------------------------------------------------------------------------------------------


def test_a_progress_that_is_not_attached_writes_nothing(tmp_path: Path) -> None:
    progress = Progress()
    progress.plan(3)
    progress.start("pytest", "pytest", 1)
    progress.fraction(Fraction(1, 2, "%"))
    progress.beat()
    progress.finish(True)
    assert list(tmp_path.iterdir()) == []


def test_a_check_that_is_running_with_no_fraction(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("ruff", "ruff", 3)
    clock.move(minutes=2)
    progress.beat()
    record = stored(path)
    assert record["schema"] == check_progress.SCHEMA
    assert record["state"] == RUNNING and record["pid"] == 4242
    assert (record["check"], record["kind"], record["index"], record["total"]) == ("ruff", "ruff", 3, 8)
    assert record["done"] is None and record["queue"] is None
    assert record["elapsed"] == 120


def test_a_running_check_with_a_fraction_keeps_when_it_last_moved(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.fraction(Fraction(64, 100, "%"))
    clock.move(minutes=10)
    progress.fraction(Fraction(64, 100, "%"))  # the same value does not move it
    progress.beat()
    record = stored(path)
    assert record["done"] == {"done": 64, "of": 100, "unit": "%", "failed": None}
    assert record["moved"] == "2026-10-31T12:00:00Z"
    clock.move(minutes=1)
    progress.fraction(Fraction(65, 100, "%"))
    progress.beat()
    assert stored(path)["moved"] == "2026-10-31T12:11:00Z"


def test_a_check_waiting_for_a_slot_has_its_place_and_runs_when_it_gets_it(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.waiting("check", 5, 6, 36.0, ["alice", "bob"])
    clock.move(minutes=1)
    progress.waiting("check", 4, 6, 30.0, ["alice", "bob"])
    record = stored(path)
    assert record["state"] == WAITING
    assert record["queue"] == {"lane": "check", "place": 4, "total": 6, "minutes": 30.0, "held_by": ["alice", "bob"]}
    assert record["since"] == "2026-10-31T12:00:00Z"  # the wait began at the first report, not the last
    clock.move(minutes=29)
    progress.running()
    record = stored(path)
    assert record["state"] == RUNNING and record["queue"] is None
    assert record["since"] == "2026-10-31T12:30:00Z" and record["elapsed"] == 0


def test_a_new_kind_in_a_check_starts_its_fraction_again(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(9)
    progress.start("msvc-ci-build and ctest-full-disc", "build", 8)
    progress.fraction(Fraction(410, 1204, "steps"))
    clock.move(minutes=20)
    progress.set_kind("ctest")
    progress.beat()
    record = stored(path)
    assert record["kind"] == "ctest" and record["done"] is None
    assert record["since"] == "2026-10-31T12:20:00Z"


def test_a_finished_run_says_pass_or_fail(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(2)
    progress.start("ruff", "ruff", 1)
    progress.finish(True)
    assert stored(path)["state"] == PASSED
    failing = attached(path, clock)
    failing.plan(2)
    failing.start("ruff", "ruff", 1)
    failing.finish(False)
    assert stored(path)["state"] == FAILED


def test_an_abandoned_run_says_so(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(2)
    progress.start("ruff", "ruff", 1)
    progress.abandon()
    assert stored(path)["state"] == ABANDONED


def test_a_write_that_cannot_happen_never_fails_the_run(tmp_path: Path, clock: Clock) -> None:
    blocked = tmp_path / "blocked"
    blocked.write_text("a file where the folder should be", encoding="utf-8")
    progress = Progress(clock=clock)
    progress.attach(blocked / FILE_NAME)
    progress.plan(1)
    progress.start("ruff", "ruff", 1)
    progress.beat()
    progress.finish(True)


def test_the_quiet_limit_is_five_heartbeats() -> None:
    assert check_progress.QUIET_SECONDS == 5 * live_progress.HEARTBEAT_SECONDS


# --- the reader ------------------------------------------------------------------------------------------------------


def test_no_file_is_no_view(tmp_path: Path) -> None:
    assert read_view(tmp_path, START, 30) is None


def test_a_file_that_is_not_a_record_is_no_view(path: Path) -> None:
    path.parent.mkdir(parents=True)
    for text in ("", "not json", "[1, 2]", '{"schema": 99}'):
        path.write_text(text, encoding="utf-8")
        assert read_view(path.parent, START, 30) is None


def test_a_running_pytest_reads_as_short_and_full(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    clock.move(minutes=50)
    progress.fraction(Fraction(64, 100, "%"))  # it has just moved
    progress.beat()
    found = view(path, clock)
    assert found["state"] == RUNNING and found["stalled"] is False
    assert found["short"] == "pytest 64% 50m"
    assert found["text"] == "check 6 of 8, pytest: running 50 min, 64% done"
    assert found["elapsed"] == 3000


def test_a_check_with_no_fraction_has_only_its_time(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("mypy", "mypy", 4)
    clock.move(seconds=40)
    progress.beat()
    found = view(path, clock)
    assert found["short"] == "mypy 40s"
    assert found["text"] == "check 4 of 8, mypy: running 40 s"


def test_the_reader_counts_the_time_since_the_last_write_too(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(2)
    progress.start("mypy", "mypy", 1)
    progress.beat()
    clock.move(minutes=2)  # the file is two minutes old: the check has run that long
    assert view(path, clock)["short"] == "mypy 2m"


def test_a_fraction_that_has_not_moved_is_stalled(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.fraction(Fraction(64, 100, "%"))
    progress.beat()
    clock.move(minutes=29)
    progress.beat()
    assert view(path, clock)["stalled"] is False
    clock.move(minutes=1)
    progress.beat()
    found = view(path, clock)
    assert found["stalled"] is True
    assert found["short"] == "pytest 64% 30m stalled"
    assert found["text"] == "check 6 of 8, pytest: running 30 min, 64% done; stalled: no progress for 30 min"
    assert view(path, clock, stall=45)["stalled"] is False  # the limit is the caller's (conclave.check_stall_minutes)


def test_a_run_that_moves_again_is_not_stalled(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.fraction(Fraction(64, 100, "%"))
    clock.move(minutes=40)
    progress.fraction(Fraction(65, 100, "%"))
    progress.beat()
    assert view(path, clock)["stalled"] is False


def test_a_check_with_no_fraction_is_never_stalled(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("mypy", "mypy", 4)
    clock.move(hours=3)
    progress.beat()
    assert view(path, clock)["stalled"] is False


def test_a_waiting_check_reads_as_its_place(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.waiting("check", 5, 6, 36.0, ["alice"])
    found = view(path, clock)
    assert found["state"] == WAITING
    assert found["short"] == "queued 5/6 ~36m"
    assert found["text"] == "check 6 of 8, pytest: waiting for a check slot, place 5 of 6, about 36 min; held by alice"
    assert found["stalled"] is False


def test_a_build_slot_wait_says_a_build_slot(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(9)
    progress.start("msvc-ci-build and ctest-full-disc", "build", 8)
    progress.waiting("main", 2, 3, 12.0, [])
    found = view(path, clock)
    assert "waiting for a build slot, place 2 of 3, about 12 min" in str(found["text"])
    assert "held by" not in str(found["text"])


def test_the_forms_of_the_build_and_ctest_fractions(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(9)
    progress.start("msvc-ci-build and ctest-full-disc", "build", 8)
    progress.fraction(Fraction(410, 1204, "steps"))
    clock.move(minutes=9)
    progress.beat()
    found = view(path, clock)
    assert found["short"] == "build 410/1204 9m"
    assert found["text"] == (
        "check 8 of 9, msvc-ci-build and ctest-full-disc: running 9 min (build), ninja step 410 of 1204"
    )
    progress.set_kind("ctest")
    progress.fraction(Fraction(1310, 2400, "tests", failed=2))
    clock.move(minutes=22)
    progress.beat()
    found = view(path, clock)
    assert found["short"] == "ctest 1310/2400 22m"
    assert found["text"].endswith("running 22 min (ctest), 1310 of 2400 tests done, 2 failed")


def test_a_dead_process_is_abandoned_though_the_file_says_running(path: Path, clock: Clock) -> None:
    progress = attached(path, clock, pid=777)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.fraction(Fraction(64, 100, "%"))
    progress.beat()
    found = view(path, clock, running=())
    assert found["state"] == ABANDONED and found["stored"] == RUNNING
    assert found["short"] == "pytest abandoned"
    assert found["text"] == "check 6 of 8, pytest: abandoned, its process is gone (last seen running, 64% done)"
    assert found["stalled"] is False


def test_a_writer_that_went_quiet_is_abandoned_though_its_pid_lives(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.beat()
    clock.move(seconds=check_progress.QUIET_SECONDS + 1)  # a reused process id is no proof of a live run
    assert view(path, clock)["state"] == ABANDONED


def test_a_finished_run_reads_as_its_result(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(2)
    progress.start("ruff", "ruff", 2)
    clock.move(minutes=3)
    progress.finish(True)
    clock.move(hours=1)  # a finished file does not go quiet or its process dead
    found = view(path, clock, running=())
    assert found["state"] == PASSED
    assert found["short"] == "ruff passed"
    assert found["text"] == "check 2 of 2, ruff: passed after 3 min"
    progress.start("ruff", "ruff", 1)
    progress.finish(False)
    assert view(path, clock, running=())["short"] == "ruff failed"


def test_a_long_run_reads_in_hours(path: Path, clock: Clock) -> None:
    progress = attached(path, clock)
    progress.plan(8)
    progress.start("pytest", "pytest", 6)
    progress.fraction(Fraction(10, 100, "%"))
    clock.move(hours=1, minutes=5)
    progress.beat()
    found = view(path, clock, stall=600)
    assert found["short"] == "pytest 10% 1h05m"
    assert "running 1 h 05 min" in str(found["text"])


# --- the pieces ------------------------------------------------------------------------------------------------------


def test_a_fraction_is_the_same_numbers_in_a_progress_line_and_in_the_file() -> None:
    assert Fraction(45, 100, "%").suffix() == ", 45% done"
    assert Fraction(2, 2, "steps").suffix() == ", ninja step 2 of 2"
    assert Fraction(3, None, "tests", failed=0).suffix() == ", 3 tests done, 0 failed"
    assert Fraction(3, 10, "tests").suffix() == ", 3 of 10 tests done"
    assert Fraction(1, 100, "%").ratio == 0.01
    assert Fraction(1, 4, "steps").ratio == 0.25
    assert Fraction(3, None, "tests").ratio is None
    assert Fraction(3, 0, "steps").ratio is None


def test_the_view_of_a_unit_folder_is_found_by_its_name(tmp_path: Path, clock: Clock) -> None:
    progress = Progress(clock=clock, pid=1)
    progress.attach(tmp_path / "unit-a" / FILE_NAME)
    progress.plan(1)
    progress.start("ruff", "ruff", 1)
    progress.beat()
    assert check_progress.views(tmp_path, START, 30, alive=lambda pid: True).keys() == {"unit-a"}
    (tmp_path / "unit-b").mkdir()
    (tmp_path / "plain-file").write_text("x", encoding="utf-8")
    assert check_progress.views(tmp_path / "missing", START, 30) == {}
