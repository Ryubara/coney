# SPDX-License-Identifier: GPL-3.0-or-later
"""The `local` checks of `coney-tools repo audit`: the workspace beside the checkout, read only.

`workspace clean` and `workspace disk` act on the same things; the audit only reports them, with the command that fixes
each, so a coordinator sees disk space, merged worktrees and leftover captures next to the GitHub findings. The limits
come from `workspace` in `.github/conventions.yml`. Nothing here deletes or changes a file.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import time
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path, PurePath

from coney_tools.audit import Problem, check
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.workspace import find_layout
from coney_tools.workspace_clean import (
    Entry,
    GitWorktree,
    MergedHeads,
    Settings,
    gh_merged_heads,
    list_worktrees,
    plan,
)

_GIB = 1024**3
#: How many scratch folders are walked at once.
SCRATCH_WORKERS = 8
#: File types of screenshots and recordings, which an issue or a pull request holds once uploaded.
CAPTURE_SUFFIXES = frozenset({".png", ".jpg", ".jpeg", ".gif", ".webp", ".mp4", ".webm", ".mkv", ".mov"})


@dataclass(frozen=True)
class ScratchFolder:
    """A folder of the workspace's scratch area and what it holds."""

    name: str
    captures: int
    capture_bytes: int
    #: Whether a worktree (or a branch checked out in one) carries the folder's name.
    has_worktree: bool


@dataclass
class LocalContext:
    """What a `local` check reads: the workspace's drive, its worktrees and its scratch folders."""

    conventions: Conventions
    workspace: Path
    free_bytes: int
    #: What `workspace clean` found out about each worktree; None when git could not say (a notice explains).
    entries: list[Entry] | None = None
    #: Worktrees git lists whose folder is gone.
    stale: list[GitWorktree] = field(default_factory=list)
    #: Things in the worktrees folder that git does not know as worktrees.
    loose: list[Path] = field(default_factory=list)
    scratch: list[ScratchFolder] = field(default_factory=list)
    notices: list[str] = field(default_factory=list)


def _captures(folder: Path) -> tuple[int, int]:
    """How many capture files `folder` holds (at any depth) and their total size.

    A scratch folder can hold hundreds of thousands of files, so this walks with `os.scandir`, whose entries know
    whether they are files without a call of their own, and asks for the size of capture files only.
    """
    count = size = 0
    pending = [str(folder)]
    while pending:
        try:
            with os.scandir(pending.pop()) as entries:
                for entry in entries:
                    try:
                        if entry.is_dir(follow_symlinks=False):
                            pending.append(entry.path)
                        elif PurePath(entry.name).suffix.lower() in CAPTURE_SUFFIXES and entry.is_file():
                            count += 1
                            size += entry.stat().st_size
                    except OSError:
                        continue
        except OSError:
            continue
    return count, size


def has_origin_main(checkout: Path) -> bool:
    """Whether the checkout has an `origin/main` to judge merges against."""
    try:
        done = subprocess.run(
            ["git", "rev-parse", "--verify", "--quiet", "origin/main"],
            cwd=checkout,
            capture_output=True,
            timeout=60,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return done.returncode == 0


def build_context(
    cwd: Path,
    workspace: Path | None,
    conventions: Conventions,
    merged_heads: MergedHeads | None = None,
    now: float | None = None,
) -> LocalContext:
    """Read the workspace around `cwd` (or the folder `workspace` names) into a context; changes nothing.

    Raises ConfigError when `cwd` is not in a workspace. A missing `origin/main` is a notice, not an error: the
    worktrees cannot be judged merged without it, so only the disk and the folders are checked.
    """
    layout = find_layout(cwd, workspace)
    checkout = layout.checkout
    if not (checkout / ".git").exists():
        raise ConfigError(f"{checkout} is not a checkout; run this from the workspace's repository")
    context = LocalContext(conventions, layout.workspace, shutil.disk_usage(layout.workspace).free)
    worktrees = layout.workspace / "worktrees"
    folder = worktrees / checkout.name
    trees = list_worktrees(checkout)
    if not has_origin_main(checkout):
        context.notices.append("origin/main is missing here, so no worktree can be judged merged (git fetch origin)")
    else:
        settings = Settings(
            root=worktrees,
            checkout=checkout,
            cwd=cwd,
            now=time.time() if now is None else now,
            build_hours=0,
            worktree_hours=1.0,
            merged_heads=merged_heads or gh_merged_heads(checkout),
            busy_file=conventions.workspace.busy_marker,
            measure=False,
            builds=False,
        )
        context.entries, context.stale = plan(settings)
    known = {tree.path.resolve() for tree in trees}
    if folder.is_dir():
        context.loose = sorted(child for child in folder.iterdir() if child.resolve() not in known)
    names = {tree.path.name for tree in trees[1:]} | {tree.branch for tree in trees[1:] if tree.branch}
    scratch = layout.workspace / "scratch"
    if scratch.is_dir():
        folders = sorted(child for child in scratch.iterdir() if child.is_dir())
        # The folders are walked side by side: the walk waits on the disk.
        with ThreadPoolExecutor(max_workers=max(1, min(SCRATCH_WORKERS, len(folders)))) as pool:
            counted = list(pool.map(_captures, folders))
        for child, (count, size) in zip(folders, counted, strict=True):
            context.scratch.append(ScratchFolder(child.name, count, size, child.name in names))
    return context


# --- the checks ---------------------------------------------------------------------------------------------------


@check("ws.disk")
def disk_space(context: LocalContext) -> Iterator[Problem]:
    """The workspace's drive has at least `workspace.disk_min_free_gb` free."""
    minimum = context.conventions.workspace.disk_min_free_gb
    if context.free_bytes < minimum * _GIB:
        yield Problem(
            "workspace drive",
            f"{context.free_bytes / _GIB:.1f} GB free, the minimum is {minimum} GB: run `coney-tools workspace clean`",
        )


@check("ws.worktrees")
def merged_worktrees(context: LocalContext) -> Iterator[Problem]:
    """No worktree of merged work is left behind, and nothing but worktrees sits in the worktrees folder."""
    workspace = context.conventions.workspace
    if workspace.remove_worktree_when_merged:
        for entry in context.entries or []:
            if entry.remove:
                yield Problem(
                    f"worktree {entry.name}",
                    f"{entry.why}: `coney-tools workspace clean --apply` removes it",
                )
        for tree in context.stale:
            yield Problem(
                f"worktree {tree.path.name}", "git lists it but its folder is gone: `workspace clean --apply` prunes it"
            )
    for path in context.loose:
        kind = "folder" if path.is_dir() else "file"
        yield Problem(
            f"{path.name}", f"is a {kind} among the worktrees that git does not know as one: move or delete it"
        )


@check("ws.scratch")
def finished_scratch(context: LocalContext) -> Iterator[Problem]:
    """A scratch folder with no worktree of its name holds no captures (they are uploaded, then deleted)."""
    for folder in context.scratch:
        if folder.captures and not folder.has_worktree:
            yield Problem(
                f"scratch/{folder.name}",
                f"{folder.captures} capture file(s), {folder.capture_bytes / 1024 / 1024:.1f} MB, and no worktree of "
                "that name: upload what a pull request or issue needs, then delete them",
            )
