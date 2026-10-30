# SPDX-License-Identifier: GPL-3.0-or-later
"""The synthetic scenes Coney draws for the picture comparison (`coney-tools picture scenes`).

A scene list (`tests/pictures/scenes.toml`) names scenes that need no game data: each is a command line of `coney`
(a sandbox layout, which is Coney's own world of grid textures) at a fixed number of frames and render size. This
module draws every scene to a PNG with `--screenshot`, compares it with the reference picture of the same name and
prints the counts of coney_tools.picture_compare. CI runs it on Linux in a container image pinned by digest, with
Mesa's llvmpipe as the OpenGL driver under a virtual display (`--launcher` puts `xvfb-run` before Coney); locally the
same command compares against the references or, with `--update`, replaces them. `--repeat` draws each scene again
and compares the repeats with the first, which is how the tolerance is measured from repeated captures of one build.

The scene list's format and the whole check: docs/guides/coney-tools-trace.md#picture.

Research: docs/engine/renderer.md#comparing
"""

from __future__ import annotations

import re
import shutil
import subprocess
import tomllib
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools import picture_compare
from coney_tools.config import ConfigError

#: The scene list and the reference pictures, relative to the repository root.
DEFAULT_SCENES = "tests/pictures/scenes.toml"
DEFAULT_REFERENCES = "tests/pictures/reference"
#: What a scene draws with when the list does not say: three frames (the world settles) at 320x224 (half of the
#: original's 640x448), small enough to keep the references small.
DEFAULT_FRAMES = 3
DEFAULT_RENDER_SIZE = "320x224"
#: How long one run of Coney may take, in seconds, before it counts as hung.
RUN_TIMEOUT_SECONDS = 180

_NAME = re.compile(r"[a-z0-9][a-z0-9_-]*")
_SIZE = re.compile(r"[1-9][0-9]*x[1-9][0-9]*")
#: Options the list sets itself, so a scene's `args` may not.
_OWN_OPTIONS = ("--screenshot", "--frames", "--render-size")

Runner = Callable[..., "subprocess.CompletedProcess[str]"]


@dataclass(frozen=True)
class Scene:
    """One scene: a name (the picture's file name), Coney's arguments, the frames to run and the picture's size."""

    name: str
    args: tuple[str, ...]
    frames: int
    render_size: str


@dataclass(frozen=True)
class SceneList:
    """The scenes of a list and the tolerance they are compared with."""

    scenes: tuple[Scene, ...]
    channel_tolerance: int
    max_share_percent: float


def load_scenes(path: Path) -> SceneList:
    """Read and check a scene list; ConfigError names the file and the problem."""
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ConfigError(f"cannot read {path}: {error.strerror or error}") from error
    except tomllib.TOMLDecodeError as error:
        raise ConfigError(f"{path}: {error}") from error
    tolerance = data.get("channel_tolerance", picture_compare.DEFAULT_CHANNEL_TOLERANCE)
    share = float(data.get("max_share_percent", picture_compare.DEFAULT_MAX_SHARE_PERCENT))
    if not isinstance(tolerance, int) or isinstance(tolerance, bool):
        raise ConfigError(f"{path}: channel_tolerance must be a whole number")
    picture_compare.check_limits(tolerance, share)
    default_frames = data.get("frames", DEFAULT_FRAMES)
    default_size = data.get("render_size", DEFAULT_RENDER_SIZE)
    scenes: list[Scene] = []
    for index, entry in enumerate(data.get("scene", []), start=1):
        scenes.append(_scene(path, index, entry, default_frames, default_size))
    names = [scene.name for scene in scenes]
    for name in sorted(set(names)):
        if names.count(name) > 1:
            raise ConfigError(f"{path}: scene {name} is listed twice")
    if not scenes:
        raise ConfigError(f"{path}: no scenes ([[scene]] tables)")
    return SceneList(tuple(scenes), tolerance, share)


def _scene(path: Path, index: int, entry: object, default_frames: object, default_size: object) -> Scene:
    """One `[[scene]]` table, checked."""
    where = f"{path}: scene {index}"
    if not isinstance(entry, dict):
        raise ConfigError(f"{where} is not a table")
    name = entry.get("name")
    if not isinstance(name, str) or not _NAME.fullmatch(name):
        raise ConfigError(f"{where}: name {name!r} must be lowercase letters, digits, '-' and '_'")
    args = entry.get("args")
    if not isinstance(args, list) or not all(isinstance(item, str) for item in args):
        raise ConfigError(f"{where} ({name}): args must be a list of strings")
    for option in _OWN_OPTIONS:
        if option in args:
            raise ConfigError(f"{where} ({name}): args must not hold {option}; the list sets it")
    frames = entry.get("frames", default_frames)
    if not isinstance(frames, int) or isinstance(frames, bool) or frames < 1:
        raise ConfigError(f"{where} ({name}): frames must be a whole number of at least 1")
    size = entry.get("render_size", default_size)
    if not isinstance(size, str) or not _SIZE.fullmatch(size):
        raise ConfigError(f"{where} ({name}): render_size must look like 320x224")
    return Scene(name, tuple(args), frames, size)


def _draw(scene: Scene, coney: Sequence[str], target: Path, runner: Runner) -> None:
    """Run Coney on one scene so that it writes `target`; ConfigError when it fails or draws nothing."""
    target.parent.mkdir(parents=True, exist_ok=True)
    target.unlink(missing_ok=True)
    command = [
        *coney,
        *scene.args,
        "--frames",
        str(scene.frames),
        "--render-size",
        scene.render_size,
        "--screenshot",
        str(target),
    ]
    try:
        done = runner(command, capture_output=True, text=True, timeout=RUN_TIMEOUT_SECONDS, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ConfigError(f"scene {scene.name}: cannot run {coney[-1]}: {error}") from error
    if done.returncode != 0:
        tail = (done.stderr or done.stdout or "").strip().splitlines()[-5:]
        raise ConfigError(
            f"scene {scene.name}: coney ended with exit code {done.returncode}"
            + "".join(f"\n    {line}" for line in tail)
        )
    if not target.is_file():
        raise ConfigError(f"scene {scene.name}: coney ended well but wrote no picture at {target}")


def run_scenes(
    scenes_path: Path,
    coney: Sequence[str],
    references: Path,
    out: Path,
    runner: Runner | None = None,
    repeat: int = 1,
    update: bool = False,
    channel_tolerance: int | None = None,
    max_share_percent: float | None = None,
) -> int:
    """Draw every scene, compare it with its reference and print the counts; 0 when all pass, 1 when one does not.

    `coney` is the command that starts Coney, with any launcher before it. The drawn pictures are left in `out`.
    With `repeat` above 1 each scene is drawn that many times and every repeat is compared with the first draw. With
    `update` the references are replaced by what was drawn, after the counts against the old ones are printed (the
    pull request that moves the Mesa pin reports them), and a missing reference is made."""
    scene_list = load_scenes(scenes_path)
    tolerance = scene_list.channel_tolerance if channel_tolerance is None else channel_tolerance
    share = scene_list.max_share_percent if max_share_percent is None else max_share_percent
    picture_compare.check_limits(tolerance, share)
    if repeat < 1:
        raise ConfigError("--repeat must be at least 1")
    run = runner if runner is not None else subprocess.run
    results: list[picture_compare.PictureResult] = []
    repeats: list[picture_compare.PictureResult] = []
    updated = 0
    for scene in scene_list.scenes:
        drawn = out / f"{scene.name}.png"
        _draw(scene, coney, drawn, run)
        reference = references / f"{scene.name}.png"
        if reference.is_file():
            result = picture_compare.compare_arrays(
                drawn.name, picture_compare.load_picture(drawn), picture_compare.load_picture(reference), tolerance
            )
            results.append(result)
            print(result.line(tolerance, share))
        elif update:
            print(f"{drawn.name}: new reference")
        else:
            results.append(picture_compare.PictureResult(drawn.name, problem="no reference"))
            print(results[-1].line(tolerance, share))
        for again in range(2, repeat + 1):
            repeated = out / "repeats" / str(again) / f"{scene.name}.png"
            _draw(scene, coney, repeated, run)
            same = picture_compare.compare_arrays(
                f"repeat {again} of {drawn.name}",
                picture_compare.load_picture(drawn),
                picture_compare.load_picture(repeated),
                tolerance,
            )
            results.append(same)
            repeats.append(same)
            print(same.line(tolerance, share))
        if update:
            references.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(drawn, reference)
            updated += 1
    print(picture_compare.summary_line(results, tolerance, share))
    if update:
        # The counts against the old references are a report, not a check; only the repeats of one build can fail.
        print(f"updated {updated} references in {references}")
        return 0 if all(result.within(share) for result in repeats) else 1
    return 0 if all(result.within(share) for result in results) else 1
