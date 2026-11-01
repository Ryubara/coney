# SPDX-License-Identifier: GPL-3.0-or-later
"""`task start --adopt` and `--adopt-branch`: the unit of a task on a branch that already exists
(docs/guides/issues.md#adopting).

A pull request made before `task start` existed, or opened by hand before `pr open` existed, has a branch but no unit.
This makes the unit on that branch instead of a new one (the branch named like the task for `--adopt`, the one named for
`--adopt-branch`), and starts the task's stamp log afresh with an old one moved aside, never deleted. It is the part
of `task start` (`task_start.start_task` calls it after the checks it shares with an ordinary start) that is about an
existing branch; the git and folder helpers are in `task_adopt`.
"""

from __future__ import annotations

from pathlib import Path

from coney_tools import task_adopt
from coney_tools.config import ConfigError
from coney_tools.task_file import TaskFile
from coney_tools.task_start import (
    StartEnv,
    StartRequest,
    _fetch_and_resolve,
    _git,
    _log_report,
    _missing_steps,
    _post_progress,
    _stamp_start,
    _summary,
    _warn_unverified,
    pending_lines,
)
from coney_tools.unit_record import UnitRecord, write_record


def _log_problem(log_folder: Path, task: str, problem: str | None) -> str | None:
    """Why the task's stamp log must not be moved aside, or None. A log whose first stamp is `assigned` is a workflow
    log, damaged or not, and a log whose first stamp cannot be read might be one: neither is reset by adopting."""
    first = task_adopt.first_step(log_folder)
    if first == "assigned":
        return f"the log of '{task}' starts the workflow's way already ({log_folder}); ask the coordinator"
    if first is None and problem is not None:
        return f"the log of '{task}' is damaged and its first stamp cannot be read ({log_folder}); ask the coordinator"
    return None


def start_adopted(
    request: StartRequest, env: StartEnv, main: Path, task: TaskFile, user: str, record_path: Path, line: str
) -> int:
    """Make the unit of a task on a branch that exists already, the way of releasing again a pull request made before
    `task start` or opened before `pr open` existed: the worktree the branch has (or a new one), the record, a stamp log
    started afresh (an old one moved aside, never deleted) and the stamps `assigned` and `claimed`. The branch is not
    renamed, moved or rebased, and the stages after it are redone."""
    out = env.out
    branch = request.adopt_branch or task.task
    default = env.conventions.repo.default_branch
    if branch in (default, request.base or ""):
        out(f"refused: '{branch}' is the branch the work is measured from, not a branch with a pull request")
        return 1
    other = task_adopt.unit_on_branch(env.scratch, env.conventions.tasks.start.unit_file, branch)
    if other is not None:
        out(f"refused: the branch '{branch}' is the unit of the task '{other}' already; a branch has one unit")
        return 1
    if not task_adopt.has_local_branch(main, branch) and not (
        request.fetch and task_adopt.make_local_branch(main, branch, request.remote)
    ):
        out(
            f"refused: no branch '{branch}' to adopt, here or on {request.remote}; the unit keeps the pull request's "
            f"branch name, so name it (--adopt-branch) or make the task with that name (task create --name)"
        )
        return 1
    base_ref, base_commit = _fetch_and_resolve(request, env, main)
    # The unit starts where the branch left the base, not at the base now: the base moved on while the branch waited.
    fork_point = task_adopt.merge_base(main, branch, base_commit)
    if fork_point is None:
        out(f"refused: '{branch}' and {base_ref} have no common commit, so there is no base to start the unit from")
        return 1
    tip = _git(main, "rev-parse", f"refs/heads/{branch}^{{commit}}").stdout.strip()
    template = env.conventions.workspace.worktrees
    if "<branch>" not in template:
        raise ConfigError(f"workspace.worktrees ({template}) must contain <branch>")
    tree = task_adopt.worktree_of(main, branch)
    make_tree = tree is None
    if tree is None:
        tree = (main / template.replace("<branch>", branch)).resolve()
        if tree.exists():
            out(f"refused: {tree} exists already and is not a worktree of '{branch}'")
            return 1
    elif tree.resolve() == main.resolve():
        out(f"refused: the branch '{branch}' is checked out in the main checkout; check out another branch there")
        return 1
    log_folder = env.scratch / task.task / "stamps"
    _, problem = _log_report(task.task, env)
    old_log = log_folder.is_dir() and any(log_folder.glob("*.jsonl"))
    if old_log:
        why = _log_problem(log_folder, task.task, problem)
        if why is not None:
            out(f"refused: {why}")
            return 1
    record = UnitRecord(
        task=task.task,
        kind=task.kind,
        issues=task.issues,
        claimant=user,
        branch=branch,
        worktree=tree,
        base_ref=base_ref,
        base_commit=fork_point,
        started=env.now(),
    )
    if request.dry_run:
        moved = log_folder.with_name(task_adopt.archive_name(env.now()))
        made = "its worktree" if make_tree else f"the worktree it has ({tree})"
        out(f"dry run: would adopt the branch '{branch}' (left {base_ref} at {fork_point[:12]}) with {made}")
        if old_log:
            out(f"dry run: would move the old stamp log {log_folder} to {moved}")
        out(f"dry run: would write {record_path}, stamp assigned and claimed, and post /progress claimed on each issue")
        out(f"dry run: {line}")
        return 0
    if make_tree:
        done = _git(main, "worktree", "add", str(tree), branch)
        if done.returncode != 0:
            raise ConfigError(f"git worktree add {tree} failed: {done.stderr.strip() or 'no output'}")
    if old_log:
        target = task_adopt.archive_log(log_folder, env.now())
        out(f"  old stamp log moved to {target} (kept; no check reads it)")
    write_record(record_path, record)
    who = env.who()
    fresh, _ = _log_report(task.task, env)
    steps = _missing_steps(record, fresh)
    _warn_unverified(who, env)
    _stamp_start(record, task, who, env, steps, tip, fork_point, adopted=True)
    _post_progress(record, record_path, env)
    extra = pending_lines(env, task, record, True)
    _summary(record, steps, line, env.scratch, "adopted the branch (started on the existing branch)", env, extra)
    out(f"  redo the stages the pull request needs, then: pr open {task.task} --adopt <head> (docs/guides/pr.md#adopt)")
    return 0
