# SPDX-License-Identifier: GPL-3.0-or-later
"""The GPU renderer's parity suite (`coney-tools picture parity`): every benchmark scene and the named `--play-level`
frames drawn by both renderers, the whole frame, and compared with the cross-renderer tolerance.

The synthetic scenes and the test cards of `tests/pictures/scenes.toml` need no game data and run in CI's software
pictures (`picture scenes`). This suite is their disc-backed half and runs on a machine with the disc and a GPU: the
frames are the ones the renderer's design names (docs/engine/renderer-pictures.md#parity), drawn once with `--renderer
gl3` and once with `--renderer gpu`, each saved with `--screenshot`, and the GPU renderer's picture is compared with
GL3's, its reference until the switch. Whole frames are compared, the effects and screen passes included: `--scene-only`
is no longer what the comparison needs, since both renderers draw them.

A frame is one `coney` command line: a benchmark scene is `perf bench`'s own command (`perf_cli.scene_bench_command`,
so a scene is the same work in both tools) stopped at `BENCH_FRAME` instead of measured, the level frames are the
commands of the design's table, and the street again with the line blend on puts a screen pass in a level frame. The
report is a Markdown table, one row each, ready for a pull request; a frame whose two pictures are both one colour (the
hub's, black in every build) is named, since two blank pictures match and prove nothing.

Research: docs/engine/renderer.md#comparing
"""

from __future__ import annotations

import subprocess
from collections.abc import Sequence
from dataclasses import dataclass, replace
from pathlib import Path

import numpy

from coney_tools import perf_cli, picture_compare, picture_scenes
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.perf_scenes import SCENES

#: The renderers a frame is drawn with, GL3 first: its picture is the one the GPU renderer's is compared with.
RENDERERS = ("gl3", "gpu")
#: The frame a benchmark scene is taken at: after the crowd spawns and the 60 warm-up frames, as the earlier
#: comparisons took the crowd (docs/engine/renderer-pictures.md#comparing).
BENCH_FRAME = 300
#: How long one run of Coney may take, in seconds: a level loads from the disc and plays 300 frames in a window.
RUN_TIMEOUT_SECONDS = 600
#: What the level frames play: `--freeze-world` from the level's first frame of play at the original's frame size.
_LEVEL_RENDER_SIZE = "640x448"
_LEVEL_FRAMES = 90

Runner = picture_scenes.Runner


@dataclass(frozen=True)
class ParityFrame:
    """One frame of the suite: a name (the pictures' file names and `--only`), what it is, and Coney's arguments
    (the disc first, then everything but `--screenshot` and `--renderer`, which the run adds)."""

    name: str
    title: str
    args: tuple[str, ...]


def frames(disc: str) -> list[ParityFrame]:
    """The suite's frames, for a disc: each benchmark scene that draws, in the table's order, then the named level
    frames (docs/engine/renderer-pictures.md#disc-frames)."""
    found: list[ParityFrame] = []
    for scene in SCENES:
        if scene.headless:
            continue
        # The scene's own command, stopped at BENCH_FRAME: `--bench`'s frame count is the measured frames after the
        # warm-up, so the picture is of the frame the measured run would have started at the end of its warm-up.
        stopped = replace(scene, frames=BENCH_FRAME - scene.warmup)
        command = perf_cli.scene_bench_command(Path("coney"), disc, stopped, Path("bench.json"))
        found.append(ParityFrame(scene.name, scene.title, tuple(command[1:])))
    found.append(_level_frame(disc, "level99-street", "Street, level99", "level99", skip_movies=True))
    # The same street with the line blend on, the one screen pass a level's frame can show without a mission.
    street = found[-1]
    found.append(ParityFrame("level99-line-blend", "Street with the line blend", (*street.args, "--line-blend", "on")))
    found.append(_level_frame(disc, "level95-hub", "Hub, level95", "level95", skip_movies=False))
    return found


def _level_frame(disc: str, name: str, title: str, level: str, *, skip_movies: bool) -> ParityFrame:
    """A named `--play-level` frame: the level's first frames of play with the world frozen, at 640x448."""
    args = ["--disc", disc, "--play-level", level, "--frames", str(_LEVEL_FRAMES), "--freeze-world"]
    args += ["--render-size", _LEVEL_RENDER_SIZE, "--no-activate"]
    args += ["--skip-movies"] if skip_movies else []
    return ParityFrame(name, title, tuple(args))


def frame_command(frame: ParityFrame, coney: Sequence[str], target: Path, renderer: str) -> list[str]:
    """The command line that draws a frame with one renderer into `target`. A benchmark frame's report goes beside the
    picture (`--bench` writes one), so it never lands in the folder Coney runs in."""
    args = list(frame.args)
    if "--bench" in args:
        args[args.index("--bench") + 1] = str(target.with_suffix(".bench.json"))
    return [*coney, *args, "--screenshot", str(target), "--renderer", renderer]


@dataclass(frozen=True)
class ParityRow:
    """One frame's comparison, and whether the two pictures were one colour."""

    frame: ParityFrame
    result: picture_compare.PictureResult
    blank: bool = False

    def row(self, max_share_percent: float) -> str:
        """The report's Markdown row: name, size, pixels over, share, largest difference and the verdict."""
        result = self.result
        if result.problem:
            return f"| {self.frame.name} | none | none | none | none | {result.problem} |"
        verdict = "within" if result.within(max_share_percent) else "past the limit"
        verdict += ", blank" if self.blank else ""
        return (
            f"| {self.frame.name} | {result.width}x{result.height} | {result.over} | {result.share_percent:.3f} % | "
            f"{result.largest} | {verdict} |"
        )


def _blank(first: numpy.ndarray, second: numpy.ndarray) -> bool:
    """Whether both pictures are one colour all over (nothing was drawn, or all of it black)."""
    return all(bool((picture == picture[0, 0]).all()) for picture in (first, second) if picture.size)


def run_parity(
    coney: Sequence[str],
    disc: str,
    out: Path,
    *,
    only: Sequence[str] = (),
    runner: Runner | None = None,
    cwd: Path | None = None,
    channel_tolerance: int | None = None,
    max_share_percent: float | None = None,
    scenes_path: Path | None = None,
    timeout: float = RUN_TIMEOUT_SECONDS,
    diff_out: Path | None = None,
) -> int:
    """Draw every frame with both renderers, compare the GPU renderer's picture with GL3's and print the report; 0 when
    every frame is within the limit, 1 when one is past it.

    `coney` is the command that starts Coney, a launcher before it if any. The pictures, each run's output
    (NAME.RENDERER.log) and a benchmark frame's report go to `out`, outside the repository. The limits are the
    cross-renderer ones of the scene list (`scenes_path`, default `tests/pictures/scenes.toml` of `cwd`), unless given.
    `only` draws just the frames it names (all when empty). `diff_out` gets a difference picture of each frame with a
    pixel over the tolerance. A run that fails, hangs or draws nothing, and a GPU run that fell back to GL3, are a
    ConfigError (exit 2): a comparison never passes by comparing GL3 with itself."""
    chosen = frames(disc)
    unknown = sorted(set(only) - {frame.name for frame in chosen})
    if unknown:
        raise ConfigError(f"no frame named {', '.join(unknown)}; the frames are: {', '.join(f.name for f in chosen)}")
    if timeout <= 0:
        raise ConfigError("--timeout must be more than 0 seconds")
    scene_list = picture_scenes.load_scenes(
        scenes_path or find_repo_root(cwd or Path.cwd()) / picture_scenes.DEFAULT_SCENES
    )
    tolerance = scene_list.cross_channel_tolerance if channel_tolerance is None else channel_tolerance
    share = scene_list.cross_max_share_percent if max_share_percent is None else max_share_percent
    picture_compare.check_limits(tolerance, share)
    run = runner if runner is not None else subprocess.run
    # Coney may run elsewhere than the caller, so the pictures' paths must not depend on it.
    out = out.resolve()
    rows: list[ParityRow] = []
    for frame in (item for item in chosen if not only or item.name in only):
        pictures: dict[str, Path] = {}
        for renderer in RENDERERS:
            target = out / f"{frame.name}.{renderer}.png"
            print(f"parity: {frame.name} with {renderer} ...", flush=True)
            picture_scenes.draw_picture(
                frame.name, frame_command(frame, coney, target, renderer), target, run, cwd, timeout, renderer
            )
            pictures[renderer] = target
        reference = picture_compare.load_picture(pictures["gl3"])
        drawn = picture_compare.load_picture(pictures["gpu"])
        result = picture_compare.compare_arrays(frame.name, reference, drawn, tolerance)
        rows.append(ParityRow(frame, result, _blank(reference, drawn) and not result.problem))
        if diff_out is not None and not result.problem and result.over:
            diff_out.mkdir(parents=True, exist_ok=True)
            picture_compare.difference_picture(reference, drawn, tolerance).save(diff_out / f"{frame.name}.diff.png")
    print(report(rows, tolerance, share))
    return 0 if all(row.result.within(share) for row in rows) else 1


def report(rows: Sequence[ParityRow], tolerance: int, max_share_percent: float) -> str:
    """The Markdown report: one row per frame (the counts of the GPU renderer's picture against GL3's), then a
    summary line. The columns are what a pull request prints (docs/guides/testing.md#picture-counts)."""
    header = f"| Frame | Size | Pixels over {tolerance} | Share | Largest difference | Against GL3 |"
    lines = [header, "|" + " --- |" * 6]
    lines += [row.row(max_share_percent) for row in rows]
    failed = sum(1 for row in rows if not row.result.within(max_share_percent))
    blank = sum(1 for row in rows if row.blank)
    noun = "frame" if len(rows) == 1 else "frames"
    verdict = "ok" if not failed else f"{failed} of {len(rows)} {noun} past the limit"
    summary = (
        f"{len(rows)} {noun}, GPU renderer against GL3: limit {max_share_percent:g} % of the pixels over {tolerance}"
    )
    summary += f", {blank} blank" if blank else ""
    return "\n".join(lines) + f"\n\n{summary}: {verdict}"
