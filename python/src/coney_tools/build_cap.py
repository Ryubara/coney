# SPDX-License-Identifier: GPL-3.0-or-later
"""The cap on how many worktrees may hold a build folder (docs/guides/agents.md#build-cap).

A build folder is about 7 GB and the drive must keep 30 GB free, so only so many worktrees can hold one at once, and
a machine has only so many cores. `coney-tools build` therefore refuses to configure a NEW build folder (one that
does not exist yet) when the worktrees already holding a build folder are at the cap (`build_worktree_cap` in
`coney.local.toml`, 7 by default, 0 for no cap). The refusal lists those worktrees with their size, age and state and
names the command that makes room, `coney-tools workspace clean --apply` (workspace_clean.py), which uses the same
judgement of "merged", "busy" and "idle" as the list does.

What counts: the worktrees git lists under the workspace's `worktrees/` folder whose `build/` folder holds anything,
in any preset. The main checkout's `build/` (the coordinator's play build) never counts, and a run in the main
checkout is never refused. A second folder in a worktree that already holds one adds no worktree, so it is allowed;
a folder that exists keeps building. Two agents configuring at the same moment may both pass, so the cap is
"about": it is a brake on a pile-up, not a lock.
"""

from __future__ import annotations

import os
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, replace
from pathlib import Path

from coney_tools.config import ConfigError, main_checkout, workspace_root
from coney_tools.workspace_clean import (
    Entry,
    GitWorktree,
    Settings,
    assess,
    default_settings,
    dir_size,
    last_activity,
    list_worktrees,
)

#: The command that makes room, as the refusal names it.
CLEANUP_COMMAND = "uv run --project python coney-tools workspace clean --apply"

_GIB = 1024**3


class BuildCapError(ConfigError):
    """A new build folder was refused because the cap on worktrees holding builds is reached."""


@dataclass(frozen=True)
class Holder:
    """One worktree that holds a build folder, as the refusal lists it."""

    name: str
    #: Bytes of its `build/` folder.
    size: int
    #: Hours since the worktree was last touched.
    idle_hours: float
    #: What is known of it: busy, merged, or still unmerged work; and whether `workspace clean` would free it.
    state: str
    #: `workspace clean --apply` would remove the worktree or delete its build folder now.
    frees: bool


def holds_build(worktree: Path) -> bool:
    """Whether the worktree has a `build/` folder with anything in it (an empty one is no build)."""
    try:
        with os.scandir(worktree / "build") as entries:
            return next(entries, None) is not None
    except OSError:
        return False


def holding_worktrees(main: Path, workspace: Path) -> list[GitWorktree]:
    """The worktrees git lists under `workspace/worktrees/` (never the main checkout) that hold a build folder."""
    root = (workspace / "worktrees").resolve()
    found = []
    for tree in list_worktrees(main)[1:]:
        if tree.path.is_dir() and tree.path.resolve().is_relative_to(root) and holds_build(tree.path):
            found.append(tree)
    return found


def _state(entry: Entry) -> tuple[str, bool]:
    """How the list describes a worktree, and whether `workspace clean --apply` would free it right now."""
    if entry.busy:
        return entry.busy, False
    if entry.remove:
        return f"{entry.why}: workspace clean removes it", True
    if entry.delete_build:
        return f"{entry.why}; its build is idle: workspace clean deletes it", True
    if entry.merged:
        return f"PR merged, {entry.why.split(', ', 1)[-1]}", False
    return entry.why or "not merged", False


def describe(trees: list[GitWorktree], settings: Settings) -> list[Holder]:
    """The size, age and state of each worktree, the longest idle first. Slow (it walks the build folders and asks
    GitHub), so it runs only to explain a refusal."""

    # The sizes are measured below; assess need not walk the build folders a second time.
    judging = replace(settings, measure=False)

    def one(tree: GitWorktree) -> Holder:
        entry = assess(tree, judging)
        state, frees = _state(entry)
        idle = (settings.now - last_activity(tree.path)) / 3600.0
        return Holder(tree.path.name, dir_size(tree.path / "build"), idle, state, frees)

    with ThreadPoolExecutor(max_workers=max(1, min(settings.workers, len(trees)))) as pool:
        holders = list(pool.map(one, trees))
    return sorted(holders, key=lambda holder: -holder.idle_hours)


def refusal(folder: Path, cap: int, holders: list[Holder], idle_hours: float) -> str:
    """The message of a refused build: what is full, who holds the builds and how to make room."""
    lines = [
        f"build refused: {len(holders)} worktrees already hold a build folder (the cap is {cap}, "
        f"build_worktree_cap in coney.local.toml) and {folder} would be a new one. Each build is about 7 GB and "
        "the drive must keep 30 GB free; existing build folders keep building."
    ]
    width = max(len(holder.name) for holder in holders)
    for holder in holders:
        lines.append(
            f"  {holder.name:<{width}}  {holder.size / _GIB:5.1f} GB  idle {holder.idle_hours:4.0f} h  {holder.state}"
        )
    freed = sum(holder.frees for holder in holders)
    lines.append(
        f"Make room with `{CLEANUP_COMMAND}` (a dry run without --apply): it removes worktrees whose pull request "
        f"merged and deletes build folders idle over {idle_hours:g} h, never a busy one; right now it would free "
        f"{freed} of these. Delete a build/ folder you no longer need by hand, then run the build again."
    )
    return "\n".join(lines)


def check(root: Path, folder: Path, cap: int, now: float | None = None) -> None:
    """Refuse (BuildCapError) to configure the new build folder `folder` of the checkout `root` when `cap` or more
    worktrees already hold a build folder; do nothing in every other case (see the module comment). A workspace git
    cannot list is not judged: the cap is a convenience, never a reason a build fails for another reason."""
    if cap <= 0 or folder.exists():
        return
    main = main_checkout(root)
    workspace = workspace_root(root)
    try:
        if not root.resolve().is_relative_to((workspace / "worktrees").resolve()):
            return  # the main checkout, or a clone outside the workspace's worktrees: not the cap's business
        holding = holding_worktrees(main, workspace)
    except (ConfigError, OSError):
        return
    if len(holding) < cap or any(tree.path.resolve() == root.resolve() for tree in holding):
        return
    settings = default_settings(workspace, main, root, now=time.time() if now is None else now)
    holders = describe(holding, settings)
    raise BuildCapError(refusal(folder, cap, holders, settings.build_hours))
