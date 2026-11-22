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
change is the change of one commit there (the squash commit, by `git patch-id`), and no later commit of the default
branch reverts that commit (`git revert`'s message, GitHub's `Revert "<title>"`, or a commit whose change is the exact
inverse). A merge that is clean but adds changes is never judged this way: it is refused, which also catches a base
that merged and was reverted. If the check does not tell, the coordinator can go on with `--base-merged-reason "<why>"`,
which is recorded in the `claimed` stamp and in the review packet (`base_merged_override`).
These are the git helpers and the decision; `task_start` dispatches here and stamps the start through `_stamp_start`.
"""

from __future__ import annotations

import subprocess
from dataclasses import replace
from pathlib import Path
from typing import Any

from coney_tools import stamps, task_adopt
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
    claimed_detail,
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


def _merge_conflict_or_additions(main: Path, onto: str, tip: str, base_ref: str) -> tuple[str | None, bool]:
    """(why merging the base (at `tip`) into `onto` in memory does not add nothing, or None when it adds nothing; and
    whether it conflicted, as against adding changes cleanly)."""
    done = _git(main, "merge-tree", "--write-tree", "--name-only", "--no-messages", onto, tip)
    lines = done.stdout.split("\n")
    if done.returncode == 1:
        files = [line for line in lines[1:] if line.strip()]
        return (
            f"the old base {base_ref} has not merged into {onto}, or {onto} has changed what it added since: merging "
            f"it conflicts in {_names(files)}"
        ), True
    if done.returncode != 0 or not lines[0].strip():
        raise ConfigError(f"git merge-tree failed (it needs git 2.38 or later): {done.stderr.strip() or 'no output'}")
    merged = lines[0].strip()
    current = _git(main, "rev-parse", f"{onto}^{{tree}}").stdout.strip()
    if merged == current:
        return None, False
    changed = _git(main, "diff", "--name-only", current, merged).stdout.split()
    return (
        f"the old base {base_ref} has not merged into {onto}: it still adds changes {onto} lacks ({_names(changed)})"
    ), False


def _patch_lists(main: Path, revs: list[str]) -> dict[str, list[str]]:
    """The `git patch-id` of each non-merge commit `revs` selects, to the commits with that change, oldest first, so a
    commit can be recognised by its change under another hash (an empty commit has no change and so no id)."""
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
    found: dict[str, list[str]] = {}
    for line in ids.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) == 2:
            found.setdefault(parts[0], []).append(parts[1])
    return found


def _patch_ids(main: Path, revs: list[str]) -> dict[str, str]:
    """`_patch_lists` with the oldest commit of each change."""
    return {patch: commits[0] for patch, commits in _patch_lists(main, revs).items()}


def _patch_id_of(main: Path, args: list[str]) -> str | None:
    """The `git patch-id` of the diff `git diff <args>` prints, or None when it is empty."""
    diff = _run_bytes(main, ["diff", "--no-ext-diff", "--no-color", "--binary", "--no-renames", *args])
    if diff.returncode != 0:
        raise ConfigError(f"git diff failed: {diff.stderr.decode('utf-8', 'replace').strip() or 'no output'}")
    if not diff.stdout.strip():
        return None
    done = _run_bytes(main, ["patch-id", "--stable"], diff.stdout)
    if done.returncode != 0 or not done.stdout.strip():
        raise ConfigError(f"git patch-id failed: {done.stderr.decode('utf-8', 'replace').strip() or 'no output'}")
    return done.stdout.decode("utf-8", "replace").split()[0]


def matching_commits(main: Path, onto: str, tip: str) -> list[str]:
    """The commits of `onto` that carry the base's change, judged by change alone (which is what a squash merge leaves),
    or an empty list when the base is not shown to be in `onto` that way. Either every commit of the base that changes
    something has a commit in `onto` with the same change (`git cherry`'s rule: they were merged one by one; the newest
    such commit is taken, as a change put back after a revert is the one in effect), or the base's whole change against
    where it left `onto` is the change of one commit there (a squash merge). A base with no change matches none."""
    fork = _git(main, "merge-base", onto, tip)
    if fork.returncode != 0 or not fork.stdout.strip():
        return []
    fork_point = fork.stdout.strip()
    in_onto = _patch_lists(main, [f"{fork_point}..{onto}"])
    cherry = _git(main, "cherry", onto, tip)
    if cherry.returncode != 0:
        raise ConfigError(f"git cherry {onto} {tip} failed: {cherry.stderr.strip() or 'no output'}")
    marks = [line.split()[0] for line in cherry.stdout.splitlines() if line.strip()]
    if marks and all(mark == "-" for mark in marks):
        own = _patch_lists(main, [f"{fork_point}..{tip}"])
        found = [in_onto[patch][-1] for patch in own if patch in in_onto]
        if found:
            return list(dict.fromkeys(found))
    whole = _patch_id_of(main, [fork_point, tip])
    if whole is not None and whole in in_onto:
        return [in_onto[whole][-1]]
    return []


def reverting_commit(main: Path, onto: str, commit: str) -> str | None:
    """The commit after `commit` in `onto` that reverts it, or None. Seen: a `git revert` (its message names the
    commit), a GitHub revert (a subject `Revert "<the commit's subject>"`) and any commit whose change is exactly the
    inverse of the commit's. Not seen: a revert made by hand that differs from the inverse (a conflict resolved another
    way, other edits in it), and a change taken out by several commits; a base reverted that way is judged merged."""
    later = _git(main, "log", "--no-merges", "--format=%H%x00%s%x00%B%x1e", f"{commit}..{onto}")
    if later.returncode != 0:
        raise ConfigError(f"git log {commit}..{onto} failed: {later.stderr.strip() or 'no output'}")
    subject = _git(main, "log", "-1", "--format=%s", commit).stdout.strip()
    # GitHub puts the pull request number after a squash commit's title, and one after its revert's as well.
    plain = subject.rsplit(" (#", 1)[0] if subject.endswith(")") and " (#" in subject else subject
    for entry in later.stdout.split("\x1e"):
        parts = entry.strip("\n").split("\x00", 2)
        if len(parts) != 3:
            continue
        found, title, body = parts
        if f"This reverts commit {commit}" in body or title.startswith((f'Revert "{subject}"', f'Revert "{plain}')):
            return found
    parent = _commit(main, f"{commit}^")
    if parent is None:
        return None
    inverse = _patch_id_of(main, [commit, parent])
    if inverse is None:
        return None
    return _patch_ids(main, [f"{commit}..{onto}"]).get(inverse)


def unmerged_problem(main: Path, onto: str, tip: str, base_ref: str) -> str | None:
    """Why the old base (at `tip`) has not merged into `onto`, or None when it has. Merging it in memory shows by change
    what its commits' hashes cannot, since a squash merge leaves none of them in `onto`: if that adds nothing the base
    has merged, and if it adds changes cleanly it has not (or it did and was reverted: the work is gone). Only when it
    conflicts, because `onto` edited the base's lines after it merged, is it judged patch by patch
    (`matching_commits`) and refused if a later commit of `onto` reverts the match (`reverting_commit`, whose limits
    apply: a revert that differs from the inverse is not seen). The caller adds the way on when the check does not tell:
    the coordinator's `--base-merged-reason`."""
    problem, conflicted = _merge_conflict_or_additions(main, onto, tip, base_ref)
    if problem is None or not conflicted:
        return problem
    matched = matching_commits(main, onto, tip)
    if not matched:
        return problem
    for commit in matched:
        reverted = reverting_commit(main, onto, commit)
        if reverted is not None:
            return (
                f"the old base {base_ref} merged into {onto} as {commit[:10]} but {reverted[:10]} reverts it, so its "
                f"work is not in {onto}"
            )
    return None


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


#: The longest the check's own words are kept in the stamp (they name files, which can be long).
CHECK_ROOM = 400


def _shortened(text: str) -> str:
    """`text` cut to `CHECK_ROOM` characters, so the coordinator's reason keeps room in the stamp."""
    return text if len(text) <= CHECK_ROOM else text[: CHECK_ROOM - 3] + "..."


def _override_note(reason: str, check: str) -> dict[str, Any]:
    """The `restarted.base_merged_override` entry of the `claimed` stamp: the coordinator's reason and the check."""
    return {"base_merged_override": {"reason": reason, "check": check}}


def _reason_problem(record: UnitRecord, task: TaskFile, reason: str, check: str) -> str | None:
    """Why the `claimed` stamp would not hold `reason` (a stamp's detail is limited to `stamps.MAX_DETAIL` bytes, with
    non-ASCII text escaped), or None. Run before anything is moved aside, so a long reason costs nothing."""
    note = {
        "from_base_ref": record.base_ref,
        "from_base_commit": record.base_commit,
        # The longest name an old log is moved to (task_adopt.archive_name), and a margin.
        "log": "x" * 64,
        **_override_note(reason, check),
    }
    size = len(stamps.canonical(claimed_detail(record, task, restarted=note)))
    if size <= stamps.MAX_DETAIL:
        return None
    room = len(reason) - (size - stamps.MAX_DETAIL)
    return (
        f"--base-merged-reason is too long for the stamp it is recorded in ({size} bytes, at most "
        f"{stamps.MAX_DETAIL}; non-ASCII characters count six); shorten it to about {max(room, 0)} characters"
    )


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
            f"refused: {unmerged}; if its pull request has not merged yet, wait for its pull request to merge and run "
            "this again; if it has merged and this check cannot tell, the coordinator can go on with a recorded "
            f'reason: task start {task.task} --restart --base-merged-reason "<why it merged>" '
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
    if unmerged is not None:
        unmerged = _shortened(unmerged)
        too_long = _reason_problem(fresh, task, reason, unmerged)
        if too_long is not None:
            out(f"refused: {too_long}")
            return 1
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
        **(_override_note(reason, unmerged) if unmerged is not None else {}),
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
