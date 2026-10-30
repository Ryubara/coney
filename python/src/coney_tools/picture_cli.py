# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools picture ...` commands: compare pictures with a tolerance, and check Coney's synthetic scenes.

`picture compare` prints the pixels over the tolerance and the largest difference for two PNGs or two folders of them
and exits with 1 past the limit (coney_tools.picture_compare). `picture scenes` draws the scenes of
`tests/pictures/scenes.toml` with Coney and compares them with their reference pictures (coney_tools.picture_scenes),
which is what CI's `software-pictures` job runs. The rules: docs/engine/renderer.md#comparing.
"""

from __future__ import annotations

import shlex
from pathlib import Path

from coney_tools import picture_compare, picture_scenes
from coney_tools.config import ConfigError, find_repo_root


def run_compare(
    first: Path,
    second: Path,
    channel_tolerance: int | None,
    max_share_percent: float | None,
    diff_out: Path | None,
) -> int:
    """`picture compare`: print one line per picture and a total; 0 within the limit, 1 past it.

    A limit not given is the stand-in of docs/engine/renderer.md#comparing."""
    if channel_tolerance is None:
        channel_tolerance = picture_compare.DEFAULT_CHANNEL_TOLERANCE
    if max_share_percent is None:
        max_share_percent = picture_compare.DEFAULT_MAX_SHARE_PERCENT
    check_arguments(first, second)
    results = picture_compare.compare_paths(first, second, channel_tolerance, max_share_percent, diff_out)
    for result in results:
        print(result.line(channel_tolerance, max_share_percent))
    print(picture_compare.summary_line(results, channel_tolerance, max_share_percent))
    return 0 if all(result.within(max_share_percent) for result in results) else 1


def run_scenes(
    coney: str,
    launcher: str,
    scenes: Path | None,
    references: Path | None,
    out: Path,
    repeat: int,
    update: bool,
    channel_tolerance: int | None,
    max_share_percent: float | None,
) -> int:
    """`picture scenes`: draw the scenes with `launcher coney` and compare them with the references."""
    root = find_repo_root(Path.cwd())
    command = [*shlex.split(launcher), coney]
    return picture_scenes.run_scenes(
        scenes or root / picture_scenes.DEFAULT_SCENES,
        command,
        references or root / picture_scenes.DEFAULT_REFERENCES,
        out,
        repeat=repeat,
        update=update,
        channel_tolerance=channel_tolerance,
        max_share_percent=max_share_percent,
    )


def check_arguments(first: Path, second: Path) -> None:
    """Refuse paths that do not exist, naming the first that does not (ConfigError, exit 2)."""
    for path in (first, second):
        if not path.exists():
            raise ConfigError(f"{path}: no such picture or folder")
