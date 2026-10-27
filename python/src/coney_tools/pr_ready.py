# SPDX-License-Identifier: GPL-3.0-or-later
"""The proof of one tip, and whether a unit's stamps cover its change: one decision for `pr open` and Conclave.

`prove` is the check `pr open` runs before it pushes (docs/guides/pr.md#open, step 4): `pr verify`'s stages and `pr
text`'s checks on the unit as it is. `readiness` asks the same question without releasing anything, for Conclave's
`unit-ready` event (docs/guides/conclave.md#watchers): it is true exactly when `prove` finds nothing to refuse, so the
wake that says "ready for pr open" and the command that opens it can never disagree about the stamps.

The snapshot asks about every unit that has a description file on every GitHub read, and a full proof reads git, the
whole stamp log and (for a change under `src/`) every `@orig` function at two commits, which takes seconds. So
`readiness` goes from cheap to dear and stops at the first refusal: the stamp log alone (no passing Check or Review),
then the change as git sees it (a Check or Review made for another change, uncommitted work), and only a unit whose
stamps cover its change is proved in full.

Nothing here writes: no stamp, no file, no git state.
"""

from __future__ import annotations

import dataclasses
import hashlib
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import pr_proof, pr_text, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.pr_proof import Refusal
from coney_tools.repo_checks import branch_trailer_models
from coney_tools.task_file import TaskFile, TaskFileError, parse_task_file
from coney_tools.unit import Unit


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


def without_opened(report: stamps.Report) -> stamps.Report:
    """The log as the facts show it: without `pr-opened` stamps, so a rerun makes the same facts as the first run."""
    return stamps.Report(stamps=[s for s in report.stamps if s.step != "pr-opened"], problems=list(report.problems))


def prove(
    root: Path,
    unit: Unit,
    owner: str,
    task_file: TaskFile,
    keys: dict[str, Any],
    text_path: Path,
    *,
    remote: str = "origin",
    now: datetime | None = None,
    subject: pr_proof.Subject | None = None,
) -> Proof:
    """Steps 1-5 of a release on the worktree `root`: the stages of `pr verify`, then the title and description `pr
    text` generates from the engineer's file at `text_path`, with the checks CI runs on them.

    `owner` is the task whose log and branch the unit is (a fix task's parent). Every refusal is returned, none is
    raised; a ConfigError means the unit could not be read at all (an unknown ref, unreadable rules). `subject` is
    the unit already read, with the functions of its change (`pr_proof.scanned`), when the caller has it."""
    if subject is None:
        subject = pr_proof.load_subject(root, unit, owner, task_file.kind, remote=remote, now=now)
    try:
        hashes: pr_proof.TextHashes | None = pr_text.hashes_of(text_path)
    except ConfigError:
        hashes = None  # the text check below says what is wrong with the file
    result = pr_proof.verify(subject, hashes)
    refusals = list(result.refusals)
    generated: pr_text.Generated | None = None
    pictures: list[tuple[str, Path, str]] = []
    models: list[str] = []
    try:
        form = pr_text.read_engineer_file(text_path, subject.conventions)
        models = branch_trailer_models(root, subject.base, subject.conventions.commits.trailers)
        shown = dataclasses.replace(subject, report=without_opened(subject.report))
        generated = pr_text.generate(shown, task_file, form, keys=keys, trailer_models=models)
        refusals += generated.refusals
        pictures = [
            (image.ref, image.path, hashlib.sha256(image.path.read_bytes()).hexdigest())
            for image in form.images
            if image.path.is_file()
        ]
    except ConfigError as error:
        refusals.append(Refusal("text", f"text: {error}", "engineer"))
    return Proof(subject, refusals, generated, pictures, models, list(result.notes))


# --- is the unit ready ----------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Readiness:
    """Whether `pr open` would pass the proof for a unit, and what the answer is bound to."""

    #: True when the proof finds nothing to refuse.
    ready: bool
    #: The first refusal's text; empty when the unit is ready.
    reason: str = ""
    #: The change the stamps were weighed against (`pr_diff`'s hash); empty when git was not read, because the stamp
    #: log alone already said the unit is not ready.
    change: str = ""
    #: True when the answer could not be had (an unknown ref, unreadable rules, git failing), which says nothing about
    #: the unit: a caller must not take it for "not ready".
    unknown: bool = False


def readiness(
    root: Path,
    unit: Unit,
    text_path: Path,
    conventions: Conventions,
    *,
    remote: str = "origin",
    now: datetime | None = None,
) -> Readiness:
    """Whether `pr open` would pass the proof for the unit whose scratch folder is `unit.dir` and whose worktree is
    `root`, with the description at `text_path`.

    A folder with no readable task file is not a task unit (`pr open` refuses it) and is not ready. `conventions` are
    the caller's own (the task file's name and the stamp rules); the proof reads the rest from the remote's default
    branch, as `pr verify` does."""
    try:
        raw = (unit.dir / conventions.tasks.file).read_text(encoding="utf-8")
        task_file = parse_task_file(raw, conventions.tasks)
    except (OSError, UnicodeDecodeError, TaskFileError) as error:
        return Readiness(False, f"{unit.task}: no readable {conventions.tasks.file} ({error})")
    # A fix task continues its parent's unit: the log, the branch and the text are the parent's.
    owner = task_file.parent or unit.task
    own = unit if owner == unit.task else Unit(owner, unit.dir.parent / owner)
    unsettled = pr_proof.unsettled(stamps.verify(own.stamps, conventions.stamps, task=owner))
    if unsettled:
        return Readiness(False, unsettled[0].text)
    try:
        subject = pr_proof.load_subject(root, own, owner, task_file.kind, remote=remote, now=now, scan=False)
        early = pr_proof.refusals_before_scan(subject)
        if early:
            return Readiness(False, early[0].text, subject.diff_sha256)
        proof = prove(
            root,
            own,
            owner,
            task_file,
            pr_text.optional_task_keys(raw),
            text_path,
            remote=remote,
            now=now,
            subject=pr_proof.scanned(subject),
        )
    except (ConfigError, OSError) as error:
        return Readiness(False, str(error) or type(error).__name__, unknown=True)
    if proof.refusals:
        return Readiness(False, proof.refusals[0].text, proof.subject.diff_sha256)
    return Readiness(True, "", proof.subject.diff_sha256)
