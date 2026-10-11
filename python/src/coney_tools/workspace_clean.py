# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools workspace clean` and `workspace disk`: keep the workspace's drive from filling up.

Every track works in its own worktree under `worktrees/coney/`, and a build of the engine is about 4 GB, so ninety
forgotten worktrees fill a drive (it happened: 3 GB were left and builds broke). `clean` removes the worktrees whose
work is merged and deletes the `build/` folders nobody has used for a while; `disk` tells scripts and hooks whether
enough space is left. Both are described in docs/guides/workspace.md#cleanup.

`clean` is a dry run unless asked to `--apply`. What it will never do:

* touch the main checkout, the folder it is run from, or a worktree that holds a `.coney-busy` file (an agent
  creates that file while it works and deletes it when it is done);
* remove a worktree with uncommitted changes (untracked files count) or with commits that are not merged. A branch
  is merged when `origin/main` contains it, or when GitHub says a pull request from it was merged at that very
  commit or a later one (a squash merge leaves the branch's commits out of `main`, so `git branch --merged` alone
  would keep every one of them);
* delete a local branch that is not merged in the same sense.

A `build/` folder is generated and ignored by git, so it is deleted from any worktree that is not busy and has not
been touched for `build_hours`, whatever state its sources are in. "Touched" is the newest modification time of the
worktree's tracked and untracked files, of its `HEAD` and of the build folder itself.
"""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.workspace import find_layout

#: The file an agent creates in its worktree while it works and deletes when it is done.
BUSY_FILE = ".coney-busy"

#: Default idle hours before a `build/` folder is deleted.
DEFAULT_BUILD_HOURS = 12.0

#: Default idle hours before a merged worktree is removed (so one just created from `main` is not taken for merged).
DEFAULT_WORKTREE_HOURS = 1.0

#: Default minimum free space in GB for `workspace disk`.
DEFAULT_MIN_GB = 30.0

#: Branches that are never deleted, whatever the state of their worktree.
PROTECTED_BRANCHES = frozenset({"main", "master"})

_GIB = 1024**3

#: Looks up the head commits of the merged pull requests from a branch; None when GitHub cannot be asked.
MergedHeads = Callable[[str], list[str] | None]


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


def gh_merged_heads(checkout: Path) -> MergedHeads:
    """A lookup of the head commits of the merged pull requests from a branch, asked of GitHub with `gh`.

    Returns None for every branch when `gh` is missing or fails once (not signed in, no network): the worktrees
    that need the answer are then kept, which is the safe side.
    """
    state = {"working": shutil.which("gh") is not None}

    def lookup(branch: str) -> list[str] | None:
        if not state["working"]:
            return None
        command = ["gh", "pr", "list", "--state", "merged", "--head", branch, "--json", "headRefOid", "--limit", "30"]
        try:
            done = subprocess.run(
                command, cwd=checkout, capture_output=True, text=True, encoding="utf-8", timeout=60, check=False
            )
            if done.returncode != 0:
                raise OSError(done.stderr)
            return [str(item["headRefOid"]) for item in json.loads(done.stdout)]
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError):
            state["working"] = False
            return None

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


def assess(tree: GitWorktree, settings: Settings) -> Entry:
    """Decide what to do with one worktree. Reads the file system and git; changes nothing."""
    entry = Entry(tree.path, tree.branch)
    path = tree.path
    if (path / BUSY_FILE).exists():
        entry.why = f"busy ({BUSY_FILE})"
        return entry
    if settings.cwd.resolve().is_relative_to(path.resolve()):
        entry.why = "the folder this runs from"
        return entry

    idle = (settings.now - last_activity(path)) / 3600.0
    build = path / "build"
    has_build = build.is_dir()
    ignored = has_build and _git(path, "check-ignore", "-q", "build").returncode == 0
    build_size = dir_size(build) if has_build else 0
    if has_build and not ignored:
        entry.build_why = "build/ is not ignored by git: left alone"
    elif has_build and idle < settings.build_hours:
        entry.build_why = f"build/ used {idle:.1f} h ago"
    elif has_build:
        entry.delete_build = True
        entry.reclaim = build_size
        entry.build_why = f"build/ idle {idle:.0f} h"

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
        if entry.why.startswith(("merged into", "pull request merged")):
            if idle < settings.worktree_hours:
                entry.why = f"{entry.why}, but used {idle:.1f} h ago"
            else:
                entry.remove = True
                entry.delete_build = False
                entry.reclaim = build_size + dir_size(path, frozenset({"build", ".git"}))
    return entry


def plan(settings: Settings) -> tuple[list[Entry], list[GitWorktree]]:
    """The entry for each worktree under the workspace's worktrees folder, and the stale ones (folder gone)."""
    trees = list_worktrees(settings.checkout)
    root = settings.root.resolve()
    entries: list[Entry] = []
    stale: list[GitWorktree] = []
    for tree in trees[1:]:  # the first is the main checkout
        if not tree.path.resolve().is_relative_to(root):
            continue
        if tree.prunable or not tree.path.is_dir():
            stale.append(tree)
        else:
            entries.append(assess(tree, settings))
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
    build_hours: float = DEFAULT_BUILD_HOURS,
    worktree_hours: float = DEFAULT_WORKTREE_HOURS,
    fetch: bool = False,
    *,
    now: float | None = None,
    merged_heads: MergedHeads | None = None,
) -> int:
    """Run `workspace clean`; 1 when any removal failed, else 0. Prints the report."""
    layout = find_layout(cwd, workspace)
    checkout = layout.checkout
    if not (checkout / ".git").exists():
        raise ConfigError(f"{checkout} is not a checkout; run this from the workspace's repository")
    if fetch:
        fetched = _git(checkout, "fetch", "--quiet", "origin", "main")
        if fetched.returncode != 0:
            print(f"warning: git fetch failed: {fetched.stderr.strip()}", file=sys.stderr)
    if _git(checkout, "rev-parse", "--verify", "--quiet", "origin/main").returncode != 0:
        raise ConfigError("origin/main does not exist here, so nothing can be judged merged (git fetch origin)")

    settings = Settings(
        root=layout.workspace / "worktrees",
        checkout=checkout,
        cwd=cwd,
        now=time.time() if now is None else now,
        build_hours=build_hours,
        worktree_hours=worktree_hours,
        merged_heads=merged_heads or gh_merged_heads(checkout),
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
    _report_loose(settings.root / checkout.name, {tree.path.resolve() for tree in list_worktrees(checkout)})

    verb = "Reclaimed" if apply else "Would reclaim"
    print(f"{verb} {_gb(reclaimed)}: {removed} worktree(s) removed, {builds} build folder(s) deleted.")
    if apply:
        free_after = shutil.disk_usage(layout.workspace).free
        print(f"Free space: {_gb(free_before)} -> {_gb(free_after)}")
    return 1 if failures else 0


def _report_loose(folder: Path, known: set[Path]) -> None:
    """Name the folders under `worktrees/coney/` that git does not know as worktrees (never touched)."""
    try:
        loose = sorted(child.name for child in folder.iterdir() if child.is_dir() and child.resolve() not in known)
    except OSError:
        return
    if loose:
        print(f"Not worktrees, left alone: {', '.join(loose)}")


def run_disk(cwd: Path, workspace: Path | None, min_gb: float = DEFAULT_MIN_GB) -> int:
    """Run `workspace disk`: print the free space of the workspace's drive; 1 when it is below `min_gb`."""
    layout = find_layout(cwd, workspace)
    free = shutil.disk_usage(layout.workspace).free
    low = free < min_gb * _GIB
    advice = ": LOW, run workspace clean" if low else ""
    print(f"Free space on the workspace drive: {_gb(free)} (minimum {min_gb:g} GB){advice}")
    return 1 if low else 0
