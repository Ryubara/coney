# SPDX-License-Identifier: GPL-3.0-or-later
"""The drives that hold a Coney folder, each with its free space and limits: the one list that Conclave's band, the
slot-fill check, `workspace clean`, `workspace check-space` and `repo audit --local` all read
(docs/guides/workspace.md#watched-drives).

A build is large and every worktree has its own, so a drive that fills stops the work, and it need not be the one that
holds the workspace: `build_root` puts the builds on another, and a contributor keeps scratch or emulators where it
suits them. The watched drives are, in this order, the drive of the workspace, the drive of `build_root` (when set) and
the drive of each folder in `watch_paths` of `coney.local.toml`; two folders on one drive make one entry. Every drive is
judged by `workspace.disk_min_free_gb` and `workspace.disk_margin_gb` of the conventions file, or by the limits a
`watch_paths` item gives its own drive. The disk is read through a `Probe`, so the tests stand in for it.
"""

from __future__ import annotations

import os
import shutil
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path

from coney_tools import build_root
from coney_tools.config import ConfigError, WatchPath, load_config, local_config_file, workspace_root
from coney_tools.conventions import Workspace

_GIB = 1024**3
#: The limits when the conventions file cannot be read (`workspace clean` still runs then): the values it ships with.
DEFAULT_MIN_GB = 30
DEFAULT_MARGIN_GB = 10


def _existing(folder: Path) -> Path:
    """The folder, or its nearest parent that exists (a build root not made yet is on the drive of its parent)."""
    here = folder.resolve()
    while not here.exists() and here.parent != here:
        here = here.parent
    return here


def _free(folder: Path) -> int:
    """The free bytes of the drive that holds `folder`; OSError when it cannot be read."""
    return shutil.disk_usage(_existing(folder)).free


def _same(one: Path, two: Path) -> bool:
    """Whether two folders are on the same drive."""
    return build_root.same_drive(one, two)


def _label(folder: Path) -> str:
    """The drive's name: `E:` (or the share) on Windows, the mount point elsewhere."""
    here = _existing(folder)
    if here.drive:
        return here.drive
    while not os.path.ismount(here) and here.parent != here:
        here = here.parent
    return str(here)


def gb_text(gigabytes: float) -> str:
    """GB for a sentence, to one decimal and without a trailing zero: `3.7`, `12`, `29.9`."""
    return f"{round(gigabytes, 1):g}"


@dataclass(frozen=True)
class Probe:
    """How the disk is read: a folder's free bytes, whether two folders share a drive and a drive's name. The defaults
    ask the operating system; the tests give made-up ones."""

    free: Callable[[Path], int] = _free
    same: Callable[[Path, Path], bool] = _same
    label: Callable[[Path], str] = _label


@dataclass(frozen=True)
class Drive:
    """One watched drive: its name, what Coney keeps on it, its free space and the limits it is judged by."""

    #: `C:`, `E:` or a mount point.
    name: str
    #: What Coney keeps there, as it is told in a report: `the workspace drive`, `the build root (<path>)` and
    #: `the watched folder (<path>)`.
    holds: tuple[str, ...]
    free_bytes: int
    #: Under this many GB the drive is low (red; the slot-fill check holds back new builds; `workspace clean` deletes).
    min_free_gb: float
    #: ... and under the floor plus this many GB it is getting low (amber).
    margin_gb: float
    #: The folders behind `holds`, in the same order (empty where a caller made the drive up).
    folders: tuple[Path, ...] = ()

    @property
    def free_gb(self) -> float:
        """The free space in GB."""
        return self.free_bytes / _GIB

    @property
    def warn_free_gb(self) -> float:
        """The free space in GB under which the drive is amber."""
        return self.min_free_gb + self.margin_gb

    @property
    def low(self) -> bool:
        """Whether the drive is under its floor."""
        return self.free_bytes < self.min_free_gb * _GIB

    @property
    def warn(self) -> bool:
        """Whether the drive is under its floor plus its margin."""
        return self.free_bytes < self.warn_free_gb * _GIB

    @property
    def room_gb(self) -> float:
        """The GB above its own floor (negative when low): what `worst` compares."""
        return self.free_gb - self.min_free_gb

    def phrase(self) -> str:
        """What the drive holds, in a sentence: `the build root (E:/coney_builds)`, `the workspace drive, also ...`."""
        return self.holds[0] + "".join(f", also {more}" for more in self.holds[1:])

    def row(self) -> dict[str, object]:
        """The drive as the snapshot's `machine.drives` lists it."""
        return {
            "name": self.name,
            "holds": list(self.holds),
            "free_gb": round(self.free_gb, 1),
            "min_free_gb": self.min_free_gb,
            "warn_free_gb": self.warn_free_gb,
        }


@dataclass
class Watch:
    """The watched drives, and what could not be read of the optional ones (an extra drive that is not there)."""

    drives: list[Drive]
    problems: list[str] = field(default_factory=list)


def with_limits(found: Watch, min_free_gb: float | None, margin_gb: float | None) -> Watch:
    """The watch with a floor and a margin given on a command line (`--min-gb`, `--margin-gb`) applied to every drive;
    a limit left None keeps each drive's own."""
    return Watch(
        [
            replace(
                drive,
                min_free_gb=drive.min_free_gb if min_free_gb is None else min_free_gb,
                margin_gb=drive.margin_gb if margin_gb is None else margin_gb,
            )
            for drive in found.drives
        ],
        found.problems,
    )


def worst(drives: Sequence[Drive]) -> Drive:
    """The drive with the least room above its own floor (the first of a tie), the one a single figure shows."""
    return min(drives, key=lambda drive: drive.room_gb)


@dataclass
class _Group:
    """The folders found on one drive, while the list is built."""

    folders: list[tuple[str, Path]]
    limits: list[WatchPath]


def _extras(checkout: Path) -> tuple[WatchPath, ...]:
    """The `watch_paths` of the local file; none without one (a clone that has not set up its machine)."""
    return load_config(checkout).watch if local_config_file(checkout) is not None else ()


def _group(probe: Probe, checkout: Path, workspace_folder: Path | None) -> list[_Group]:
    """The folders Coney keeps, by drive, in the order workspace, build root, `watch_paths`."""
    groups: list[_Group] = []

    def add(what: str, folder: Path, limits: WatchPath | None = None) -> None:
        for group in groups:
            if probe.same(group.folders[0][1], folder):
                break
        else:
            group = _Group([], [])
            groups.append(group)
        group.folders.append((what, folder))
        if limits is not None and (limits.min_free_gb is not None or limits.margin_gb is not None):
            group.limits.append(limits)

    add("the workspace drive", workspace_folder or workspace_root(checkout))
    root = build_root.configured(checkout)
    if root is not None:
        add(f"the build root ({root})", root)
    for extra in _extras(checkout):
        add(f"the watched folder ({extra.path})", extra.path, extra)
    return groups


def read(
    checkout: Path, rules: Workspace | None, probe: Probe | None = None, *, workspace_folder: Path | None = None
) -> Watch:
    """The watched drives of the checkout, each with its free space now, judged by the `workspace` section of the
    conventions (`rules`; None: the built-in limits) unless a `watch_paths` item gives its drive its own.
    `workspace_folder` is the workspace when a command was told another than the checkout's (`--workspace`).

    Raises ConfigError for a local file that cannot be read, a build root inside a checkout or two `watch_paths` items
    that give one drive limits, and OSError when the workspace's drive cannot be read; an optional drive that cannot
    (an external disk that is not plugged in) is left out and named in `problems`."""
    probe = probe or Probe()
    default_min = rules.disk_min_free_gb if rules is not None else DEFAULT_MIN_GB
    default_margin = rules.disk_margin_gb if rules is not None else DEFAULT_MARGIN_GB
    found = Watch([])
    for number, group in enumerate(_group(probe, checkout, workspace_folder)):
        first = group.folders[0][1]
        name = probe.label(first)
        if len(group.limits) > 1:
            raise ConfigError(
                f"two entries of watch_paths give the drive {name} limits "
                f"({', '.join(str(item.path) for item in group.limits)}); give them on one"
            )
        try:
            free = probe.free(first)
        except OSError as error:
            if number == 0:
                raise
            found.problems.append(f"{name} ({group.folders[0][0]}) could not be read: {error}")
            continue
        given = group.limits[0] if group.limits else None
        found.drives.append(
            Drive(
                name,
                tuple(what for what, _folder in group.folders),
                free,
                default_min if given is None or given.min_free_gb is None else given.min_free_gb,
                default_margin if given is None or given.margin_gb is None else given.margin_gb,
                tuple(folder for _what, folder in group.folders),
            )
        )
    return found
