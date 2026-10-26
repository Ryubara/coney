# SPDX-License-Identifier: GPL-3.0-or-later
"""The stamp log: an append-only, hash-chained record of the proof a unit of work carries (docs/guides/stamps.md).

A stamp says that a step ended, with what result, on which commit, covering which hashes, run by whom, when, and what
the stamp before it hashed to. The stage commands (`original study`, `original compare` and `original fails-first`
today; `task check`, `task review` and `pr open` as they land) are the only writers: they call `append`, which takes
the log's lock, checks that the chain is intact and writes one line. `verify` re-reads the log and reports every
problem.

The log is a folder of segments `0001.jsonl`, `0002.jsonl`, ... of canonical JSON lines (UTF-8, sorted keys, no
spaces). A stamp's *hash* is the SHA-256 of its line without a `sig` key; `prev` is the previous stamp's hash, across
segments. Stamps are not signed; a stamp written while they were carries a `sig` key, which is read, left out of the
hash and ignored.

The steps that say a whole change is good (`checked`, `reviewed`, `pr-opened`) are bound to the *change* the branch
makes, not to a commit: `covers.diff_sha256` (`CHANGE`, computed by `pr_diff`), which a clean rebase keeps and any
edit of the branch's own lines changes. `commit` stays in each stamp as a record.

A stamp holds hashes, names and counts only: no text from the original, no decompile line, no test output (LEGAL.md).
"""

from __future__ import annotations

import contextlib
import hashlib
import json
import os
import re
import sys
import time
from collections.abc import Callable, Collection, Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import identity
from coney_tools.config import ConfigError
from coney_tools.conventions import Stamps

#: The `prev` of the first stamp.
ZERO = "0" * 64
#: The format version of a stamp.
VERSION = 1
#: A stamp's keys, exactly. A stamp of the time they were signed has one more, `sig`, which is ignored (`LEGACY_KEY`).
FIELDS = ("v", "seq", "task", "step", "subject", "result", "commit", "covers", "detail", "who", "time", "prev")
#: The key an older stamp may carry: `null`, or `{"signer", "armor"}`. Read, kept in the stamp's line, never hashed.
LEGACY_KEY = "sig"
#: The `covers` key that binds a stamp to the change a branch makes (`pr_diff.change_of(...).sha256`).
CHANGE = "diff_sha256"
#: The `detail` key in which a `compared` stamp names the Coney function it was made for (`orig_review.Unit.key`).
FUNCTION = "coney"
#: The biggest `detail`, canonical bytes (a stamp stays under about a kilobyte).
MAX_DETAIL = 2048
_SEGMENT = re.compile(r"^(\d{4})\.jsonl$")
HEX = re.compile(r"[0-9a-f]{40}|[0-9a-f]{64}")
_TIME = "%Y-%m-%dT%H:%M:%SZ"


class StampError(ConfigError):
    """A stamp cannot be written, or the log cannot be trusted."""


# --- records ----------------------------------------------------------------------------------------------------


def _check_plain(value: object, where: str) -> None:
    """Refuse anything but str, int, bool, None, lists and str-keyed dicts of them (no floats: canonical form)."""
    if isinstance(value, float):
        raise StampError(f"{where}: a float is not allowed in a stamp")
    if isinstance(value, list):
        for index, item in enumerate(value):
            _check_plain(item, f"{where}[{index}]")
    elif isinstance(value, dict):
        for key, item in value.items():
            if not isinstance(key, str):
                raise StampError(f"{where}: keys must be text")
            _check_plain(item, f"{where}.{key}")
    elif not (value is None or isinstance(value, str | int | bool)):
        raise StampError(f"{where}: {type(value).__name__} is not allowed in a stamp")


def canonical(value: object) -> bytes:
    """`value` as canonical JSON bytes: UTF-8 (ASCII), sorted keys, no spaces; no floats."""
    _check_plain(value, "value")
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")


def sha256_hex(data: bytes) -> str:
    """The SHA-256 of `data`, as hex."""
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class Stamp:
    """One stamp: the record as written (see FIELDS)."""

    data: Mapping[str, Any]

    def __getattr__(self, name: str) -> Any:
        """The fields as attributes: `stamp.step`, `stamp.covers`."""
        try:
            return self.data[name]
        except KeyError:
            raise AttributeError(name) from None

    @property
    def body(self) -> bytes:
        """The canonical JSON without an old stamp's `sig`: what is hashed."""
        return canonical({k: v for k, v in self.data.items() if k != LEGACY_KEY})

    @property
    def hash(self) -> str:
        """The SHA-256 of the body: the next stamp's `prev`."""
        return sha256_hex(self.body)

    @property
    def line(self) -> str:
        """The stamp as a log line (canonical JSON, no newline)."""
        return canonical(dict(self.data)).decode("utf-8")

    @property
    def moment(self) -> datetime:
        """`time` as an aware UTC datetime."""
        return datetime.strptime(self.data["time"], _TIME).replace(tzinfo=UTC)


def _check_fields(
    rules: Stamps,
    task: str,
    step: str,
    subject: object,
    result: str,
    commit: str,
    covers: object,
    detail: object,
    who: object,
) -> None:
    """Refuse a stamp that the rules or the shape do not allow (the same checks on writing and on reading)."""
    if not task or not isinstance(task, str):
        raise StampError("task: expected the task's name")
    if step not in rules.steps:
        raise StampError(f"step {step!r} is not one of: {', '.join(rules.steps)}")
    if not isinstance(subject, str):
        raise StampError("subject must be text (an address, a test name, or empty)")
    if result not in rules.results:
        raise StampError(f"result {result!r} is not one of: {', '.join(rules.results)}")
    if commit and not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise StampError("commit must be a full 40-digit hash (or empty)")
    if not isinstance(covers, dict) or not all(
        isinstance(k, str) and isinstance(v, str) and HEX.fullmatch(v) for k, v in covers.items()
    ):
        raise StampError("covers must map names to hex hashes (SHA-256, or a commit)")
    if not isinstance(who, dict) or not who.get("kind"):
        raise StampError("who must say what ran the step (kind)")
    _check_plain(detail, "detail")
    _check_plain(who, "who")
    if not isinstance(detail, dict) or len(canonical(detail)) > MAX_DETAIL:
        raise StampError(f"detail must be a small mapping (at most {MAX_DETAIL} bytes)")


def who_from_env(tool: str, env: Mapping[str, str] | None = None, folder: Path | None = None) -> dict[str, Any]:
    """The `who` of a stamp written by `tool`: the agent the hook vouched for, and where that came from.

    The hook dispatcher puts a random `CONEY_AGENT_TOKEN` in front of every `coney-tools` command and writes the
    caller's identity, from the hook input and the agent's transcript, to the file that token names
    (`coney_tools.identity`, docs/guides/agents.md#identity). The source is "hook" only when that file is found and
    valid; otherwise it is "unverified" with every field "unknown", whatever the environment says about the agent:
    a typed or changed token finds no file, so it can never pass for another role. With no `env` the identity read at
    the start of the process is used. This never refuses.
    """
    found = identity.current() if env is None else identity.load(env, folder)
    return {"kind": "tool", "tool": tool, "agent": found.agent(), "source": "hook" if found.verified else "unverified"}


# --- reading ----------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Problem:
    """Something wrong or doubtful about a log."""

    severity: str  # "error" or "warning"
    where: str  # "0001.jsonl:3" or the folder
    text: str

    def __str__(self) -> str:
        return f"{self.severity}: {self.where}: {self.text}"


@dataclass
class Report:
    """What `verify` found."""

    stamps: list[Stamp] = field(default_factory=list)
    problems: list[Problem] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        """True when there is no error (warnings may remain)."""
        return not any(p.severity == "error" for p in self.problems)

    @property
    def head(self) -> str:
        """The hash of the newest stamp, ZERO for an empty log."""
        return self.stamps[-1].hash if self.stamps else ZERO

    def latest(
        self,
        step: str,
        subject: str | None = None,
        result: str | None = None,
        *,
        function: str | None = None,
        cited_by: Collection[str] = (),
    ) -> Stamp | None:
        """The newest stamp of `step` (and `subject`, and `result`, when given), or None.

        Two Coney functions can port parts of one original function and so cite one address, which is a `compared`
        stamp's subject. Given the Coney function the caller asks for and every function that cites the address
        (`cited_by`), only the stamps that name `function` (`FUNCTION` in their detail) count while another function
        also cites the address; with `function` alone at the address every stamp of it counts, as a stamp made before
        it was named does, so comparing one function never replaces the stamp of the other.
        """
        shared = function is not None and any(other != function for other in cited_by)
        for stamp in reversed(self.stamps):
            if (
                stamp.step == step
                and (subject is None or stamp.subject == subject)
                and (result is None or stamp.result == result)
                and (not shared or stamp.detail.get(FUNCTION) == function)
            ):
                return stamp
        return None

    def current(
        self,
        step: str,
        subject: str,
        covers: Mapping[str, str],
        *,
        function: str | None = None,
        cited_by: Collection[str] = (),
    ) -> Stamp | None:
        """The newest passing stamp of `step` and `subject` when it covers exactly these hashes (the ones the caller
        recomputed for the key names given), else None. `function` and `cited_by` pick the stamps as `latest` does."""
        stamp = self.latest(step, subject, function=function, cited_by=cited_by)  # a later fail replaces a pass
        if stamp is None or stamp.result != "pass":
            return None
        if any(stamp.covers.get(name) != value for name, value in covers.items()):
            return None
        return stamp


def _segments(folder: Path) -> list[Path]:
    """The segment files of a log, in order."""
    if not folder.is_dir():
        return []
    return sorted(p for p in folder.iterdir() if _SEGMENT.match(p.name))


def _read(folder: Path, rules: Stamps) -> Report:
    """Parse every segment and check the chain."""
    report = Report()
    previous: Stamp | None = None
    task = ""
    expected_segment = 1
    for path in _segments(folder):
        number = int(_SEGMENT.match(path.name).group(1))  # type: ignore[union-attr]
        if number != expected_segment:
            report.problems.append(Problem("error", path.name, f"segment {expected_segment:04d}.jsonl is missing"))
        expected_segment = number + 1
        raw = path.read_bytes()
        if raw and not raw.endswith(b"\n"):
            report.problems.append(Problem("error", path.name, "the last line is not complete (a torn write?)"))
            raw = raw[: raw.rfind(b"\n") + 1]
        for index, row in enumerate(raw.split(b"\n")[:-1], start=1):
            where = f"{path.name}:{index}"
            stamp = _parse(row, where, rules, report)
            if stamp is None:
                continue
            _chain(stamp, previous, task, where, report)
            task = task or stamp.task
            report.stamps.append(stamp)
            previous = stamp
    return report


def _parse(row: bytes, where: str, rules: Stamps, report: Report) -> Stamp | None:
    """One log line as a Stamp, or None (the problem is reported)."""
    try:
        data = json.loads(row.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        report.problems.append(Problem("error", where, f"not valid JSON ({error})"))
        return None
    if not isinstance(data, dict) or set(data) - {LEGACY_KEY} != set(FIELDS):
        report.problems.append(Problem("error", where, f"a stamp has exactly the keys {', '.join(FIELDS)}"))
        return None
    if data.get(LEGACY_KEY) is not None and not isinstance(data[LEGACY_KEY], dict):
        report.problems.append(Problem("error", where, "sig: only null or an object (a signature of an older format)"))
        return None
    try:
        if canonical(data) != row:
            report.problems.append(Problem("error", where, "not in canonical form (edited by hand?)"))
            return None
    except StampError as error:
        report.problems.append(Problem("error", where, str(error)))
        return None
    if isinstance(data["v"], bool) or data["v"] != VERSION:
        report.problems.append(Problem("error", where, f"unknown version {data['v']!r} (this tool reads {VERSION})"))
        return None
    try:
        _check_fields(
            rules,
            data["task"],
            data["step"],
            data["subject"],
            data["result"],
            data["commit"],
            data["covers"],
            data["detail"],
            data["who"],
        )
        stamp = Stamp(data)
        _ = stamp.moment
        if (
            not isinstance(data["seq"], int)
            or isinstance(data["seq"], bool)
            or not re.fullmatch(r"[0-9a-f]{64}", str(data["prev"]))
        ):
            raise StampError("seq or prev is malformed")
    except (StampError, ValueError) as error:
        report.problems.append(Problem("error", where, str(error)))
        return None
    return stamp


def _chain(stamp: Stamp, previous: Stamp | None, task: str, where: str, report: Report) -> None:
    """Check `stamp` follows `previous`: sequence, link, time, task."""
    want_seq = previous.seq + 1 if previous else 1
    want_prev = previous.hash if previous else ZERO
    if stamp.seq != want_seq:
        report.problems.append(
            Problem("error", where, f"seq {stamp.seq} where {want_seq} is expected (a stamp is missing or moved)")
        )
    if stamp.prev != want_prev:
        report.problems.append(
            Problem("error", where, f"seq {stamp.seq}: prev does not match the stamp before it (the log was edited)")
        )
    if previous is not None and stamp.moment < previous.moment:
        report.problems.append(Problem("error", where, f"seq {stamp.seq}: time is earlier than the stamp before it"))
    if task and stamp.task != task:
        report.problems.append(Problem("error", where, f"seq {stamp.seq}: task {stamp.task!r}, the log is {task!r}'s"))


def verify(folder: Path, rules: Stamps, *, require_stamps: bool = False, task: str | None = None) -> Report:
    """Read the log in `folder` and report every problem: the chain and each field.

    With `require_stamps` a missing or empty log is an error (what a check that needs the unit's proof uses). With
    `task`, a log of another task is an error. A `pr-opened` stamp must name the stamp before it as `chain_head`.
    """
    report = _read(folder, rules)
    if require_stamps and not report.stamps:
        report.problems.append(Problem("error", str(folder), "no stamps: the unit has no log"))
    if task is not None and report.stamps and report.stamps[0].task != task:
        report.problems.append(
            Problem("error", str(folder), f"the log is task {report.stamps[0].task!r}'s, not {task!r}'s")
        )
    for index, stamp in enumerate(report.stamps):
        if stamp.step == "pr-opened":
            before = report.stamps[index - 1].hash if index else ZERO
            if stamp.covers.get("chain_head") != before:
                where = f"{folder.name}: seq {stamp.seq}"
                report.problems.append(Problem("error", where, "pr-opened: chain_head is not the stamp before it"))
    return report


# --- writing ----------------------------------------------------------------------------------------------------


@contextlib.contextmanager
def _locked(folder: Path) -> Iterator[None]:
    """Hold the log's lock (a file in the folder) for one append: other writers wait."""
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / ".lock").open("a+b") as handle:
        deadline = time.monotonic() + 60
        while True:
            try:
                if sys.platform == "win32":
                    import msvcrt

                    handle.seek(0)
                    msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl

                    fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise StampError(f"{folder}: the stamp log stayed locked for a minute") from None
                time.sleep(0.01)
        try:
            yield
        finally:
            if sys.platform == "win32":
                import msvcrt

                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def append(
    folder: Path,
    *,
    rules: Stamps,
    task: str,
    step: str,
    subject: str,
    result: str,
    commit: str,
    covers: Mapping[str, str],
    detail: Mapping[str, Any],
    who: Mapping[str, Any],
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> Stamp:
    """Add one stamp to the log in `folder` and return it (see `append_many`)."""
    entry = {
        "step": step,
        "subject": subject,
        "result": result,
        "commit": commit,
        "covers": covers,
        "detail": detail,
        "who": who,
    }
    return append_many(folder, rules=rules, task=task, entries=[entry], clock=clock)[0]


def append_many(
    folder: Path,
    *,
    rules: Stamps,
    task: str,
    entries: Sequence[Mapping[str, Any]],
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> list[Stamp]:
    """Add the stamps `entries` describe (each a mapping of step, subject, result, commit, covers, detail, who) to the
    log in `folder` as one write: all of them or none.

    Refuses (StampError, nothing written) when a field of any of them is not allowed or when the log is not intact.
    Under the lock it assigns `seq` and `prev`, never lets `time` go backwards, and for `pr-opened` fills `chain_head`.
    """
    for entry in entries:
        _check_fields(
            rules,
            task,
            entry["step"],
            entry["subject"],
            entry["result"],
            entry["commit"],
            dict(entry["covers"]),
            dict(entry["detail"]),
            dict(entry["who"]),
        )
    with _locked(folder):
        report = _read(folder, rules)
        if not report.ok:
            raise StampError(f"{folder}: the log is not intact, so nothing can be added: {report.problems[0]}")
        previous = report.stamps[-1] if report.stamps else None
        if previous is not None and previous.task != task:
            raise StampError(f"{folder}: the log is task {previous.task!r}'s, not {task!r}'s")
        written: list[Stamp] = []
        for entry in entries:
            moment = clock().astimezone(UTC)
            if previous is not None:
                moment = max(moment, previous.moment)
            covered = dict(entry["covers"])
            if entry["step"] == "pr-opened":
                covered["chain_head"] = previous.hash if previous else ZERO
            data: dict[str, Any] = {
                "v": VERSION,
                "seq": (previous.seq + 1) if previous else 1,
                "task": task,
                "step": entry["step"],
                "subject": entry["subject"],
                "result": entry["result"],
                "commit": entry["commit"],
                "covers": covered,
                "detail": dict(entry["detail"]),
                "who": dict(entry["who"]),
                "time": moment.strftime(_TIME),
                "prev": previous.hash if previous else ZERO,
            }
            stamp = Stamp(data)
            written.append(stamp)
            previous = stamp
        _write_lines(folder, "".join(stamp.line + "\n" for stamp in written), rules.segment_bytes)
    return written


def _write_lines(folder: Path, text: str, limit: int) -> None:
    """Append `text` (whole lines) to the newest segment, or to a new one when it would pass `limit` bytes."""
    segments = _segments(folder)
    path = segments[-1] if segments else folder / "0001.jsonl"
    data = text.encode("utf-8")
    if segments and path.stat().st_size + len(data) > limit:
        path = folder / f"{int(path.stem) + 1:04d}.jsonl"
    with path.open("ab") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
