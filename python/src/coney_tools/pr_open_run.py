# SPDX-License-Identifier: GPL-3.0-or-later
"""What the steps of `coney-tools pr open` share: the request, the machine, a run's state and the proof of one tip.

Kept apart from the steps (`pr_open`, `pr_open_publish`) so both can use them (docs/guides/pr.md#open).
"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import identity, pr_proof, pr_text, stamps
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_remote import Remote
from coney_tools.pr_proof import Refusal
from coney_tools.pr_publish import Gh, Git
from coney_tools.task_file import TaskFile
from coney_tools.unit import Unit
from coney_tools.unit_record import UnitRecord

#: Exit codes: 1 refused, 2 cannot run (nothing touched), 3 a rebase conflict, 4 a step failed after the first write.
EXIT_REFUSED = 1
EXIT_UNABLE = 2
EXIT_CONFLICT = 3
EXIT_FAILED = 4
#: The permissions of the signed-in account that may turn auto-merge on.
MERGERS = ("admin", "maintain", "write")
#: The body written for an update that must show already uploaded pictures by address.
UPLOAD_BODY = "PR-BODY-upload.md"
PROGRESS = "/progress pr-opened"


@dataclass(frozen=True)
class Request:
    """What was asked: the task (default `CONEY_TASK`, else the branch), the unit's worktree (default: the one `task
    start` recorded), the engineer's file (default `PR.md` in the task's folder), the remote, the repository (default:
    what `gh` makes of the remote), whether to reopen a closed pull request, to leave auto-merge off, to only try, and
    to print one JSON object."""

    task: str | None = None
    worktree: Path | None = None
    text: Path | None = None
    remote: str = "origin"
    repo: str | None = None
    reopen: bool = False
    no_auto_merge: bool = False
    dry_run: bool = False
    json: bool = False


@dataclass
class Env:
    """The machine `pr open` runs on: `gh`, git, the claims on GitHub, the clock, where lines go, who is running it."""

    gh: Gh
    git: Git
    remote: Remote
    clock: Callable[[], datetime]
    say: Callable[[str], None]
    who: Callable[[], identity.Identity]
    #: The stamp's `who`, as `stamps.who_from_env` makes it.
    stamp_who: Callable[[], dict[str, Any]] = lambda: stamps.who_from_env("coney-tools pr open")
    #: Builds the claims client from the default branch's rules (bot login, markers) once they are read, replacing
    #: `remote`, so nothing the branch holds names the bot. None keeps `remote` (the tests' made-up one).
    remote_from: Callable[[Conventions], Remote] | None = None


class Stop(Exception):
    """The run ends here with this exit code, this message and these refusals."""

    def __init__(self, code: int, message: str, refusals: Sequence[Refusal] = ()) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.refusals = tuple(refusals)


@dataclass
class Run:
    """What one run knows: the request, the unit and what has happened, for the report."""

    request: Request
    env: Env
    task: str = ""
    owner: str = ""
    root: Path = Path()
    #: The unit's own checkout's rules: where to find files. Nothing that steers the release is read from them.
    conventions: Conventions | None = None
    #: The sections that steer the release (repo, pull requests, merge, claims, stamps) as the default branch has
    #: them: a branch under release cannot change them. Set by `pr_open_unit.fetch`.
    rules: Conventions | None = None
    #: The default branch, as `origin`'s `HEAD` names it (`pr_proof.default_branch`), the same one `pr verify` uses;
    #: never from the branch under release or from the unit record. Set by `pr_open_unit.find_unit`.
    default_branch: str = "main"
    record: UnitRecord | None = None
    unit: Unit | None = None
    task_file: TaskFile | None = None
    keys: dict[str, Any] = field(default_factory=dict)
    text_path: Path = Path()
    repo: str = ""
    log: list[str] = field(default_factory=list)
    result: dict[str, Any] = field(default_factory=dict)

    def say(self, line: str) -> None:
        """Print a line, or keep it for the JSON report."""
        if self.request.json:
            self.log.append(line)
        else:
            self.env.say(line)


@dataclass
class Proof:
    """The checks of `pr verify` and `pr text` on one tip."""

    subject: pr_proof.Subject
    refusals: list[Refusal]
    generated: pr_text.Generated | None
    pictures: list[tuple[str, Path, str]]
    models: list[str]
    notes: list[str]

    @property
    def conventions(self) -> Conventions:
        """The proof rules (`proof` and the path classes) from the remote's default branch; every other section is the
        branch's own. What steers a release is `Run.rules`, which is wholly the default branch's."""
        return self.subject.conventions
