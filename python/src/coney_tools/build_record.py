# SPDX-License-Identifier: GPL-3.0-or-later
"""The build record: what a run of `coney-tools build` leaves behind so that `task check` and `pr verify` can read
what happened instead of parsing printed text (docs/guides/stamps.md#build-record).

At the end of every run that configured, built or tested, `coney-tools build` writes one JSON file to
`<scratch>/<task>/build-runs/<sha256>.json`, named by the SHA-256 of its bytes (canonical JSON, like a stamp's line), so
the `checked` stamp can cover it by that hash and a changed record no longer reads. The record holds the tip it was
built from and whether the worktree was clean, the preset, the test filters, the command line, the exit code, the test
counts (with the skipped disc tests apart), the hash of the log and the times. It holds no game data and no test output.
Only `coney-tools build` writes it, and the hooks keep an agent from writing it by hand; neither makes forgery by the
person at the machine impossible. Standard library, besides the stamp log's canonical form.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError
from coney_tools.stamps import canonical

#: The folder of the task's scratch folder that holds its records.
FOLDER = "build-runs"
#: The format version of a record.
RECORD_VERSION = 1
_NAME = re.compile(r"^[0-9a-f]{64}$")


class BuildRecordError(ConfigError):
    """A build record that is missing, unreadable or does not hash to its name."""


def make(
    *,
    task: str,
    agent: str,
    tip: str,
    dirty: bool,
    preset: str,
    filters: Sequence[str],
    command_line: Sequence[str],
    exit_code: int,
    ran: Sequence[str],
    tests: Mapping[str, int] | None,
    disc: str | None,
    log_sha256: str,
    started: str,
    ended: str,
    log: str = "",
) -> dict[str, Any]:
    """A record from what a run knows. `ran` names the steps that started; `tests` is total, passed, failed, skipped
    and skipped_disc (None when no test run ended); `disc` says where the disc came from (None: there was none)."""
    return {
        "record_version": RECORD_VERSION,
        "task": task,
        "agent": agent,
        "tip": tip,
        "dirty": dirty,
        "preset": preset,
        "filters": list(filters),
        "command_line": list(command_line),
        "exit_code": exit_code,
        "ran": list(ran),
        "tests": dict(tests) if tests is not None else None,
        "disc": disc,
        "log": log,
        "log_sha256": log_sha256,
        "started": started,
        "ended": ended,
    }


def write(folder: Path, record: Mapping[str, Any]) -> Path:
    """Write `record` to `folder` as `<sha256>.json` (all or nothing) and return the path; the same record is the same
    file."""
    data = canonical(dict(record))
    name = hashlib.sha256(data).hexdigest()
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{name}.json"
    temporary = folder / f".{name}.{os.getpid()}.tmp"
    temporary.write_bytes(data)
    temporary.replace(path)
    return path


def read(folder: Path, sha256: str) -> dict[str, Any]:
    """The record named `sha256` in `folder`; BuildRecordError when it is missing, not JSON, or its bytes do not hash
    to the name."""
    if not _NAME.match(sha256):
        raise BuildRecordError(f"{sha256!r}: a build record is named by 64 hex digits")
    path = folder / f"{sha256}.json"
    try:
        data = path.read_bytes()
    except OSError as error:
        raise BuildRecordError(f"the build record {sha256[:12]} is not in {folder}") from error
    if hashlib.sha256(data).hexdigest() != sha256:
        raise BuildRecordError(f"{path.name}: the file does not hash to its name (it was edited)")
    try:
        record = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BuildRecordError(f"{path.name}: not valid JSON ({error})") from error
    if not isinstance(record, dict):
        raise BuildRecordError(f"{path.name}: expected a JSON object")
    return record


def problems(record: Mapping[str, Any], *, tip: str, preset: str) -> list[str]:
    """Why `record` is not one whole, clean run of `preset` on the commit `tip` with the disc: every reason, in words
    that name what to do, or an empty list. A record whose log still exists must also match the log's hash."""
    found: list[str] = []
    if record.get("record_version") != RECORD_VERSION:
        found.append(f"the build record has version {record.get('record_version')!r}, this tool reads {RECORD_VERSION}")
    if record.get("preset") != preset:
        found.append(f"the build record is preset {record.get('preset')}, Check needs {preset}")
    filters = record.get("filters") or []
    if filters:
        found.append(f"the build record is a filtered run ({' '.join(map(str, filters))}); Check needs the full suite")
    if record.get("exit_code") != 0:
        found.append(f"the build record's run failed (exit {record.get('exit_code')})")
    if record.get("tip") != tip:
        found.append(f"the build record is for another commit ({str(record.get('tip'))[:10]}, the tip is {tip[:10]})")
    if record.get("dirty"):
        found.append("the build record was made from a worktree with uncommitted changes")
    found.extend(_test_problems(record))
    found.extend(_log_problems(record))
    return found


def _test_problems(record: Mapping[str, Any]) -> list[str]:
    """What is wrong with the record's test counts."""
    tests = record.get("tests")
    if not isinstance(tests, dict) or "test" not in (record.get("ran") or []):
        return ["the build record has no test run (Check needs the full suite)"]
    found: list[str] = []
    if not tests.get("total"):
        found.append("no tests ran")
    if tests.get("failed"):
        found.append(f"{tests['failed']} test{'s' if tests['failed'] != 1 else ''} failed")
    if tests.get("skipped_disc"):
        found.append(f"{tests['skipped_disc']} disc tests skipped; Check needs the disc (CONEY_DISC)")
    return found


def _log_problems(record: Mapping[str, Any]) -> list[str]:
    """The log's hash against the log, when the log is still there."""
    log = record.get("log")
    if not log:
        return []
    try:
        actual = hashlib.sha256(Path(str(log)).read_bytes()).hexdigest()
    except OSError:
        return []
    if actual != record.get("log_sha256"):
        return [f"the log {Path(str(log)).name} differs from the hash the build record kept (it was edited)"]
    return []
