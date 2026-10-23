# SPDX-License-Identifier: GPL-3.0-or-later
"""The stamp log: an append-only, hash-chained record of the proof a unit of work carries (docs/guides/stamps.md).

A stamp says that a step ended, with what result, on which commit, covering which hashes, run by whom, when, and what
the stamp before it hashed to. The stage commands (`original study`, `original compare`, `original fails-first`, later
`task check`, `task review` and `pr open`) are the only writers: they call `append`, which takes the log's lock, checks
that the chain is intact and writes one line. `verify` re-reads the log and reports every problem; `pr open` and a CI
check will call it (neither does yet).

The log is a folder of segments `0001.jsonl`, `0002.jsonl`, ... of canonical JSON lines (UTF-8, sorted keys, no
spaces). A stamp's *body* is its canonical JSON without `sig`; its *hash* is the SHA-256 of the body; `prev` is the
previous body's hash, across segments. A signature signs the body, so it covers the whole earlier history, and being
outside the body it may be added without breaking the chain; the steps in `stamps.signed` must carry a valid one for a
release.

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
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import identity, stamp_sign
from coney_tools.config import ConfigError
from coney_tools.conventions import Stamps

#: The `prev` of the first stamp.
ZERO = "0" * 64
#: The format version of a stamp.
VERSION = 1
#: A stamp's keys, exactly.
FIELDS = ("v", "seq", "task", "step", "subject", "result", "commit", "covers", "detail", "who", "time", "prev", "sig")
#: The biggest `detail`, canonical bytes (a stamp stays under about a kilobyte).
MAX_DETAIL = 2048
_SEGMENT = re.compile(r"^(\d{4})\.jsonl$")
HEX = re.compile(r"[0-9a-f]{40}|[0-9a-f]{64}")
_TIME = "%Y-%m-%dT%H:%M:%SZ"
#: Signs a stamp's body: returns the `sig` object, or raises stamp_sign.SignError.
Sign = Callable[[bytes], dict[str, Any]]
#: Checks a signature: the principal of the signers file, or raises stamp_sign.SignError.
Verifier = Callable[[bytes, str, datetime], str]


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
        """The canonical JSON without the signature: what is hashed and signed."""
        return canonical({k: v for k, v in self.data.items() if k != "sig"})

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

    def latest(self, step: str, subject: str | None = None, result: str | None = None) -> Stamp | None:
        """The newest stamp of `step` (and `subject`, and `result`, when given), or None."""
        for stamp in reversed(self.stamps):
            if (
                stamp.step == step
                and (subject is None or stamp.subject == subject)
                and (result is None or stamp.result == result)
            ):
                return stamp
        return None

    def current(self, step: str, subject: str, covers: Mapping[str, str]) -> Stamp | None:
        """The newest passing stamp of `step` and `subject` when it covers exactly these hashes (the ones the caller
        recomputed for the key names given), else None."""
        stamp = self.latest(step, subject)  # the newest of any result: a later fail replaces an earlier pass
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
    """Parse every segment and check the chain, without signatures."""
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
    if not isinstance(data, dict) or set(data) != set(FIELDS):
        report.problems.append(Problem("error", where, f"a stamp has exactly the keys {', '.join(FIELDS)}"))
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


def verify(
    folder: Path,
    rules: Stamps,
    *,
    signers: Path | None = None,
    require_signatures: bool = False,
    verifier: Verifier | None = None,
    task: str | None = None,
) -> Report:
    """Read the log in `folder` and report every problem: the chain, each field, and the signatures.

    A signature that is present is always checked (with `verifier`, else ssh-keygen against `signers`). A stamp of a
    signed step (`rules.signed`) without one is a warning, or an error with `require_signatures` (what `pr open` and
    CI will use), which also refuses a missing or empty log. With `task`, a log of another task is an error. A
    `pr-opened` stamp must name the stamp before it as `chain_head`.
    """
    report = _read(folder, rules)
    if require_signatures and not report.stamps:
        report.problems.append(Problem("error", str(folder), "no stamps: a release needs the unit's log"))
    if task is not None and report.stamps and report.stamps[0].task != task:
        report.problems.append(
            Problem("error", str(folder), f"the log is task {report.stamps[0].task!r}'s, not {task!r}'s")
        )
    check = verifier or _ssh_verifier(signers, rules.namespace)
    for index, stamp in enumerate(report.stamps):
        where = f"{folder.name}: seq {stamp.seq}"
        if stamp.step == "pr-opened":
            before = report.stamps[index - 1].hash if index else ZERO
            if stamp.covers.get("chain_head") != before:
                report.problems.append(Problem("error", where, "pr-opened: chain_head is not the stamp before it"))
        sig = stamp.sig
        if sig is None:
            if stamp.step in rules.signed:
                severity = "error" if require_signatures else "warning"
                report.problems.append(Problem(severity, where, f"{stamp.step}: unsigned"))
            continue
        if check is None:
            report.problems.append(Problem("error", where, "signed, but there is no signers file to check it against"))
            continue
        try:
            check(stamp.body, str(sig.get("armor", "")) if isinstance(sig, dict) else "", stamp.moment)
        except stamp_sign.SignError as error:
            report.problems.append(Problem("error", where, f"{stamp.step}: signature: {error}"))
    return report


def _ssh_verifier(signers: Path | None, namespace: str) -> Verifier | None:
    """A Verifier that asks ssh-keygen about `signers`, or None without a signers file."""
    if signers is None:
        return None

    def check(body: bytes, armor: str, when: datetime) -> str:
        return stamp_sign.verify(body, armor, signers, namespace, when)

    return check


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
    sign: Sign | None = None,
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
    return append_many(folder, rules=rules, task=task, entries=[entry], sign=sign, clock=clock)[0]


def append_many(
    folder: Path,
    *,
    rules: Stamps,
    task: str,
    entries: Sequence[Mapping[str, Any]],
    sign: Sign | None = None,
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> list[Stamp]:
    """Add the stamps `entries` describe (each a mapping of step, subject, result, commit, covers, detail, who) to the
    log in `folder` as one write: all of them or none.

    Refuses (StampError, nothing written) when a field is not allowed, when the log is not intact, or when `sign` fails
    for any of them. Under the lock it assigns `seq` and `prev`, never lets `time` go backwards, and for `pr-opened`
    fills `chain_head`. `sign` is given for the signed steps when a key is configured; without it the stamps are
    unsigned.
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
                "sig": None,
            }
            stamp = Stamp(data)
            if sign is not None:
                try:
                    data["sig"] = sign(stamp.body)
                except stamp_sign.SignError as error:
                    raise StampError(f"the {entry['step']} stamp could not be signed: {error}") from error
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
