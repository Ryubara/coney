# SPDX-License-Identifier: GPL-3.0-or-later
"""The preconditions of `coney-tools pr open`: who runs it, the tools it needs, the unit, the worktree, the claim.

Every refusal here is exit 2 (nothing was touched) except the claim, which is exit 1 (docs/guides/pr.md#open).
"""

from __future__ import annotations

import contextlib
import sys
from collections.abc import Iterator
from pathlib import Path

from coney_tools import pr_proof, pr_publish, pr_text
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, ConventionsError, load_conventions_leniently, load_release_rules_at
from coney_tools.issue_claim_engine import Engine
from coney_tools.pr_open_run import EXIT_UNABLE, Run, Stop
from coney_tools.pr_proof import Refusal
from coney_tools.pr_publish import GhError
from coney_tools.task_file import TaskFile, TaskFileError, parse_task_file
from coney_tools.unit import Unit, status_lines, task_name
from coney_tools.unit_record import UnitRecordError, read_record


@contextlib.contextmanager
def exclusive(path: Path, task: str) -> Iterator[None]:
    """Hold a lock file for the run (an operating-system lock, so a dead run releases it); Stop when it is taken."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as handle:
        try:
            if sys.platform == "win32":
                import msvcrt

                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise Stop(EXIT_UNABLE, f"another pr open of {task} is running") from None
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


# --- step 0: the preconditions ---------------------------------------------------------------------------------------


def read_task(folder: Path, name: str, conventions: Conventions) -> tuple[TaskFile, str]:
    """The task file in `folder` and its text; Stop(2) when it is missing or damaged."""
    path = folder / conventions.tasks.file
    try:
        raw = path.read_text(encoding="utf-8")
        return parse_task_file(raw, conventions.tasks), raw
    except (OSError, TaskFileError) as error:
        raise Stop(EXIT_UNABLE, f"{name}: {path.name} cannot be used ({error}); run task create") from error


def caller(run: Run) -> None:
    """Refuse an engineer or a reviewer: only the coordinator (or a person at the machine) opens pull requests."""
    who = run.env.who()
    if who.verified and who.type != "coordinator":
        raise Stop(EXIT_UNABLE, "only the coordinator opens pull requests; ask it")


def gh_ready(run: Run) -> None:
    """Refuse a `gh` older than the one that uploads pictures with `--attach`, or one that is not logged in."""
    gh = run.env.gh
    try:
        version = pr_publish.gh_version(gh)
        if version < pr_publish.MIN_GH:
            need = ".".join(str(part) for part in pr_publish.MIN_GH)
            raise Stop(EXIT_UNABLE, f"gh {need} or newer is needed (--attach); found {'.'.join(map(str, version))}")
        gh(["auth", "status"], None)
    except GhError as error:
        raise Stop(EXIT_UNABLE, f"gh is not ready ({error})") from error


def find_unit(run: Run) -> None:
    """Find the unit: the task, its record and worktree, the task files and the engineer's file."""
    request = run.request
    root0 = find_repo_root(request.worktree if request.worktree is not None else Path.cwd())
    conventions0 = load_conventions_leniently(root0)
    run.task = task_name(root0, request.task)
    own_dir = Unit.of(root0, run.task).dir
    try:
        run.record = read_record(own_dir / conventions0.tasks.start.unit_file)
    except UnitRecordError as error:
        raise Stop(EXIT_UNABLE, f"{run.task}: no unit worktree (run task start): {error}") from error
    worktree = request.worktree if request.worktree is not None else run.record.worktree
    if not worktree.is_dir():
        raise Stop(EXIT_UNABLE, f"{run.task}: no unit worktree (run task start): {worktree} does not exist")
    run.root = find_repo_root(worktree)
    run.conventions = load_conventions_leniently(run.root, run.say)
    run.owner = run.record.log_task
    # The default branch is the one origin's HEAD names, as pr verify takes it; a unit that was started from any
    # other base (a stacked task) is refused, so no unmerged branch is ever where the release rules come from.
    try:
        run.default_branch = pr_proof.default_branch(run.root)
    except ConfigError as error:
        raise Stop(EXIT_UNABLE, str(error)) from error
    expected = f"{request.remote}/{run.default_branch}"
    if run.record.base_ref != expected:
        raise Stop(
            EXIT_UNABLE,
            f"{run.task}: the unit was started from {run.record.base_ref}, not from {expected}; "
            "pr open releases a unit onto the default branch only (rebase the task onto it, or start it again)",
        )
    run.unit = Unit.of(root0, run.owner)
    own_task, own_raw = read_task(own_dir, run.task, run.conventions)
    # A fix task is released onto its parent's pull request, with the parent's issues, log and text.
    if run.owner != run.task:
        run.task_file, raw = read_task(run.unit.dir, run.owner, run.conventions)
    else:
        run.task_file, raw = own_task, own_raw
    run.keys = pr_text.optional_task_keys(raw)
    run.text_path = request.text if request.text is not None else run.unit.dir / "PR.md"


def git_state(run: Run) -> None:
    """Refuse a worktree that is not on the unit's branch, is mid-rebase or merge, or has uncommitted changes."""
    git, root, record = run.env.git, run.root, run.record
    assert record is not None
    branch = git(root, ["symbolic-ref", "--short", "-q", "HEAD"]).stdout.strip()
    if not branch:
        raise Stop(EXIT_UNABLE, f"{run.task}: no unit worktree (run task start): {root} has a detached HEAD")
    if branch != record.branch:
        raise Stop(EXIT_UNABLE, f"{run.task}: the worktree is on '{branch}', the unit's branch is '{record.branch}'")
    for marker in ("rebase-merge", "rebase-apply", "MERGE_HEAD"):
        path = pr_publish.out(git, root, "rev-parse", "--git-path", marker).strip()
        if (root / path).exists():
            raise Stop(EXIT_UNABLE, f"{run.task}: a rebase or merge is in progress in {root}")
    dirty = status_lines(root)
    if dirty:
        shown = ", ".join(line[3:] for line in dirty[:5]) + (" ..." if len(dirty) > 5 else "")
        raise Stop(EXIT_UNABLE, f"the worktree has uncommitted changes: {shown}; the engineer commits or removes them")


def fetch(run: Run) -> str:
    """Fetch the default branch and read the release rules from `origin/<default>` (whatever `--remote` is); the ref
    the change is measured from is `<remote>/<default>`."""
    assert run.conventions is not None
    default = run.default_branch
    done = run.env.git(run.root, ["fetch", "-q", run.request.remote, default])
    if done.returncode != 0:
        raise Stop(EXIT_UNABLE, f"cannot fetch {run.request.remote} {default}: {done.stderr.strip() or 'failed'}")
    upstream = f"{run.request.remote}/{default}"
    rules_ref = f"{pr_proof.RULES_REMOTE}/{default}"
    if run.request.remote != pr_proof.RULES_REMOTE:
        done = run.env.git(run.root, ["fetch", "-q", pr_proof.RULES_REMOTE, default])
        if done.returncode != 0:
            raise Stop(
                EXIT_UNABLE, f"cannot fetch {pr_proof.RULES_REMOTE} {default}: {done.stderr.strip() or 'failed'}"
            )
    try:
        run.rules = load_release_rules_at(run.root, rules_ref, run.conventions)
    except ConventionsError as error:
        raise Stop(EXIT_UNABLE, str(error)) from error
    return upstream


def claims(run: Run) -> list[Refusal]:
    """The claim on each issue of the task must be held by the signed-in account."""
    assert run.rules is not None and run.task_file is not None
    user = run.env.remote.current_user()
    held = Engine(run.env.remote, run.rules, run.env.clock).held_cards()
    found: list[Refusal] = []
    for number in run.task_file.issues:
        card = held.get(number)
        if card is None:
            text = f"claim: the claim on #{number} was released or never made: ask the coordinator to re-claim"
        elif card.claimant != user:
            text = f"claim: #{number} is claimed by @{card.claimant}, not @{user}"
        else:
            continue
        found.append(Refusal("claim", text, "coordinator"))
    return found
