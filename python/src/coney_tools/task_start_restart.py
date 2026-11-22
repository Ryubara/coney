# SPDX-License-Identifier: GPL-3.0-or-later
"""`task start --restart`: start a stacked unit again from the default branch once the branch it was started from has
merged (docs/guides/issues.md#restarting).

A unit started from another unit's branch (`task start --base <branch>`) records that base, and `pr open` releases a
unit based on the default branch only. When the base's pull request merges, the engineer rebases the unit's branch off
the base's commits onto the default branch (`git rebase --onto origin/main <old base> <branch>`) and this command makes
the unit a unit of the default branch: the record's base becomes `origin/main`, the stamp log is moved aside (never
deleted) and started again with `assigned` and `claimed` saying so, and every stage after it is redone, as they were
stamped for a change on another base.

Whether the old base "has merged" is judged by what it changed, not by its commits: GitHub squashes a pull request, so
none of its commits are in the default branch. The base's tip is merged into the default branch in memory
(`git merge-tree`); if that adds nothing, every change of the base is in the default branch already. A branch must hold
none of the base's commits either, by hash or by change (`git patch-id`), so what is left of it is the unit's own.
When merging conflicts, because the default branch edited the base's lines after the squash, the base is judged merged
patch by patch instead: every one of its commits has its change in the default branch (`git cherry`), or its whole
change is the change of one commit there (the squash commit, by `git patch-id`). If neither tells, the coordinator can
go on with `--base-merged-reason "<why>"`, which is recorded in the `claimed` stamp.
These are the git helpers and the decision; `task_start` dispatches here and stamps the start through `_stamp_start`.
"""

from __future__ import annotations

import subprocess
from dataclasses import replace
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

#: How many commits or files a refusal lists before it says how many more there are.
SHOWN = 5


def _run_bytes(main: Path, args: list[str], data: bytes | None = None) -> subprocess.CompletedProcess[bytes]:
    """Run git in `main` on bytes (a diff of a binary file is not text); the caller looks at the exit code."""
    return subprocess.run(["git", "-C", str(main), *args], input=data, capture_output=True, check=False)


def _commit(main: Path, ref: str) -> str | None:
    """The commit `ref` names (a branch, a remote branch, a hash) in the repository, or None."""
    done = _git(main, "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}")
    return done.stdout.strip() if done.returncode == 0 and done.stdout.strip() else None


def old_base_tips(main: Path, record: UnitRecord, remote: str) -> tuple[str | None, list[str]]:
    """(the old base to judge, every tip the branch might hold commits of). The base's branch as it is now when it can
    be found (here or on the remote), since the base moves on while the unit waits and that is what merged; else the
    commit the unit recorded. The recorded commit is also searched for the base's commits in the branch, as the
    branch may have been cut from it before the base was rebased."""
    recorded = _commit(main, record.base_commit)
    current = _commit(main, f"refs/heads/{record.base_ref}") or _commit(
        main, f"refs/remotes/{remote}/{record.base_ref}"
    )
    current = current or _commit(main, record.base_ref)
    tips = [tip for tip in dict.fromkeys([current or "", recorded or ""]) if tip]
    return (current or recorded), tips


def _names(items: list[str], more: str = "") -> str:
    """The first few of `items`, comma separated, and how many more there are."""
    shown = ", ".join(items[:SHOWN])
    return shown + (f" and {len(items) - SHOWN} more{more}" if len(items) > SHOWN else "")


def _merge_conflict_or_additions(main: Path, onto: str, tip: str, base_ref: str) -> str | None:
    """Why merging the base (at `tip`) into `onto` in memory does not add nothing, or None when it adds nothing."""
    done = _git(main, "merge-tree", "--write-tree", "--name-only", "--no-messages", onto, tip)
    lines = done.stdout.split("\n")
    if done.returncode == 1:
        files = [line for line in lines[1:] if line.strip()]
        return (
            f"the old base {base_ref} has not merged into {onto}, or {onto} has changed what it added since: merging "
            f"it conflicts in {_names(files)}"
        )
    if done.returncode != 0 or not lines[0].strip():
        raise ConfigError(f"git merge-tree failed (it needs git 2.38 or later): {done.stderr.strip() or 'no output'}")
    merged = lines[0].strip()
    current = _git(main, "rev-parse", f"{onto}^{{tree}}").stdout.strip()
    if merged == current:
        return None
    changed = _git(main, "diff", "--name-only", current, merged).stdout.split()
    return f"the old base {base_ref} has not merged into {onto}: it still adds changes {onto} lacks ({_names(changed)})"


def merged_patch_by_patch(main: Path, onto: str, tip: str) -> bool:
    """Whether the base (at `tip`) has merged into `onto` judged by its changes alone, which holds when `onto` has
    edited the base's lines since and merging it conflicts: either every commit of the base has an equivalent in `onto`
    (`git cherry`: they were merged one by one), or the base's whole change against where it left `onto` is the change
    of one commit in `onto` (a squash merge). A base with no change of its own is not "merged" by this."""
    cherry = _git(main, "cherry", onto, tip)
    if cherry.returncode != 0:
        raise ConfigError(f"git cherry {onto} {tip} failed: {cherry.stderr.strip() or 'no output'}")
    marks = [line.split()[0] for line in cherry.stdout.splitlines() if line.strip()]
    if marks and all(mark == "-" for mark in marks):
        return True
    fork = _git(main, "merge-base", onto, tip)
    if fork.returncode != 0 or not fork.stdout.strip():
        return False
    fork_point = fork.stdout.strip()
    whole = _run_bytes(main, ["diff", "--no-ext-diff", "--no-color", "--binary", "--no-renames", fork_point, tip])
    if whole.returncode != 0:
        raise ConfigError(f"git diff failed: {whole.stderr.decode('utf-8', 'replace').strip() or 'no output'}")
    if not whole.stdout.strip():
        return False
    mine = _run_bytes(main, ["patch-id", "--stable"], whole.stdout)
    if mine.returncode != 0 or not mine.stdout.strip():
        raise ConfigError(f"git patch-id failed: {mine.stderr.decode('utf-8', 'replace').strip() or 'no output'}")
    return mine.stdout.decode("utf-8", "replace").split()[0] in _patch_ids(main, [f"{fork_point}..{onto}"])


def unmerged_problem(main: Path, onto: str, tip: str, base_ref: str) -> str | None:
    """Why the old base (at `tip`) has not merged into `onto`, or None when it has. Merging it in memory shows by change
    what its commits' hashes cannot, since a squash merge leaves none of them in `onto`; when that conflicts or adds
    something, because `onto` has edited the lines since, the patch-by-patch check (`merged_patch_by_patch`) decides.
    The caller adds the way on when neither check tells: the coordinator's `--base-merged-reason`."""
    problem = _merge_conflict_or_additions(main, onto, tip, base_ref)
    if problem is None or merged_patch_by_patch(main, onto, tip):
        return None
    return problem


def _patch_ids(main: Path, revs: list[str]) -> dict[str, str]:
    """The `git patch-id` of each non-merge commit `revs` selects (`<patch id>` to the commit), so a commit can be
    recognised by its change under another hash."""
    # No external diff or colour from the user's config, and binary files' content in the patch: the id must depend on
    # the change alone.
    log = _run_bytes(
        main,
        ["log", "-p", "--no-ext-diff", "--no-color", "--binary", "--no-renames", "--no-merges", "--reverse", *revs],
    )
    if log.returncode != 0:
        raise ConfigError(f"git log failed: {log.stderr.decode('utf-8', 'replace').strip() or 'no output'}")
    ids = _run_bytes(main, ["patch-id", "--stable"], log.stdout)
    if ids.returncode != 0:
        raise ConfigError(f"git patch-id failed: {ids.stderr.decode('utf-8', 'replace').strip() or 'no output'}")
    found: dict[str, str] = {}
    for line in ids.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) == 2:
            found.setdefault(parts[0], parts[1])
    return found


def _rev_list(main: Path, *revs: str) -> list[str]:
    """The commits `git rev-list` selects; ConfigError when git fails, so a failure never reads as "none"."""
    done = _git(main, "rev-list", *revs)
    if done.returncode != 0:
        raise ConfigError(f"git rev-list {' '.join(revs)} failed: {done.stderr.strip() or 'no output'}")
    return done.stdout.split()


def foreign_problem(main: Path, onto: str, branch: str, tips: list[str], base_ref: str) -> str | None:
    """Why the branch holds commits of the old base that are not in `onto`, or None when what it holds beyond `onto` is
    the unit's own: no commit of the base's tips by hash, and none with the same change under another hash."""
    held = _rev_list(main, f"refs/heads/{branch}", f"^{onto}")
    base = _rev_list(main, *tips, f"^{onto}")
    base_set = set(base)
    same = [commit for commit in held if commit in base_set]
    if same:
        return (
            f"the branch '{branch}' still holds {len(same)} commit{'s' if len(same) != 1 else ''} of the old base "
            f"{base_ref} ({_names([c[:10] for c in same])}); rebase it off them first: "
            f"git rebase --onto {onto} {base_ref} {branch}, in the unit's worktree"
        )
    if not base or not held:
        return None
    theirs = _patch_ids(main, [*tips, f"^{onto}"])
    copies = [
        commit for patch, commit in _patch_ids(main, [f"refs/heads/{branch}", f"^{onto}"]).items() if patch in theirs
    ]
    if copies:
        return (
            f"the branch '{branch}' holds {len(copies)} commit{'s' if len(copies) != 1 else ''} with the same change "
            f"as a commit of the old base {base_ref} ({_names([c[:10] for c in copies])}): not the unit's own, and not "
            f"in {onto}; rebase it off them first: git rebase --onto {onto} {base_ref} {branch}, in the unit's worktree"
        )
    return None


def _worktree_problem(record: UnitRecord, main: Path) -> str | None:
    """Why the unit's worktree cannot be restarted from, or None: it is there, on the unit's branch, which exists."""
    if not record.worktree.is_dir():
        return f"the unit's worktree is gone ({record.worktree}); ask the coordinator"
    if not task_adopt.has_local_branch(main, record.branch):
        return f"the unit's branch '{record.branch}' does not exist here; ask the coordinator"
    on = _git(record.worktree, "symbolic-ref", "--short", "-q", "HEAD").stdout.strip()
    if on != record.branch:
        return (
            f"the worktree {record.worktree} is not on the unit's branch '{record.branch}' ({on or 'a detached HEAD'})"
        )
    return None


def _newest_archive(log_folder: Path) -> str:
    """The name of the newest old log moved aside beside `log_folder`, or "" when there is none: a rerun after a stop
    between moving the log and stamping finds no log to move, and the earlier move is where the old stamps went."""
    found = sorted(p.name for p in log_folder.parent.glob(f"{task_adopt.ARCHIVE_PREFIX}*") if p.is_dir())
    return found[-1] if found else ""


def restart_unit(
    request: StartRequest, env: StartEnv, main: Path, task: TaskFile, record: UnitRecord, record_path: Path, line: str
) -> int:
    """Start the stacked unit `record` again from the default branch; 0 when it was (or would be), 1 when refused. The
    worktree and branch stay as they are; the record, the log's start and the stages' results are what change."""
    out = env.out
    default = env.conventions.repo.default_branch
    onto = f"{request.remote}/{default}"
    if record.parent:
        out("refused: --restart makes an ordinary task's unit start again; a fix task works in its parent's unit")
        return 1
    if record.base_ref == onto:
        out(f"refused: the unit of '{task.task}' was started from {onto} already; there is nothing to restart")
        return 1
    why = _worktree_problem(record, main)
    if why is not None:
        out(f"refused: {why}")
        return 1
    log_folder = env.scratch / record.log_task / "stamps"
    report, problem = _log_report(record.log_task, env)
    if problem is not None:
        out(f"refused: {problem}")
        return 1
    if report.latest("pr-opened", result="pass") is not None:
        out(
            f"refused: the unit of '{task.task}' was released already (its log has a pr-opened stamp); "
            "pr open updates its pull request"
        )
        return 1
    base_ref, _ = _fetch_and_resolve(request, env, main)
    old, tips = old_base_tips(main, record, request.remote)
    if old is None:
        out(
            f"refused: the old base {record.base_ref} ({record.base_commit[:12]}) is not in this repository, so what "
            "it added cannot be checked; fetch it, or ask the coordinator"
        )
        return 1
    reason = (request.base_merged_reason or "").strip()
    unmerged = unmerged_problem(main, onto, old, record.base_ref)
    if unmerged is not None and not reason:
        out(
            f"refused: {unmerged}; if its pull request has merged, this check cannot tell, and the coordinator can go "
            f'on with a recorded reason: task start {task.task} --restart --base-merged-reason "<why it merged>" '
            "(docs/guides/issues.md#restarting)"
        )
        return 1
    if reason and unmerged is None:
        out("  the old base is shown merged: --base-merged-reason is not needed and not recorded")
    why = foreign_problem(main, onto, record.branch, tips, record.base_ref)
    if why is not None:
        out(f"refused: {why}")
        return 1
    tip = _git(main, "rev-parse", f"refs/heads/{record.branch}^{{commit}}").stdout.strip()
    fork_point = task_adopt.merge_base(main, record.branch, onto)
    if fork_point is None:
        out(f"refused: '{record.branch}' and {onto} have no common commit, so there is no base to start the unit from")
        return 1
    fresh = replace(record, base_ref=base_ref, base_commit=fork_point, started=env.now())
    old_log = log_folder.is_dir() and any(log_folder.glob("*.jsonl"))
    if request.dry_run:
        moved = log_folder.with_name(task_adopt.archive_name(env.now()))
        out(
            f"dry run: would restart '{task.task}' from {base_ref} (the branch left it at {fork_point[:12]}); "
            f"its base {record.base_ref} has merged"
            + (f" (by the coordinator's reason: {reason})" if unmerged is not None else "")
        )
        if old_log:
            out(f"dry run: would move the old stamp log {log_folder} to {moved}")
        out(f"dry run: would write {record_path}, then stamp assigned and claimed")
        out(f"dry run: {line}")
        return 0
    # The old log moves first, then the new one is stamped, and the record is written last: a failure in between leaves
    # the record on the old base, so a rerun restarts again (docs/guides/issues.md#restarting).
    target = task_adopt.archive_log(log_folder, env.now()) if old_log else None
    if target is not None:
        out(f"  old stamp log moved to {target} (kept; no check reads it)")
    who = env.who()
    steps = _missing_steps(fresh, _log_report(record.log_task, env)[0])
    _warn_unverified(who, env)
    note = {
        "from_base_ref": record.base_ref,
        "from_base_commit": record.base_commit,
        "log": target.name if target else _newest_archive(log_folder),
        # What the check said and the coordinator's reason for going on, in the stamp the reviewer reads.
        **({"base_merged_override": {"reason": reason, "check": unmerged}} if unmerged is not None else {}),
    }
    _stamp_start(fresh, task, who, env, steps, tip, fork_point, restarted=note)
    write_record(record_path, fresh)
    if not record.progress_posted:
        _post_progress(fresh, record_path, env)
    extra = pending_lines(env, task, fresh, True)
    if unmerged is not None:
        out(f"  --base-merged-reason recorded in the claimed stamp (the check said: {unmerged}): {reason}")
    _summary(
        fresh, steps, line, env.scratch, f"restarted from {base_ref} (its base {record.base_ref} merged)", env, extra
    )
    out(f"  redo the stages the pull request needs, then: pr open {task.task} (docs/guides/issues.md#restarting)")
    return 0
