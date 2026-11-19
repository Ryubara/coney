# SPDX-License-Identifier: GPL-3.0-or-later
"""The resume of `coney-tools task check`: what passed in an earlier run for the same change, kept so a rerun goes on
from the first check that did not pass (docs/guides/coney-tools-repo.md#task-check-resume).

A run keeps a **ledger**, `<scratch>/<task>/check-resume/ledger.json`, written after every check that passes (so a
run that was killed, gave up waiting for a slot or hit a missing program keeps what it had done). The ledger holds a
**key**, the facts that decide whether a pass still means anything, and the passed checks with the counts the stamp
needs:

* the change id (`covers.diff_sha256`), the tip commit and the base commit: a clean rebase keeps the change id but moves
  the ground the checks ran on, and a build record is bound to its tip;
* the profile, the checks it runs, and a fingerprint of the rules the checks read (`proof`, the scope classes of
  `paths`);
* the disc setting, where the tests' disc came from and its path.

A run whose key differs from the ledger's drops the ledger and runs everything; a pass older than `proof.resume.
max_age_hours` is run again. A passing run removes the ledger, because its stamp is the record from then on. The
stamp says which checks it took from an earlier run (`detail.resumed`), so a resumed pass is never taken for a full
one. The folder is one the protect hook keeps agents from writing, as it does for stamps and build records; as with
them, nothing here stops the person at the machine. Standard library, besides the stamp log's canonical form.
"""

from __future__ import annotations

import hashlib
import json
import os
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

from coney_tools.conventions import Conventions
from coney_tools.stamps import canonical

#: The folder of the task's scratch folder that holds the ledger (the protect hook's `STAMP_FOLDERS` names it too).
FOLDER = "check-resume"
#: The ledger's file name.
FILE = "ledger.json"
#: The format of the ledger.
VERSION = 1
#: A time as the stamps write it, so the stamp's `resumed.from` reads like the stamps beside it.
TIME = "%Y-%m-%dT%H:%M:%SZ"


@dataclass(frozen=True)
class Key:
    """What must be the same for an earlier pass to count: see the module docstring."""

    change: str
    tip: str
    base: str
    profile: str
    checks: tuple[str, ...]
    rules: str
    disc: str

    def as_dict(self) -> dict[str, Any]:
        """The key as written to the ledger."""
        return {
            "change": self.change,
            "tip": self.tip,
            "base": self.base,
            "profile": self.profile,
            "checks": list(self.checks),
            "rules": self.rules,
            "disc": self.disc,
        }

    @property
    def sha256(self) -> str:
        """The key's hash, which the stamp names so a reader can tell two resumed stamps came from one ledger."""
        return hashlib.sha256(canonical(self.as_dict())).hexdigest()

    def differs_from(self, other: Mapping[str, Any]) -> list[str]:
        """The names of the fields in which `other` (a ledger's key) is not this key."""
        mine = self.as_dict()
        return [name for name in mine if other.get(name) != mine[name]]


def rules_fingerprint(conventions: Conventions) -> str:
    """A hash of the rules the checks read: the `proof` section and the three scope lists of `paths`, as `task check`
    read them from the base. The text of the frozen dataclasses is the input, so a field added later is part of it
    without this function being told."""
    paths = conventions.paths
    text = repr((conventions.proof, paths.docs_only, paths.tools_only, paths.engine))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def at(moment: datetime) -> str:
    """`moment` as the stamps write a time (UTC, to the second)."""
    return moment.astimezone(UTC).strftime(TIME)


def _parse(text: object) -> datetime | None:
    """A time written by `at`, or None."""
    try:
        return datetime.strptime(str(text), TIME).replace(tzinfo=UTC)
    except ValueError:
        return None


@dataclass
class Entry:
    """A check that passed: what it said, what it counted, the run that did it and when it ended."""

    name: str
    detail: str
    counts: dict[str, int]
    run: str
    at: str


@dataclass
class Ledger:
    """The passes kept for one key, and the ones this run took. Writes are all or nothing and never fail the run."""

    path: Path
    key: Key
    run: str
    entries: dict[str, Entry] = field(default_factory=dict)
    #: The build record the build checks' passes belong to (its hash and test counts), or None.
    build: dict[str, Any] | None = None
    #: The checks this run took from the ledger, in order, and the runs that had done them.
    used: list[str] = field(default_factory=list)
    used_runs: set[str] = field(default_factory=set)

    @classmethod
    def open(
        cls,
        folder: Path,
        key: Key,
        *,
        run: datetime,
        max_age_hours: int,
        use: bool,
        say: Callable[[str], None],
    ) -> Ledger:
        """The ledger of `folder` for `key`: what passed before when its key is this one and the pass is not older than
        `max_age_hours`, else an empty one (said, with the reason, because a run that starts from the beginning when
        the reader expected otherwise should not be a mystery). `use` False (`--no-resume`) ignores what is there; the
        next write replaces it."""
        ledger = cls(folder / FILE, key, at(run))
        if not use:
            say("  resume: off (--no-resume): every check runs")
            return ledger
        try:
            text = ledger.path.read_text(encoding="utf-8")
        except FileNotFoundError:
            return ledger
        except OSError as error:
            say(f"  resume: ignoring {ledger.path.name}: {error}")
            return ledger
        try:
            data = json.loads(text)
            if not isinstance(data, dict) or data.get("version") != VERSION:
                raise ValueError(f"not a version {VERSION} ledger")
            saved = data["key"]
            entries = data["entries"]
            if not isinstance(saved, dict) or not isinstance(entries, dict):
                raise ValueError("no key or entries")
            kept = {name: _entry(name, value) for name, value in entries.items()}
            build = data.get("build")
            if build is not None and not (isinstance(build, dict) and isinstance(build.get("record"), str)):
                raise ValueError("a build that is not a record")
        except (ValueError, KeyError, TypeError) as error:
            say(f"  resume: ignoring {ledger.path.name}: {error}")
            return ledger
        different = key.differs_from(saved)
        if different:
            say(f"  resume: the earlier run was for another {', '.join(different)}: every check runs")
            return ledger
        limit = timedelta(hours=max_age_hours)
        for name, entry in kept.items():
            ended = _parse(entry.at)
            if ended is None or run - ended > limit:
                say(f"  resume: {name} passed at {entry.at}, older than the {max_age_hours} hours a resume trusts")
                continue
            ledger.entries[name] = entry
        ledger.build = build if isinstance(build, dict) else None
        return ledger

    def taken(self, name: str) -> Entry | None:
        """The earlier pass of `name`, or None."""
        return self.entries.get(name)

    def take(self, name: str) -> Entry:
        """Count `name` as taken from the ledger and return its entry."""
        entry = self.entries[name]
        self.used.append(name)
        self.used_runs.add(entry.run)
        return entry

    def keep(self, name: str, detail: str, counts: Mapping[str, int], moment: datetime) -> None:
        """Remember that `name` passed in this run."""
        self.entries[name] = Entry(name, detail, dict(counts), self.run, at(moment))
        self.save()

    def keep_build(
        self,
        names: Sequence[str],
        details: Sequence[str],
        record: str,
        tests: Mapping[str, int] | None,
        moment: datetime,
    ) -> None:
        """Remember that the build run passed: each of its checks, and the record that proves it."""
        for name, detail in zip(names, details, strict=True):
            self.entries[name] = Entry(name, detail, {}, self.run, at(moment))
        self.build = {"record": record, "tests": dict(tests) if tests is not None else None}
        self.save()

    def forget(self, names: Sequence[str]) -> None:
        """Drop `names` (a build record that no longer holds takes its checks' passes with it)."""
        for name in names:
            self.entries.pop(name, None)
        self.build = None

    def summary(self) -> dict[str, Any] | None:
        """What a stamp says of the checks it took: which, from which runs, under which key; None when it took none."""
        if not self.used:
            return None
        return {"checks": list(self.used), "from": sorted(self.used_runs), "key_sha256": self.key.sha256}

    def save(self) -> None:
        """Write the ledger (a temporary file, then a rename); a failure is ignored: the ledger only saves time."""
        data = {
            "version": VERSION,
            "key": self.key.as_dict(),
            "key_sha256": self.key.sha256,
            "entries": {
                name: {"detail": e.detail, "counts": e.counts, "run": e.run, "at": e.at}
                for name, e in self.entries.items()
            },
            "build": self.build,
        }
        temporary = self.path.with_name(f".{self.path.name}.{os.getpid()}.tmp")
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps(data, sort_keys=True, indent=1), encoding="utf-8")
            temporary.replace(self.path)
        except OSError:
            temporary.unlink(missing_ok=True)

    def clear(self) -> None:
        """Remove the ledger: a passing run's stamp is the record now."""
        self.path.unlink(missing_ok=True)


def _entry(name: str, value: object) -> Entry:
    """One ledger entry from its JSON, or a ValueError naming what is wrong."""
    if not isinstance(value, dict):
        raise ValueError(f"{name}: not an object")
    counts = value.get("counts", {})
    if not isinstance(counts, dict) or not all(isinstance(n, int) for n in counts.values()):
        raise ValueError(f"{name}: counts that are not numbers")
    return Entry(name, str(value.get("detail", "")), dict(counts), str(value.get("run", "")), str(value.get("at", "")))
