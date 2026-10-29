# SPDX-License-Identifier: GPL-3.0-or-later
"""Where a checkout's build trees live: the one resolver every tool uses (docs/guides/workspace.md#build-root).

By default a checkout builds in its own `build/` folder (`build/dev`, `build/ci`, ...), as CMakePresets.json says.
A machine whose system drive should not hold the builds sets `build_root` in its untracked `coney.local.toml`
(`build_root = "<another drive>/coney_builds"`); every checkout's build tree is then `<build_root>/<id>/` (`<id>/dev`,
`<id>/ci`, ...). Unset, nothing changes: every path is exactly the one CMakePresets.json names.

The id is stable when the worktree folder is renamed or moved, or its branch renamed: it is written once, into the
checkout's own git folder (`<git dir>/coney-build-id`, outside the working tree, so no `git status` or task check
sees it), and read from there ever after. The first id is the task id when the checkout is a task's unit worktree
(its unit record names it), else the folder's name with a short random suffix. Each build folder in the root holds a
marker, `coney-build.json`, naming the worktree it serves, so `workspace clean` tells an orphan (its worktree gone)
from a live one, and never deletes a folder it did not make (one with no marker).

`coney-tools build` (every preset and `--build-dir build/...`), and through it `task check`, `orig fails-first` and
`ci run`, `workspace clean`, `workspace check-space`, the cap on build folders, `perf` and `trace` all resolve the
build tree here; nothing else joins `"build"` to a checkout.
"""

from __future__ import annotations

import json
import os
import re
import secrets
import subprocess
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import unit, unit_record
from coney_tools.config import ConfigError, load_config, local_config_file, main_checkout

#: The build tree of a checkout when `build_root` is unset: CMakePresets.json's `${sourceDir}/build/<preset>`.
IN_CHECKOUT = "build"
#: The file in the checkout's git folder that holds its id (`.coney-build-id` in the checkout when git has none).
ID_FILE = "coney-build-id"
NO_GIT_ID_FILE = ".coney-build-id"
#: The marker in each build folder of the root: which worktree it serves.
MARKER_FILE = "coney-build.json"
#: What an id may hold: it names a folder.
ID_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,99}")


def configured(checkout: Path) -> Path | None:
    """The `build_root` of the checkout's `coney.local.toml` (a worktree reads the main checkout's), as an absolute
    path; None when it is unset or there is no local config. Raises ConfigError for a root inside the checkout or the
    main checkout: git and every tool would walk the builds there."""
    if local_config_file(checkout) is None:
        return None
    root = load_config(checkout).paths.get("build_root")
    if root is None:
        return None
    for inside in dict.fromkeys((checkout.resolve(), main_checkout(checkout).resolve())):
        if root.resolve().is_relative_to(inside):
            raise ConfigError(
                f"build_root {root} is inside the checkout {inside}; point it at a folder outside every checkout "
                "(docs/guides/workspace.md#build-root)"
            )
    return root


def _git_dir(checkout: Path) -> Path | None:
    """The checkout's own git folder (`.git/worktrees/<name>` for a worktree), or None when git has none."""
    try:
        done = subprocess.run(
            ["git", "-C", str(checkout), "rev-parse", "--absolute-git-dir"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )
    except OSError:
        return None
    text = done.stdout.strip()
    return Path(text) if done.returncode == 0 and text else None


def _id_file(checkout: Path) -> Path:
    """Where the checkout's id is kept: in its git folder, else (no git) in the checkout itself."""
    git_dir = _git_dir(checkout)
    return git_dir / ID_FILE if git_dir is not None else checkout / NO_GIT_ID_FILE


def read_id(checkout: Path) -> str | None:
    """The id written for the checkout, or None when none was written yet (or it is unreadable or not an id)."""
    try:
        text = _id_file(checkout).read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return text if ID_PATTERN.fullmatch(text) else None


def _same_path(one: Path, two: Path) -> bool:
    """Whether two paths name the same folder, however they are spelled."""
    return os.path.normcase(os.path.realpath(one)) == os.path.normcase(os.path.realpath(two))


def _unit_task(checkout: Path) -> str | None:
    """The task whose unit worktree the checkout is (its unit record names this very folder), else None."""
    try:
        task = unit.task_name(checkout)
        record = unit_record.read_record(unit.Unit.of(checkout, task).dir / "unit.json")
    except ConfigError:
        return None
    return task if _same_path(record.worktree, checkout) else None


def _random_id(checkout: Path) -> str:
    """The folder's name, cut to what an id holds, and a short random suffix."""
    name = re.sub(r"[^A-Za-z0-9._-]+", "-", checkout.resolve().name).strip("-.")[:60] or "checkout"
    return f"{name}-{secrets.token_hex(3)}"


def _serves_another(folder: Path, checkout: Path) -> bool:
    """Whether the root's `folder` serves a worktree other than `checkout` that still exists."""
    served = owner(folder)
    return served is not None and served.exists() and not _same_path(served, checkout)


def build_id(checkout: Path, root: Path) -> str:
    """The checkout's id, written once: the one already written, else its task id (when it is a unit worktree and the
    root's folder of that name serves no other worktree), else its name with a random suffix."""
    known = read_id(checkout)
    if known is not None:
        return known
    task = _unit_task(checkout)
    if task is not None and ID_PATTERN.fullmatch(task):
        ident = f"{task[:90]}-{secrets.token_hex(3)}" if _serves_another(root / task, checkout) else task
    else:
        ident = _random_id(checkout)
    target = _id_file(checkout)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(ident + "\n", encoding="utf-8")
    return ident


def owner(folder: Path) -> Path | None:
    """The worktree the build folder `folder` serves, from its marker; None when it has none (or it is unreadable)."""
    try:
        data = json.loads((folder / MARKER_FILE).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    worktree = data.get("worktree") if isinstance(data, dict) else None
    return Path(worktree) if isinstance(worktree, str) and worktree else None


def write_marker(folder: Path, checkout: Path) -> None:
    """Make `folder` and write its marker naming `checkout`, when it does not name it already."""
    folder.mkdir(parents=True, exist_ok=True)
    served = owner(folder)
    if served is not None and served == checkout.resolve():
        return
    data = {
        "v": 1,
        "worktree": str(checkout.resolve()),
        "id": folder.name,
        "written": datetime.now(UTC).isoformat(timespec="seconds"),
    }
    (folder / MARKER_FILE).write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")


def tree(checkout: Path, *, create: bool = False, plan: bool = False) -> Path:
    """The checkout's build tree: `<checkout>/build` without a build root; with one, `<build_root>/<id>`.

    `create` is for a build about to run: it writes the id (once) and makes the folder with its marker. `plan` writes
    the id and makes nothing (the cap on build folders asks whether the folder is new). Without either nothing is
    written, and a checkout with no id yet has built nothing in the root, so its tree is still `<checkout>/build`."""
    root = configured(checkout)
    if root is None:
        return checkout / IN_CHECKOUT
    if create or plan:
        folder = root / build_id(checkout, root)
        if create:
            write_marker(folder, checkout)
        return folder
    ident = read_id(checkout)
    return root / ident if ident is not None else checkout / IN_CHECKOUT


def preset_folder(checkout: Path, preset: str, *, create: bool = False, plan: bool = False) -> Path:
    """The build folder of `preset` in the checkout's build tree (`build/<preset>` by default)."""
    return tree(checkout, create=create, plan=plan) / preset


def custom_folder(checkout: Path, given: Path, *, create: bool = False, plan: bool = False) -> Path:
    """The folder `--build-dir given` names: relative to the checkout as given, except that with a build root a
    relative path in `build/` (`build/clang`) moves into the build tree with the presets' folders."""
    parts = given.parts
    if not given.is_absolute() and len(parts) > 1 and parts[0] == IN_CHECKOUT and configured(checkout) is not None:
        return tree(checkout, create=create, plan=plan).joinpath(*parts[1:])
    return (checkout / given).resolve()


def folders(checkout: Path, root: Path | None) -> list[Path]:
    """The build trees the checkout holds now, given the machine's build root: its own `build/` (a build from before
    the root was set, or with none set) and its folder in the root; only those that exist."""
    found = [checkout / IN_CHECKOUT] if (checkout / IN_CHECKOUT).is_dir() else []
    if root is not None:
        ident = read_id(checkout)
        if ident is not None and (root / ident).is_dir():
            found.append(root / ident)
    return found


def is_link(path: Path) -> bool:
    """Whether `path` is a symbolic link or a Windows junction (never followed, never deleted through)."""
    try:
        return path.is_symlink() or path.is_junction()
    except OSError:
        return False


def same_drive(one: Path, two: Path) -> bool:
    """Whether two folders (or their nearest existing parents) are on the same drive."""

    def device(path: Path) -> int | None:
        path = path.resolve()
        while not path.exists() and path.parent != path:
            path = path.parent
        try:
            return path.stat().st_dev
        except OSError:
            return None

    first, second = device(one), device(two)
    return first is not None and first == second


@dataclass(frozen=True)
class Item:
    """One folder of the build root that no live checkout explains."""

    path: Path
    #: The worktree its marker names; None for a folder without a marker.
    worktree: Path | None


@dataclass(frozen=True)
class Survey:
    """The folders of a build root that `workspace clean` names and leaves alone."""

    #: Folders whose marker names a worktree that is gone, and whose id no checkout holds.
    orphans: list[Item]
    #: Folders and links that `coney-tools build` did not make (no marker): never touched.
    unmarked: list[Item]


def survey(root: Path, live_ids: set[str]) -> Survey:
    """Sort the folders of the build root `root` that no checkout of `live_ids` holds: orphans and unmarked ones."""
    orphans: list[Item] = []
    unmarked: list[Item] = []
    try:
        children = sorted(root.iterdir())
    except OSError:
        return Survey([], [])
    for child in children:
        if child.name in live_ids:
            continue
        if is_link(child) or not child.is_dir():
            unmarked.append(Item(child, None))
            continue
        served = owner(child)
        if served is None:
            unmarked.append(Item(child, None))
        elif not served.exists():
            orphans.append(Item(child, served))
    return Survey(orphans, unmarked)
