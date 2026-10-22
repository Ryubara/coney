# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools workspace clean` and `workspace disk`: keep the workspace's drive from filling up.

Every track works in its own worktree under `worktrees/coney/`, and a build of the engine is about 4 GB, so ninety
forgotten worktrees fill a drive (it happened: 3 GB were left and builds broke). `clean` removes the worktrees whose
work is merged and deletes the `build/` folders nobody has used for a while; `disk` tells scripts and hooks whether
enough space is left. Both are described in docs/guides/workspace.md#cleanup.

`clean` also lists the processes left behind, and stops them with `--apply`: agent shells left waiting on a run that
died, and test runs whose caller is gone (stale_processes.py).

`clean` is a dry run unless asked to `--apply`. What it will never do:

* touch the main checkout, the folder it is run from, a worktree that holds or waits for a build slot, or a worktree
  that holds a `.coney-busy` file (an agent creates that file while it works and deletes it when it is done). An
  empty marker older than the idle threshold no longer counts: its agent is long gone. A marker with something in
  it (an agent id) always counts;
* remove a worktree with uncommitted changes (untracked files count) or with commits that are not merged. A branch
  is merged when `origin/main` contains it, or when GitHub says a pull request from it was merged at that very
  commit or a later one (a squash merge leaves the branch's commits out of `main`, so `git branch --merged` alone
  would keep every one of them);
* delete a local branch that is not merged in the same sense.

A `build/` folder is generated and ignored by git, so it is deleted from any worktree that is not busy and has not
been touched for `build_hours` (`workspace.build_idle_hours`, 24 by default), whatever state its sources are in.
"Touched" is the newest modification time of the worktree's tracked and untracked files, of its `HEAD` and of the
build folder itself.

**Disk pressure.** When the drive is still under the floor (`workspace.disk_min_free_gb`) after that, `clean` deletes
the `build/` folders of the other worktrees, oldest last use first, until the free space reaches the floor plus
`workspace.disk_margin_gb`. Only `build/` folders go in this mode, never a worktree or a source file, so uncommitted
and unmerged work is safe. A worktree that is busy, that the command runs from, or that holds or waits for a build
slot is skipped.

Who holds a slot is read from the build queue's own claim and ticket files each time (`build_claims.live_worktrees`,
with the queue's liveness rules), never from a listing kept earlier, and read again just before each deletion: a
stale listing once deleted the build folders of two running builds. The same functions tell `coney-tools build` which
worktrees hold a build folder (build_cap.py).
"""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import sys
import threading
import time
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from coney_tools import build_claims, stale_processes
from coney_tools.config import ConfigError
from coney_tools.conventions import Workspace, load_conventions
from coney_tools.workspace import find_layout

#: The file an agent creates in its worktree while it works and deletes when it is done.
BUSY_FILE = ".coney-busy"

#: Default idle hours before a `build/` folder is deleted (it is only a rebuild; `workspace.build_idle_hours`).
DEFAULT_BUILD_HOURS = 24.0

#: Default GB above the floor that disk-pressure mode frees (`workspace.disk_margin_gb`).
DEFAULT_MARGIN_GB = 10.0

#: The build queue's folder under the workspace's `scratch/` when `coney.local.toml` does not name one.
BUILD_CLAIMS_DIR = "build-claims"

#: Default idle hours before a merged worktree is removed (so one just created from `main` is not taken for merged).
DEFAULT_WORKTREE_HOURS = 1.0

#: Default minimum free space in GB for `workspace disk`.
DEFAULT_MIN_GB = 30.0

#: Branches that are never deleted, whatever the state of their worktree.
PROTECTED_BRANCHES = frozenset({"main", "master"})

_GIB = 1024**3

#: Looks up the head commits of the merged pull requests from a branch; None when GitHub cannot be asked.
MergedHeads = Callable[[str], list[str] | None]

#: Runs a `gh` command in a folder; replaced by a fake in tests.
GhRunner = Callable[[list[str], Path], "subprocess.CompletedProcess[str]"]

#: How many merged pull requests the one `gh` call asks for (the repository has a few hundred).
MERGED_PR_LIMIT = 1000


@dataclass(frozen=True)
class GitWorktree:
    """One line of `git worktree list`."""

    path: Path
    branch: str | None
    locked: bool
    prunable: bool


@dataclass
class Entry:
    """What `clean` found out about one worktree and what it will do."""

    path: Path
    branch: str | None
    #: The worktree and its local branch are to be removed.
    remove: bool = False
    #: Why it is removed or kept, in a few words.
    why: str = ""
    #: The `build/` folder is to be deleted (a worktree that stays).
    delete_build: bool = False
    #: Bytes the action frees: the whole worktree when removed, else its build folder when deleted.
    reclaim: int = 0
    #: Why a build folder is kept or deleted, when it exists.
    build_why: str = ""
    #: The build folder exists and git ignores it, so disk pressure may delete it even from a worktree that stays.
    build_deletable: bool = False
    #: Bytes of that build folder and hours since the worktree was last used (set with `build_deletable`).
    build_bytes: int = 0
    build_idle: float = 0.0
    #: Why the worktree is busy (the marker, or a build slot); empty when it is not. A busy worktree is judged no
    #: further.
    busy: str = ""
    #: Its branch is merged (into `origin/main` or by a merged pull request), whatever else keeps it.
    merged: bool = False

    @property
    def name(self) -> str:
        """The worktree's folder name."""
        return self.path.name


# --- helpers over the file system and git ----------------------------------------------------------------------


def _fs(path: Path) -> str:
    """`path` in the form the file system calls accept: with Windows' long-path prefix, so deep build trees work."""
    text = str(path.absolute())
    if os.name == "nt" and not text.startswith("\\\\"):
        return "\\\\?\\" + text
    return text


def _git(folder: Path, *args: str) -> subprocess.CompletedProcess[str]:
    """Run git in `folder`, capturing its text output."""
    return subprocess.run(
        ["git", "-C", str(folder), *args], capture_output=True, text=True, encoding="utf-8", check=False
    )


def dir_size(path: Path, skip: frozenset[str] = frozenset()) -> int:
    """The bytes of the files under `path`; links are counted as themselves, never followed.

    `skip` names top-level entries to leave out. A folder that cannot be read counts as 0.
    """
    total = 0
    pending = [(_fs(path), True)]
    while pending:
        folder, top = pending.pop()
        try:
            with os.scandir(folder) as entries:
                for entry in entries:
                    if top and entry.name in skip:
                        continue
                    try:
                        if entry.is_symlink() or entry.is_junction():
                            continue
                        if entry.is_dir(follow_symlinks=False):
                            pending.append((entry.path, False))
                        else:
                            total += entry.stat(follow_symlinks=False).st_size
                    except OSError:
                        continue
        except OSError:
            continue
    return total


def remove_tree(path: Path) -> None:
    """Delete the folder `path`, clearing read-only attributes on the way (git's pack files and some tools' output
    are read-only, which Windows refuses to delete)."""

    def make_writable_and_retry(function: Callable[[str], object], target: str, _error: BaseException) -> None:
        Path(target).chmod(stat.S_IWRITE | stat.S_IREAD | stat.S_IEXEC)
        function(target)

    shutil.rmtree(_fs(path), onexc=make_writable_and_retry)


def _mtime(path: str | Path) -> float:
    """The modification time of `path`, 0 when it cannot be read."""
    try:
        return os.lstat(path).st_mtime
    except OSError:
        return 0.0


def last_activity(worktree: Path) -> float:
    """When the worktree was last touched: the newest modification time of its tracked and untracked files, of the
    folder git keeps for it (`HEAD`, and the log that every commit and checkout appends to) and of its `build/`
    folder with the entries one and two levels inside it (a running build writes there)."""
    newest = 0.0
    listed = _git(worktree, "ls-files", "-z", "--cached", "--others", "--exclude-standard")
    for name in listed.stdout.split("\0"):
        if name:
            newest = max(newest, _mtime(worktree / name))
    gitdir = _git(worktree, "rev-parse", "--absolute-git-dir").stdout.strip()
    if gitdir:
        newest = max(newest, _mtime(Path(gitdir) / "HEAD"), _mtime(Path(gitdir) / "logs" / "HEAD"))
    build = worktree / "build"
    newest = max(newest, _mtime(build))
    try:
        with os.scandir(_fs(build)) as children:
            for child in children:
                newest = max(newest, _mtime(child.path))
                if child.is_dir(follow_symlinks=False):
                    with os.scandir(child.path) as grandchildren:
                        for grandchild in grandchildren:
                            newest = max(newest, _mtime(grandchild.path))
    except OSError:
        pass
    return newest


def list_worktrees(checkout: Path) -> list[GitWorktree]:
    """The worktrees git knows of, the main checkout first."""
    listed = _git(checkout, "worktree", "list", "--porcelain")
    if listed.returncode != 0:
        raise ConfigError(f"git worktree list failed in {checkout}: {listed.stderr.strip()}")
    result: list[GitWorktree] = []
    for block in listed.stdout.split("\n\n"):
        path: Path | None = None
        branch: str | None = None
        locked = prunable = False
        for line in block.splitlines():
            key, _, value = line.partition(" ")
            if key == "worktree":
                path = Path(value)
            elif key == "branch":
                branch = value.removeprefix("refs/heads/")
            elif key == "locked":
                locked = True
            elif key == "prunable":
                prunable = True
        if path is not None:
            result.append(GitWorktree(path, branch, locked, prunable))
    return result


def _run_gh(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    """Run one `gh` command in `cwd`, capturing its output; raises OSError or SubprocessError when it cannot run."""
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True, encoding="utf-8", timeout=120, check=False)


def gh_merged_heads(checkout: Path, run: GhRunner | None = None, *, limit: int = MERGED_PR_LIMIT) -> MergedHeads:
    """A lookup of the head commits of the merged pull requests from a branch, asked of GitHub with `gh`.

    GitHub is asked once, for the newest `limit` merged pull requests of every branch, the first time any worktree
    needs an answer; every later branch is answered from that list. (One `gh pr list --head <branch>` per worktree took
    9.7 minutes for about 90 of them; the single call takes seconds.) A branch the list does not hold has no merged
    pull request, unless the list was full, when an older one may be missing and the answer is unknown.

    Returns None for every branch when `gh` is missing or fails (not signed in, no network, unreadable answer): the
    worktrees that need the answer are then kept, which is the safe side. `run` replaces the `gh` runner in tests.
    """
    runner = run or _run_gh
    # Worktrees are judged side by side: the first thread to need the answer asks, the others wait for it.
    lock = threading.Lock()
    asked = False
    heads: dict[str, list[str]] | None = None
    full = False

    def ask() -> None:
        # One call, whatever the outcome: a failure is not retried for each of the other worktrees.
        nonlocal asked, heads, full
        asked = True
        if run is None and shutil.which("gh") is None:
            return
        command = ["gh", "pr", "list", "--state", "merged", "--limit", str(limit), "--json", "headRefName,headRefOid"]
        try:
            done = runner(command, checkout)
            if done.returncode != 0:
                return
            pulls = json.loads(done.stdout)
            if not isinstance(pulls, list):
                return
            found: dict[str, list[str]] = {}
            for pull in pulls:
                found.setdefault(str(pull["headRefName"]), []).append(str(pull["headRefOid"]))
            heads, full = found, len(pulls) >= limit
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError):
            heads = None

    def lookup(branch: str) -> list[str] | None:
        with lock:
            if not asked:
                ask()
        if heads is None:
            return None
        if branch in heads:
            return heads[branch]
        return None if full else []

    return lookup


# --- deciding -----------------------------------------------------------------------------------------------


def _unmerged_commits(worktree: Path, base: str) -> int | None:
    """How many commits of the worktree's HEAD are not in `base`; None when git cannot say."""
    counted = _git(worktree, "rev-list", "--count", f"{base}..HEAD")
    return int(counted.stdout.strip()) if counted.returncode == 0 and counted.stdout.strip().isdigit() else None


def _covered_by_merged_pr(worktree: Path, heads: list[str]) -> bool:
    """Whether HEAD is a merged pull request's head commit or an ancestor of one (the pull request had more)."""
    tip = _git(worktree, "rev-parse", "HEAD").stdout.strip()
    if not tip:
        return False
    for head in heads:
        if head == tip or _git(worktree, "merge-base", "--is-ancestor", tip, head).returncode == 0:
            return True
    return False


@dataclass(frozen=True)
class Settings:
    """The thresholds and collaborators of one `clean` run."""

    #: Where the worktrees live (`<workspace>/worktrees`); anything outside it is left alone.
    root: Path
    checkout: Path
    #: The folder the command runs from; its worktree is never touched.
    cwd: Path
    now: float
    build_hours: float
    worktree_hours: float
    merged_heads: MergedHeads
    #: The ref a branch must be contained in.
    base: str = "origin/main"
    #: The file whose presence marks a worktree as in use (`workspace.busy_marker` of the conventions).
    busy_file: str = BUSY_FILE
    #: Hours after which an empty busy file no longer protects its worktree (its agent died without deleting it).
    marker_hours: float = DEFAULT_BUILD_HOURS
    #: The names of the worktrees that hold a build slot or wait for one, read from the build queue each time it is
    #: called (never kept): see `build_claims.live_worktrees`.
    live: Callable[[], set[str]] = lambda: set()
    #: Whether to add up the sizes a removal frees (a walk over every build folder; the audit skips it).
    measure: bool = True
    #: Whether to judge `build/` folders too. That needs every worktree's last activity (a stat of every tracked
    #: file); the audit only reports worktrees and skips it, reading the activity of a merged one alone.
    builds: bool = True
    #: How many worktrees are judged at once (git and the file system are waited for, not computed).
    workers: int = 8


def busy_reason(path: Path, settings: Settings) -> str:
    """Why the worktree at `path` is in use, or "" when it is not: its busy file, or a build slot or queue place.

    An empty busy file older than `marker_hours` is ignored (an agent that dies leaves one behind, and it would
    protect its worktree for ever); a busy file with something in it always protects.
    """
    try:
        marker = os.lstat(path / settings.busy_file)
    except OSError:
        marker = None
    if marker is not None:
        forgotten = marker.st_size == 0 and (settings.now - marker.st_mtime) / 3600.0 > settings.marker_hours
        if not forgotten:
            return f"busy ({settings.busy_file})"
    if path.name in settings.live():
        return "holds or waits for a build slot"
    return ""


def assess(tree: GitWorktree, settings: Settings) -> Entry:
    """Decide what to do with one worktree. Reads the file system and git; changes nothing."""
    entry = Entry(tree.path, tree.branch)
    path = tree.path
    entry.busy = busy_reason(path, settings)
    if entry.busy:
        entry.why = entry.busy
        return entry
    if settings.cwd.resolve().is_relative_to(path.resolve()):
        entry.why = "the folder this runs from"
        return entry

    # The last activity walks every file of the worktree, so it is read once and only when a decision needs it.
    activity: list[float] = []

    def idle() -> float:
        if not activity:
            activity.append((settings.now - last_activity(path)) / 3600.0)
        return activity[0]

    build = path / "build"
    has_build = settings.builds and build.is_dir()
    ignored = has_build and _git(path, "check-ignore", "-q", "build").returncode == 0
    build_size = dir_size(build) if has_build and settings.measure else 0
    if has_build and ignored:
        entry.build_deletable = True
        entry.build_bytes = build_size
        entry.build_idle = idle()
    if has_build and not ignored:
        entry.build_why = "build/ is not ignored by git: left alone"
    elif has_build and idle() < settings.build_hours:
        entry.build_why = f"build/ used {idle():.1f} h ago"
    elif has_build:
        entry.delete_build = True
        entry.reclaim = build_size
        entry.build_why = f"build/ idle {idle():.0f} h"

    # Worktree removal: nothing may be lost.
    if tree.locked:
        entry.why = "locked by git"
    elif (status := _git(path, "status", "--porcelain")).returncode != 0 or status.stdout.strip():
        entry.why = "uncommitted changes"
    else:
        ahead = _unmerged_commits(path, settings.base)
        if ahead is None:
            entry.why = f"cannot compare with {settings.base}"
        elif ahead > 0:
            heads = settings.merged_heads(tree.branch) if tree.branch else None
            if tree.branch is None:
                entry.why = f"detached HEAD with {ahead} commit(s) not in {settings.base}"
            elif heads is None:
                entry.why = f"{ahead} commit(s) not in {settings.base}; GitHub not asked"
            elif _covered_by_merged_pr(path, heads):
                entry.why = "pull request merged"
            else:
                entry.why = f"{ahead} unmerged commit(s)"
        else:
            entry.why = f"merged into {settings.base}"
        entry.merged = entry.why.startswith(("merged into", "pull request merged"))
        if entry.merged:
            if idle() < settings.worktree_hours:
                entry.why = f"{entry.why}, but used {idle():.1f} h ago"
            else:
                entry.remove = True
                entry.delete_build = False
                entry.reclaim = build_size + (dir_size(path, frozenset({"build", ".git"})) if settings.measure else 0)
    return entry


def plan(settings: Settings) -> tuple[list[Entry], list[GitWorktree]]:
    """The entry for each worktree under the workspace's worktrees folder, and the stale ones (folder gone)."""
    trees = list_worktrees(settings.checkout)
    root = settings.root.resolve()
    present: list[GitWorktree] = []
    stale: list[GitWorktree] = []
    for tree in trees[1:]:  # the first is the main checkout
        if not tree.path.resolve().is_relative_to(root):
            continue
        if tree.prunable or not tree.path.is_dir():
            stale.append(tree)
        else:
            present.append(tree)
    # Each worktree is judged by a few git commands and a walk of its files, which wait on the disk and on git, so a
    # handful at once is several times faster than one after another (90 worktrees took half a minute).
    with ThreadPoolExecutor(max_workers=max(1, min(settings.workers, len(present)))) as pool:
        entries = list(pool.map(lambda tree: assess(tree, settings), present))
    return sorted(entries, key=lambda entry: entry.name), stale


# --- doing it ---------------------------------------------------------------------------------------------


def _remove_worktree(entry: Entry, checkout: Path) -> str | None:
    """Remove the worktree and its branch; None on success, else what went wrong."""
    try:
        if (entry.path / "build").is_dir():
            remove_tree(entry.path / "build")
    except OSError as error:
        return f"cannot delete build/: {error}"
    # No --force: git itself refuses a worktree that turned out not to be clean.
    removed = _git(checkout, "worktree", "remove", str(entry.path))
    if removed.returncode != 0:
        return f"git worktree remove: {removed.stderr.strip()}"
    if entry.branch and entry.branch not in PROTECTED_BRANCHES:
        deleted = _git(checkout, "branch", "-D", entry.branch)
        if deleted.returncode != 0:
            return f"worktree removed, branch kept: {deleted.stderr.strip()}"
    return None


def _delete_build(entry: Entry) -> str | None:
    """Delete the worktree's build folder; None on success, else what went wrong."""
    try:
        remove_tree(entry.path / "build")
    except OSError as error:
        return f"cannot delete build/: {error}"
    return None


def _gb(size: float) -> str:
    """A size in GB for the report."""
    return f"{size / _GIB:.1f} GB"


def run_clean(
    cwd: Path,
    workspace: Path | None,
    apply: bool,
    build_hours: float | None = None,
    worktree_hours: float = DEFAULT_WORKTREE_HOURS,
    fetch: bool = False,
    *,
    min_gb: float | None = None,
    margin_gb: float | None = None,
    now: float | None = None,
    merged_heads: MergedHeads | None = None,
    shell_hours: float = stale_processes.DEFAULT_SHELL_HOURS,
    idle_hours: float = stale_processes.DEFAULT_IDLE_HOURS,
) -> int:
    """Run `workspace clean`; 1 when any removal or stop failed, else 0. Prints the report: the worktrees and build
    folders, the disk-pressure deletions when the drive is under its floor, then the stale processes
    (stale_processes.py).

    `build_hours`, `min_gb` (the floor) and `margin_gb` default to `workspace.build_idle_hours`,
    `workspace.disk_min_free_gb` and `workspace.disk_margin_gb` of the conventions file, and to the built-in values
    when that file cannot be read.
    """
    layout = find_layout(cwd, workspace)
    checkout = layout.checkout
    if not (checkout / ".git").exists():
        raise ConfigError(f"{checkout} is not a checkout; run this from the workspace's repository")
    known = _conventions_workspace(checkout)
    if fetch:
        fetched = _git(checkout, "fetch", "--quiet", "origin", "main")
        if fetched.returncode != 0:
            print(f"warning: git fetch failed: {fetched.stderr.strip()}", file=sys.stderr)
    if _git(checkout, "rev-parse", "--verify", "--quiet", "origin/main").returncode != 0:
        raise ConfigError("origin/main does not exist here, so nothing can be judged merged (git fetch origin)")

    if min_gb is None:
        min_gb = float(known.disk_min_free_gb) if known is not None else DEFAULT_MIN_GB
    if margin_gb is None:
        margin_gb = float(known.disk_margin_gb) if known is not None else DEFAULT_MARGIN_GB
    settings = default_settings(
        layout.workspace,
        checkout,
        cwd,
        build_hours=build_hours,
        worktree_hours=worktree_hours,
        now=now,
        merged_heads=merged_heads,
    )
    free_before = shutil.disk_usage(layout.workspace).free
    print(f"Workspace: {layout.workspace}" + ("" if apply else " (dry run: nothing is changed; --apply acts)"))
    print(f"Free space: {_gb(free_before)}")
    entries, stale = plan(settings)

    failures = 0
    reclaimed = removed = builds = 0
    for entry in entries:
        if entry.remove:
            line = f"{entry.name}: {entry.why} ({_gb(entry.reclaim)}" + (", delete branch)" if entry.branch else ")")
            verb = "remove" if apply else "would remove"
            print(f"  {verb:<13}{line}", flush=True)
            if apply and _started_building(entry, settings):
                print("    skipped: a build started in it")
                continue
            problem = _remove_worktree(entry, checkout) if apply else None
            if problem:
                failures += 1
                print(f"    FAILED: {problem}")
            else:
                reclaimed += entry.reclaim
                removed += 1
        elif entry.delete_build:
            verb = "delete build" if apply else "would delete"
            print(f"  {verb:<13}{entry.name}: {entry.build_why}, {_gb(entry.reclaim)}; kept: {entry.why}", flush=True)
            if apply and _started_building(entry, settings):
                print("    skipped: a build started in it")
                continue
            problem = _delete_build(entry) if apply else None
            if problem:
                failures += 1
                print(f"    FAILED: {problem}")
            else:
                reclaimed += entry.reclaim
                builds += 1
        else:
            note = f"; {entry.build_why}" if entry.build_why else ""
            print(f"  {'keep':<13}{entry.name}: {entry.why}{note}")
    for tree in stale:
        print(f"  {'prune' if apply else 'would prune':<13}{tree.path.name}: folder is gone")
    if apply and stale:
        _git(checkout, "worktree", "prune")

    # Disk pressure: a dry run has no new free space to read, so it adds up what the lines above would free.
    def free_now() -> int:
        return shutil.disk_usage(layout.workspace).free if apply else free_before + reclaimed

    pressure = _relieve_pressure(entries, settings, min_gb, margin_gb, free_now, apply)
    failures += pressure.failures
    reclaimed += pressure.reclaimed
    builds += pressure.deleted
    _report_loose(settings.root / checkout.name, {tree.path.resolve() for tree in list_worktrees(checkout)})
    failures += stale_processes.run(apply, shell_hours, idle_hours)

    verb = "Reclaimed" if apply else "Would reclaim"
    print(f"{verb} {_gb(reclaimed)}: {removed} worktree(s) removed, {builds} build folder(s) deleted.")
    if apply:
        free_after = shutil.disk_usage(layout.workspace).free
        print(f"Free space: {_gb(free_before)} -> {_gb(free_after)}")
    return 1 if failures else 0


@dataclass
class Pressure:
    """What disk-pressure mode did: build folders deleted, bytes freed and deletions that failed."""

    deleted: int = 0
    reclaimed: int = 0
    failures: int = 0


def _started_building(entry: Entry, settings: Settings) -> bool:
    """Whether a build took a slot or a place in the queue in this worktree since it was judged: the build queue is
    read again just before each deletion, because a listing from the start of a long run can be out of date."""
    return entry.name in settings.live()


def live_slots(workspace: Path, checkout: Path) -> Callable[[], set[str]]:
    """A function that reads, each time it is called, the names of the worktrees holding or waiting for a build slot
    from the build queue of `checkout` (its `build_claims_dir`, else `scratch/build-claims` of `workspace`)."""

    def live() -> set[str]:
        try:
            claims = build_claims.default_slots(build_claims.MAIN_LANE, checkout).claims_dir
        except ConfigError:
            claims = workspace / "scratch" / BUILD_CLAIMS_DIR
        return build_claims.live_worktrees(claims)

    return live


def default_settings(
    workspace: Path,
    checkout: Path,
    cwd: Path,
    *,
    build_hours: float | None = None,
    worktree_hours: float = DEFAULT_WORKTREE_HOURS,
    now: float | None = None,
    merged_heads: MergedHeads | None = None,
) -> Settings:
    """The settings of a run in `workspace` (the folder holding `repos/`, `worktrees/` and `scratch/`): the idle
    hours from the conventions file (`workspace.build_idle_hours`; the built-in value when it cannot be read), the
    busy marker's name from it, GitHub asked with `gh`, and the live build slots read from the build queue of the
    checkout (its `build_claims_dir`, else `scratch/build-claims`) each time `Settings.live` is called."""
    known = _conventions_workspace(checkout)
    if build_hours is None:
        build_hours = float(known.build_idle_hours) if known is not None else DEFAULT_BUILD_HOURS

    return Settings(
        root=workspace / "worktrees",
        checkout=checkout,
        cwd=cwd,
        now=time.time() if now is None else now,
        build_hours=build_hours,
        worktree_hours=worktree_hours,
        merged_heads=merged_heads or gh_merged_heads(checkout),
        busy_file=known.busy_marker if known is not None else BUSY_FILE,
        marker_hours=build_hours,
        live=live_slots(workspace, checkout),
    )


def _relieve_pressure(
    entries: list[Entry],
    settings: Settings,
    floor_gb: float,
    margin_gb: float,
    free_now: Callable[[], int],
    apply: bool,
) -> Pressure:
    """When the free space is under `floor_gb`, delete `build/` folders, the one last used longest ago first, until it
    reaches `floor_gb + margin_gb` or none is left. Only build folders of worktrees `assess` found idle-able (not busy,
    not the one running this, ignored by git) that hold no build slot and wait for none; nothing else is touched."""
    result = Pressure()
    free = free_now()
    if free >= floor_gb * _GIB:
        return result
    target = (floor_gb + margin_gb) * _GIB
    print(
        f"Disk pressure: {_gb(free)} free is under the {floor_gb:g} GB floor; deleting idle build folders, "
        f"oldest first, up to {_gb(target)}"
    )
    candidates = sorted(
        (
            entry
            for entry in entries
            if entry.build_deletable and not entry.delete_build and not entry.remove and (entry.path / "build").is_dir()
        ),
        key=lambda entry: -entry.build_idle,
    )
    for entry in candidates:
        if free >= target:
            break
        if apply and _started_building(entry, settings):
            print(f"  {entry.name}: skipped, a build started in it")
            continue
        problem = _delete_build(entry) if apply else None
        if problem:
            result.failures += 1
            print(f"  {entry.name}: FAILED: {problem}")
            continue
        result.deleted += 1
        result.reclaimed += entry.build_bytes
        after = free_now() if apply else free + entry.build_bytes
        verb = "pressure" if apply else "would delete"
        print(
            f"  {verb:<13}{entry.name}: build/ last used {entry.build_idle:.1f} h ago, {_gb(entry.build_bytes)}; "
            f"free {_gb(free)} -> {_gb(after)}",
            flush=True,
        )
        free = after
    if free < floor_gb * _GIB:
        print(f"Free space is still under the {floor_gb:g} GB floor: no other build folder can be deleted")
    return result


def _report_loose(folder: Path, known: set[Path]) -> None:
    """Name the folders under `worktrees/coney/` that git does not know as worktrees (never touched)."""
    try:
        loose = sorted(child.name for child in folder.iterdir() if child.is_dir() and child.resolve() not in known)
    except OSError:
        return
    if loose:
        print(f"Not worktrees, left alone: {', '.join(loose)}")


def _conventions_workspace(checkout: Path) -> Workspace | None:
    """The `workspace` section of the checkout's conventions file; None when it has none or cannot be read, so a
    workspace without a usable checkout still cleans with the built-in defaults."""
    try:
        return load_conventions(checkout).workspace
    except ConfigError:
        return None


def run_disk(cwd: Path, workspace: Path | None, min_gb: float | None = None) -> int:
    """Run `workspace disk`: print the free space of the workspace's drive; 1 when it is below `min_gb`.

    Without `min_gb` the limit is `workspace.disk_min_free_gb` of the conventions file (30 GB when that is unreadable).
    """
    layout = find_layout(cwd, workspace)
    if min_gb is None:
        known = _conventions_workspace(layout.checkout)
        min_gb = float(known.disk_min_free_gb) if known is not None else DEFAULT_MIN_GB
    free = shutil.disk_usage(layout.workspace).free
    low = free < min_gb * _GIB
    advice = ": LOW, run workspace clean" if low else ""
    print(f"Free space on the workspace drive: {_gb(free)} (minimum {min_gb:g} GB){advice}")
    return 1 if low else 0
