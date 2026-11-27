# SPDX-License-Identifier: GPL-3.0-or-later
"""The units `workspace clean` must not take the worktree of (docs/guides/workspace.md#cleanup, issue #1683).

A worktree with no commits and no changes is contained in `origin/main`, so it looks merged: a unit paused before its
first commit, or waiting on a question or a build slot, would be removed once idle past `worktree_hours`, and
`task start` could not resume it. A unit record in work names its worktree, and `clean` keeps the worktree while

- `task slots` counts the unit (`task_slots.unit_over` says it is not over: no `finished` stamp and no passing Review
  since the claim), or
- the unit passed Review but was not released (its log has no `pr-opened` stamp since its claim) while its task still
  holds the claim on GitHub (`task_slots.claim_problem`): `pr open` still needs the worktree.

The unit records are read from the scratch folder once per run, and each claim from GitHub once, only for a worktree
that would otherwise be removed. Anything that cannot be judged keeps the worktree and says so: a stamp log that
cannot be read, a record that cannot be read, a GitHub that cannot be asked, conventions that cannot be read.
"""

from __future__ import annotations

import subprocess
import threading
from collections.abc import Callable
from pathlib import Path

from coney_tools import stamps, task_slots
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_remote import GhRemote, Remote
from coney_tools.unit_record import UnitRecord, UnitRecordError, read_record

#: Says why the unit of a worktree keeps it, or "" when no unit in work does.
InWork = Callable[[Path], str]

#: How long one `gh` read of a claim may take (the call is killed then and the worktree is kept).
GH_SECONDS = 20.0


def nobody(worktree: Path) -> str:
    """An `InWork` that knows no unit: the default of a test that does not ask for one."""
    return ""


def _gh(checkout: Path) -> Callable[[list[str]], str]:
    """A runner of read-only `gh` commands in `checkout` for `GhRemote`; OSError when one fails or takes too long."""

    def run(arguments: list[str]) -> str:
        try:
            done = subprocess.run(
                ["gh", *arguments],
                cwd=checkout,
                capture_output=True,
                text=True,
                encoding="utf-8",
                timeout=GH_SECONDS,
                check=False,
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise OSError(f"gh could not run: {error}") from error
        if done.returncode != 0:
            raise OSError(f"gh {' '.join(arguments[:2])} failed: {done.stderr.strip() or done.stdout.strip()}")
        return done.stdout

    return run


def github_remote(checkout: Path, conventions: Conventions | None) -> Callable[[], Remote]:
    """A maker of the `Remote` that reads claims with `gh` from `checkout` (made on the first claim it is asked for;
    ConfigError without conventions, which name the bot and the markers of the claim cards)."""

    def make() -> Remote:
        if conventions is None:
            raise ConfigError("the conventions cannot be read, so the claim cannot be found")
        rules = conventions.claims
        return GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, _gh(checkout))

    return make


class UnitsInWork:
    """The `InWork` of one run: the units of a scratch folder, read on the first call."""

    def __init__(self, scratch: Path, conventions: Conventions | None, remote: Callable[[], Remote]) -> None:
        self._scratch = scratch
        self._conventions = conventions
        self._make_remote = remote
        # `clean` judges several worktrees at once, and the claim cards are read one at a time.
        self._lock = threading.Lock()
        self._records: dict[Path, list[UnitRecord]] | None = None
        self._unreadable: set[str] = set()
        self._cards: task_slots.Cards | None = None

    def __call__(self, worktree: Path) -> str:
        """Why a unit in work keeps `worktree`, or "" when none does."""
        with self._lock:
            records = self._read()
            if self._conventions is None:
                named = records.get(worktree.resolve()) or worktree.name in self._unreadable
                return "a unit record names it and the conventions cannot be read to judge the unit" if named else ""
            if worktree.name in self._unreadable:
                return f"unit in work: the unit record of {worktree.name} cannot be read"
            for record in records.get(worktree.resolve(), []):
                if reason := self._judge(record, self._conventions):
                    return f"unit in work: {reason}"
            return ""

    def _read(self) -> dict[Path, list[UnitRecord]]:
        """The unit records by the worktree they name, read once; the folders whose record cannot be read are kept in
        `_unreadable` (their worktree is named like the task)."""
        if self._records is not None:
            return self._records
        found: dict[Path, list[UnitRecord]] = {}
        unit_file = self._conventions.tasks.start.unit_file if self._conventions is not None else "unit.json"
        folders = sorted(self._scratch.iterdir()) if self._scratch.is_dir() else []
        for folder in folders:
            path = folder / unit_file
            if not path.is_file():
                continue
            try:
                record = read_record(path)
            except UnitRecordError:
                self._unreadable.add(folder.name)
                continue
            found.setdefault(record.worktree.resolve(), []).append(record)
        self._records = found
        return found

    def _judge(self, record: UnitRecord, conventions: Conventions) -> str:
        """Why `record`'s unit keeps its worktree, or ""."""
        report = stamps.verify(self._scratch / record.log_task / "stamps", conventions.stamps)
        if not report.ok:
            return f"{record.task}: its stamp log cannot be read ({report.problems[0]})"
        if not task_slots.unit_over(record, report):
            return f"{record.task}: task slots counts it"
        if report.latest("finished") is not None or self._released(record, report):
            return ""
        task = task_slots.read_task(self._scratch, record.task, conventions)
        if isinstance(task, str):
            return ""
        if self._cards is None:
            remote = self._make_remote()
            self._cards = task_slots.Cards(lambda number: task_slots.held_card(remote, number, conventions))
        try:
            problem = task_slots.claim_problem(task, self._cards)
        except ConfigError as error:
            return f"{record.task}: its claim could not be read from GitHub ({error}), so it is taken as held"
        if problem is not None:
            return ""
        return f"{record.task} holds its claim and its log has no pr-opened stamp"

    @staticmethod
    def _released(record: UnitRecord, report: stamps.Report) -> bool:
        """Whether a `pr-opened` stamp came after the unit's own claim (a fix's, the stamp naming it in `fix`)."""
        opened = report.latest("pr-opened")
        if opened is None:
            return False
        fix = record.task if record.parent else ""
        claimed = [s for s in report.stamps if s.step == "claimed" and s.detail.get("fix", "") == fix]
        return not claimed or opened.seq > claimed[-1].seq


def units_in_work(scratch: Path, conventions: Conventions | None, remote: Callable[[], Remote]) -> UnitsInWork:
    """The `InWork` for the units of `scratch`; `remote` makes the GitHub the claims are read from, on the first use.
    Without `conventions` a worktree a unit record names is kept, whatever the unit's state."""
    return UnitsInWork(scratch, conventions, remote)
