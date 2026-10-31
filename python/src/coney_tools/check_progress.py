# SPDX-License-Identifier: GPL-3.0-or-later
"""The live state of a unit's `task check`, in one small file in the unit's scratch folder
(docs/guides/coney-tools-repo.md#check-progress).

A full check takes an hour and a queue of nine of them cannot be told apart from nine hung ones by their logs, so the
run writes where it is: which check of the list, what kind of work, how long, how far when that is known and, while it
waits, its place in the slot queue. `Progress` is the writer, one per `task check`, and its numbers are the ones the
progress lines of the log say (`Fraction.suffix` and the queue place come from the same values). `read_view` and `views`
are the reader, the one place the format is parsed: `coord status` shows what they return and Conclave's snapshot takes
it from `coord status` (docs/guides/conclave.md#checks), so no other reader parses the file.

The file is JSON, `FILE_NAME`, replaced whole each time (a reader never sees half of it). The writer updates it on every
change of state and on every `beat`, the heartbeat that also says the progress lines (live_progress.HEARTBEAT_SECONDS).
Its `state` is `waiting` (for a check slot or a build slot), `running`, then `passed` or `failed` at the end, or
`abandoned` when the run ended without a verdict (an error, or a wait that gave up). A run killed from outside cannot
say so itself, so the reader judges a `waiting` or `running` file by its process (gone) and by its age (`QUIET_SECONDS`
without a write) and reports it `abandoned`. A fraction that has not moved for `conclave.check_stall_minutes` marks a
running check `stalled`; a check with no fraction (ruff, mypy) is never stalled, because nothing says it should move.

A write that fails is ignored: a progress file never fails the run it reports on.
"""

from __future__ import annotations

import contextlib
import json
import os
import threading
from collections.abc import Callable, Sequence
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import pcsx2_proc

#: The file in a unit's scratch folder.
FILE_NAME = "check-progress.json"
#: The version of the record; a reader ignores a file of another one.
SCHEMA = 1
#: What the writer's `state` says, and what a reader may report on top of it (`abandoned` is also a reading).
WAITING = "waiting"
RUNNING = "running"
PASSED = "passed"
FAILED = "failed"
ABANDONED = "abandoned"
#: The states of a run that is going on.
LIVE_STATES = (WAITING, RUNNING)
#: A live file not written for this long (five of live_progress.HEARTBEAT_SECONDS) is abandoned, whatever its process:
#: a process id can be reused by an unrelated program.
QUIET_SECONDS = 300.0
#: The units of a fraction.
PERCENT = "%"
STEPS = "steps"
TESTS = "tests"


@dataclass(frozen=True)
class Fraction:
    """How far a check is: `done` of `of` (None when the total is not known) in `unit` (`%` for pytest's percent,
    `steps` for ninja's `[n/m]`, `tests` for ctest's `n/m`), and, for tests, how many of them failed."""

    done: int
    of: int | None
    unit: str
    failed: int | None = None

    @property
    def ratio(self) -> float | None:
        """The done part from 0 to 1, or None when the total is not known (or zero)."""
        return self.done / self.of if self.of else None

    def text(self) -> str:
        """The fraction in words, as the full form and the progress line say it: `64% done`, `ninja step 12 of 340`,
        `412 of 900 tests done, 3 failed`."""
        if self.unit == PERCENT:
            return f"{self.done}% done"
        if self.unit == STEPS:
            return f"ninja step {self.done} of {self.of}" if self.of else f"ninja step {self.done}"
        of = f" of {self.of}" if self.of else ""
        failed = f", {self.failed} failed" if self.failed is not None else ""
        return f"{self.done}{of} tests done{failed}"

    def suffix(self) -> str:
        """What a progress line of the log adds after its time (`, 64% done`)."""
        return f", {self.text()}"

    def short(self) -> str:
        """The fraction in the fewest characters: `64%`, `412/900`, `12/340` (just the count when the total is not
        known)."""
        if self.unit == PERCENT:
            return f"{self.done}%"
        return f"{self.done}/{self.of}" if self.of else str(self.done)


def _utc_now() -> datetime:
    """The current time, in UTC."""
    return datetime.now(UTC)


def _iso(moment: datetime) -> str:
    """A time as ISO 8601 UTC with seconds, `2026-10-31T12:00:00Z`."""
    return moment.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def _moment(value: object) -> datetime | None:
    """An ISO time as a datetime, or None when it is absent or malformed."""
    if not isinstance(value, str):
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


# --- the writer ------------------------------------------------------------------------------------------------------


class Progress:
    """The writer of one `task check` run's progress file. Created without a file it writes nothing (every method is
    then a no-op), so the code that reports progress never asks whether anyone is listening; `attach` gives it the
    file. Its methods may be called from any thread (the heartbeat's and the run's)."""

    def __init__(self, clock: Callable[[], datetime] = _utc_now, pid: int | None = None) -> None:
        self._clock = clock
        self._pid = os.getpid() if pid is None else pid
        self._lock = threading.RLock()
        self._path: Path | None = None
        self._record: dict[str, Any] = {}
        self._fraction: Fraction | None = None

    def attach(self, path: Path) -> None:
        """Start writing to `path` (its folder is made when needed): a fresh run, no check begun yet."""
        with self._lock:
            now = self._clock()
            self._path = path
            self._fraction = None
            self._record = {
                "schema": SCHEMA,
                "state": RUNNING,
                "pid": self._pid,
                "started": _iso(now),
                "check": "",
                "kind": "",
                "index": 0,
                "total": 0,
                "since": _iso(now),
                "moved": _iso(now),
                "queue": None,
            }

    def plan(self, total: int) -> None:
        """How many checks the run has (the `of` of `3 of 8`)."""
        self._change(total=total)

    def start(self, check: str, kind: str, index: int) -> None:
        """Check number `index` (counting from 1) begins: `check` is what the log calls it, `kind` the sort of work."""
        with self._lock:
            self._fraction = None
            now = _iso(self._clock())
            self._change(
                state=RUNNING, check=check, kind=kind, index=index, since=now, moved=now, queue=None, done=None
            )

    def set_kind(self, kind: str) -> None:
        """The work of the same check changes kind (a build run goes from `build` to `ctest`): its time and its
        fraction start again."""
        with self._lock:
            self._fraction = None
            now = _iso(self._clock())
            self._change(kind=kind, since=now, moved=now, done=None)

    def waiting(self, lane: str, place: int, total: int, minutes: float, held_by: Sequence[str]) -> None:
        """The check waits in the queue of `lane` (`check`, `main` or `fast`) at `place` of `total`, for about `minutes`
        more, behind `held_by`. Called at each report of the wait; the wait began at the first."""
        with self._lock:
            since = (
                self._record.get("since", _iso(self._clock()))
                if self._record.get("state") == WAITING
                else _iso(self._clock())
            )
            queue = {"lane": lane, "place": place, "total": total, "minutes": minutes, "held_by": list(held_by)}
            self._change(state=WAITING, queue=queue, since=since)

    def running(self) -> None:
        """The check has its slot and runs: the wait is over and its time starts."""
        with self._lock:
            if self._record.get("state") != WAITING:
                return
            now = _iso(self._clock())
            self._fraction = None
            self._change(state=RUNNING, queue=None, since=now, moved=now, done=None)

    def fraction(self, fraction: Fraction | None) -> None:
        """How far the running check is now. Held in memory until the next `beat` writes it; when the numbers differ
        from the last ones the check has moved."""
        if fraction is None:
            return
        with self._lock:
            if self._path is None or fraction == self._fraction:
                return
            self._fraction = fraction
            self._record["done"] = asdict(fraction)
            self._record["moved"] = _iso(self._clock())

    def beat(self) -> None:
        """Write the file as it stands: the heartbeat, so the file says the run is alive and how long it has run."""
        with self._lock:
            self._write()

    def finish(self, passed: bool) -> None:
        """The run ended with a verdict."""
        self._change(state=PASSED if passed else FAILED, queue=None)

    def abandon(self) -> None:
        """The run ended with no verdict (an error, an interrupt, a wait that gave up)."""
        self._change(state=ABANDONED, queue=None)

    def _change(self, **fields: Any) -> None:
        """Set fields of the record and write it."""
        with self._lock:
            if self._path is None:
                return
            self._record.update(fields)
            self._write()

    def _write(self) -> None:
        """Replace the file with the record, its update time and the seconds the current phase has lasted. A failure
        is ignored."""
        if self._path is None:
            return
        now = self._clock()
        since = _moment(self._record.get("since")) or now
        record = {**self._record, "updated": _iso(now), "elapsed": max(0, int((now - since).total_seconds()))}
        temporary = self._path.with_name(f"{self._path.name}.{os.getpid()}.tmp")
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8")
            temporary.replace(self._path)
        except OSError:
            with contextlib.suppress(OSError):
                temporary.unlink(missing_ok=True)


# --- the reader ------------------------------------------------------------------------------------------------------


def _short_time(seconds: float) -> str:
    """A duration in the fewest characters: `40s`, `50m`, `1h05m`."""
    if seconds < 60:
        return f"{int(seconds)}s"
    minutes = int(seconds // 60)
    return f"{minutes}m" if minutes < 60 else f"{minutes // 60}h{minutes % 60:02d}m"


def _long_time(seconds: float) -> str:
    """A duration for the full form, as the progress lines say it: `40 s`, `12 min`, `1 h 05 min`."""
    if seconds < 90:
        return f"{seconds:.0f} s"
    minutes = int(seconds // 60)
    return f"{minutes} min" if minutes < 60 else f"{minutes // 60} h {minutes % 60:02d} min"


def _fraction_of(raw: object) -> Fraction | None:
    """The fraction a record holds, or None when it holds none or a malformed one."""
    if not isinstance(raw, dict):
        return None
    done, of, unit, failed = raw.get("done"), raw.get("of"), raw.get("unit"), raw.get("failed")
    if not isinstance(done, int) or not isinstance(unit, str):
        return None
    return Fraction(done, of if isinstance(of, int) else None, unit, failed if isinstance(failed, int) else None)


def _number(value: object) -> int:
    """An integer field, 0 when it is anything else."""
    return value if isinstance(value, int) and not isinstance(value, bool) else 0


def _text_of(
    record: dict[str, Any], state: str, seconds: float, fraction: Fraction | None, why: str, idle: float
) -> str:
    """The full form of a reading: `check 6 of 8, pytest: running 50 min, 64% done`."""
    check = str(record.get("check") or "")
    kind = str(record.get("kind") or "")
    index, total = _number(record.get("index")), _number(record.get("total"))
    where = f"check {index} of {total}" if index else "starting"
    named = f", {check}" if check else ""
    head = f"{where}{named}:"
    detail = f" ({kind})" if kind and kind != check and state == RUNNING else ""
    done = f", {fraction.text()}" if fraction is not None else ""
    queue = record.get("queue") if isinstance(record.get("queue"), dict) else None
    if state == WAITING and queue is not None:
        slot = "check slot" if queue.get("lane") == "check" else "build slot"
        held = queue.get("held_by") if isinstance(queue.get("held_by"), list) else []
        by = f"; held by {', '.join(str(one) for one in held)}" if held else ""
        return (
            f"{head} waiting for a {slot}, place {_number(queue.get('place'))} of {_number(queue.get('total'))}, "
            f"about {float(queue.get('minutes') or 0):.0f} min{by}"
        )
    if state == WAITING:
        return f"{head} waiting for a slot"
    if state == RUNNING:
        stalled = f"; stalled: no progress for {_long_time(idle)}" if why == "stalled" else ""
        return f"{head} running {_long_time(seconds)}{detail}{done}{stalled}"
    if state == ABANDONED and record.get("state") in LIVE_STATES:
        # The reader's judgement of a run that could not say so itself.
        last = "waiting for a slot" if record.get("state") == WAITING else "running"
        last += f", {fraction.text()}" if fraction is not None else ""
        reason = "it stopped updating" if why == "quiet" else "its process is gone"
        return f"{head} abandoned, {reason} (last seen {last})"
    return f"{head} {state} after {_long_time(seconds)}"


def read_view(
    folder: Path,
    now: datetime,
    stall_minutes: float,
    alive: Callable[[int], bool] = pcsx2_proc.pid_alive,
) -> dict[str, Any] | None:
    """What the progress file in `folder` (a unit's scratch folder) says at `now`, or None when there is no file or it
    is not a record of this schema. The reading holds the fields of the file that readers show and adds:

    * `state`: the file's, except that a `waiting` or `running` file whose process is gone (`alive(pid)` is False) or
      that has not been written for `QUIET_SECONDS` is `abandoned`; `stored` is the file's own;
    * `elapsed`: seconds in the current phase (up to `now` for a live run, up to the last write for the rest), and
      `age`: seconds since the file was last written;
    * `fraction`: the done part from 0 to 1 when known, else None;
    * `idle`: seconds since the fraction last moved (None without a fraction);
    * `stalled`: a running check with a fraction that has not moved for `stall_minutes`;
    * `short`: the unit row's form (`pytest 64% 50m`, `queued 5/6 ~36m`) and `text`: the full one."""
    try:
        raw = json.loads((folder / FILE_NAME).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(raw, dict) or raw.get("schema") != SCHEMA:
        return None
    stored = str(raw.get("state") or "")
    updated = _moment(raw.get("updated")) or now
    since = _moment(raw.get("since")) or updated
    moved = _moment(raw.get("moved")) or since
    fraction = _fraction_of(raw.get("done"))
    state, why = stored, ""
    if stored in LIVE_STATES:
        pid = raw.get("pid")
        if isinstance(pid, int) and not alive(pid):
            state, why = ABANDONED, "gone"
        elif (now - updated).total_seconds() > QUIET_SECONDS:
            state, why = ABANDONED, "quiet"
    live = state in LIVE_STATES
    # A live run has gone on until `now`; a finished or abandoned one stopped at its last write.
    seconds = max(0.0, ((now if live else updated) - since).total_seconds())
    idle = max(0.0, (now - moved).total_seconds()) if live and fraction is not None else None
    stalled = state == RUNNING and idle is not None and idle >= stall_minutes * 60
    if stalled:
        why = "stalled"
    view = {key: raw.get(key) for key in ("pid", "started", "check", "kind", "index", "total", "since", "updated")}
    view.update(
        state=state,
        stored=stored,
        elapsed=int(seconds),
        age=int(max(0.0, (now - updated).total_seconds())),
        done=raw.get("done") if fraction is not None else None,
        fraction=fraction.ratio if fraction is not None else None,
        moved=raw.get("moved"),
        idle=int(idle) if idle is not None else None,
        stalled=stalled,
        queue=raw.get("queue") if isinstance(raw.get("queue"), dict) else None,
    )
    view["short"] = _short_of(view, state, seconds, fraction, stalled)
    view["text"] = _text_of(raw, state, seconds, fraction, why, idle or 0.0)
    return view


def _short_of(view: dict[str, Any], state: str, seconds: float, fraction: Fraction | None, stalled: bool) -> str:
    """The unit row's form of a reading: `pytest 64% 50m`, `... stalled`, `queued 5/6 ~36m`, `ruff passed`."""
    kind = str(view.get("kind") or view.get("check") or "check")
    queue = view.get("queue")
    if state == WAITING:
        if isinstance(queue, dict):
            place, total = _number(queue.get("place")), _number(queue.get("total"))
            return f"queued {place}/{total} ~{float(queue.get('minutes') or 0):.0f}m"
        return "queued"
    if state == RUNNING:
        parts = [
            kind,
            fraction.short() if fraction is not None else "",
            _short_time(seconds),
            "stalled" if stalled else "",
        ]
        return " ".join(part for part in parts if part)
    return f"{kind} {state}"


def views(
    scratch: Path,
    now: datetime,
    stall_minutes: float,
    alive: Callable[[int], bool] = pcsx2_proc.pid_alive,
) -> dict[str, dict[str, Any]]:
    """The reading (`read_view`) of every unit folder in `scratch` that has a progress file, by the folder's name.
    A scratch folder that does not exist has none."""
    found: dict[str, dict[str, Any]] = {}
    try:
        folders = sorted(path for path in scratch.iterdir() if path.is_dir())
    except OSError:
        return found
    for folder in folders:
        view = read_view(folder, now, stall_minutes, alive)
        if view is not None:
            found[folder.name] = view
    return found
