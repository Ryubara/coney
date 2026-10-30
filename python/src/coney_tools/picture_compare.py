# SPDX-License-Identifier: GPL-3.0-or-later
"""Picture comparison with a per-channel tolerance and a share of pixels allowed over it (`picture compare`).

A GPU's output differs between GPUs and drivers, and CI has no GPU, so pictures are compared with a tolerance
instead of a hash: a pixel is *over* the tolerance when any of its four channels differs by more than
`channel_tolerance` (of 255), and a picture passes when the share of pixels over is at most `max_share_percent`.
The counts reported are the pixels over the tolerance and the largest difference in any channel, which is what a
pull request that changes drawing prints in its Testing section (docs/guides/testing.md#picture-counts).

Two PNG files are compared, or two folders of them, paired by their path inside the folder; a picture in one folder
only, or two of different sizes, fails. The rule and the stand-in limits come from
docs/engine/renderer.md#comparing.

Research: docs/engine/renderer.md#comparing
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy
from PIL import Image, UnidentifiedImageError

from coney_tools.config import ConfigError

#: Stand-in tolerance until repeated captures set it (docs/engine/renderer.md#comparing): 2 of 255 in any channel.
DEFAULT_CHANNEL_TOLERANCE = 2
#: Stand-in share of the pixels allowed over the tolerance, in percent (0.1 %).
DEFAULT_MAX_SHARE_PERCENT = 0.1
#: The colour of a pixel over the tolerance in a difference picture.
_MARK = (255, 0, 255, 255)


@dataclass(frozen=True)
class PictureResult:
    """The comparison of one pair of pictures (or the fact that a pair is not there or cannot be compared)."""

    #: The picture's name: its path inside the folder, with `/`, or the first file's name for two files.
    name: str
    #: The pictures' width and height, (0, 0) when they were not compared.
    width: int = 0
    height: int = 0
    #: The pixels with a channel past the tolerance, and the largest difference in any channel.
    over: int = 0
    largest: int = 0
    #: Why the pair could not be compared ("missing from the second folder", "size differs: 10x10 and 10x11"), or "".
    problem: str = ""

    @property
    def pixels(self) -> int:
        """The number of pixels compared."""
        return self.width * self.height

    @property
    def share_percent(self) -> float:
        """The pixels over the tolerance as a share of all pixels, in percent (0 when none were compared)."""
        return 100.0 * self.over / self.pixels if self.pixels else 0.0

    def within(self, max_share_percent: float) -> bool:
        """Whether the pair passes: compared, and no more than `max_share_percent` of the pixels over."""
        # Compare counts, not the rounded share: 1 pixel of 1000 is exactly 0.1 %.
        return not self.problem and self.over * 100 <= max_share_percent * self.pixels

    def line(self, tolerance: int, max_share_percent: float) -> str:
        """The result as one line: the counts, then `ok` or `FAILED`."""
        if self.problem:
            return f"{self.name}: {self.problem}: FAILED"
        verdict = "ok" if self.within(max_share_percent) else "FAILED"
        return (
            f"{self.name}: {self.over} of {self.pixels} pixels over {tolerance} ({self.share_percent:.3f} %), "
            f"largest difference {self.largest}: {verdict}"
        )


def check_limits(channel_tolerance: int, max_share_percent: float) -> None:
    """Refuse a tolerance outside 0 to 255 or a share outside 0 to 100 percent, as a ConfigError."""
    if not 0 <= channel_tolerance <= 255:
        raise ConfigError(f"the channel tolerance is {channel_tolerance}: expected 0 to 255")
    if not 0 <= max_share_percent <= 100:
        raise ConfigError(f"the share of pixels allowed over is {max_share_percent}: expected 0 to 100 percent")


def load_picture(path: Path) -> numpy.ndarray:
    """The picture at `path` as a height x width x 4 array of unsigned bytes (RGBA); ConfigError when unreadable."""
    try:
        with Image.open(path) as picture:
            return numpy.asarray(picture.convert("RGBA"), dtype=numpy.uint8)
    except FileNotFoundError as error:
        raise ConfigError(f"{path}: no such picture") from error
    except (OSError, UnidentifiedImageError, ValueError) as error:
        raise ConfigError(f"{path}: cannot read the picture: {error}") from error


def _differences(first: numpy.ndarray, second: numpy.ndarray) -> numpy.ndarray:
    """The absolute difference of every channel of every pixel (the arrays have the same shape)."""
    return numpy.abs(first.astype(numpy.int16) - second.astype(numpy.int16))


def compare_arrays(name: str, first: numpy.ndarray, second: numpy.ndarray, channel_tolerance: int) -> PictureResult:
    """Compare two RGBA arrays; a size difference is a result with a `problem`, not an error."""
    if first.shape != second.shape:
        sizes = f"{first.shape[1]}x{first.shape[0]} and {second.shape[1]}x{second.shape[0]}"
        return PictureResult(name, problem=f"size differs: {sizes}")
    difference = _differences(first, second)
    height, width = first.shape[:2]
    over = int(numpy.count_nonzero(difference.max(axis=2) > channel_tolerance)) if difference.size else 0
    largest = int(difference.max()) if difference.size else 0
    return PictureResult(name, width, height, over, largest)


def compare_pictures(first: Path, second: Path, channel_tolerance: int = DEFAULT_CHANNEL_TOLERANCE) -> PictureResult:
    """Compare the pictures at two paths; the result is named after the first."""
    return compare_arrays(first.name, load_picture(first), load_picture(second), channel_tolerance)


def difference_picture(first: numpy.ndarray, second: numpy.ndarray, channel_tolerance: int) -> Image.Image:
    """A picture for a reader: the first picture dimmed to grey, with every pixel over the tolerance in magenta."""
    height, width = first.shape[:2]
    grey = first[:, :, :3].mean(axis=2) * 0.4 + 40
    out = numpy.empty((height, width, 4), dtype=numpy.uint8)
    out[:, :, :3] = grey.astype(numpy.uint8)[:, :, None]
    out[:, :, 3] = 255
    over = _differences(first, second).max(axis=2) > channel_tolerance
    out[over] = _MARK
    return Image.fromarray(out, "RGBA")


def _pictures_in(folder: Path) -> dict[str, Path]:
    """Every `.png` under `folder`, by its path inside it written with `/`."""
    return {path.relative_to(folder).as_posix(): path for path in sorted(folder.rglob("*.png")) if path.is_file()}


def compare_paths(
    first: Path,
    second: Path,
    channel_tolerance: int = DEFAULT_CHANNEL_TOLERANCE,
    max_share_percent: float = DEFAULT_MAX_SHARE_PERCENT,
    diff_out: Path | None = None,
) -> list[PictureResult]:
    """Compare two pictures, or two folders of them paired by name; one result each, in name order.

    With `diff_out`, a difference picture (`NAME.diff.png`, in the same sub-folder) is written for every pair that
    was compared and has a pixel over the tolerance. ConfigError when a path is unreadable or one is a file and the
    other a folder."""
    check_limits(channel_tolerance, max_share_percent)
    if first.is_dir() != second.is_dir():
        raise ConfigError(f"{first} and {second} must both be pictures or both be folders")
    if not first.is_dir():
        pairs = [(first.name, first, second)]
        missing: list[PictureResult] = []
    else:
        left, right = _pictures_in(first), _pictures_in(second)
        pairs = [(name, left[name], right[name]) for name in sorted(left.keys() & right.keys())]
        missing = [PictureResult(name, problem="missing from the second folder") for name in left.keys() - right.keys()]
        missing += [PictureResult(name, problem="missing from the first folder") for name in right.keys() - left.keys()]
    results = [_compare_pair(name, one, two, channel_tolerance, diff_out) for name, one, two in pairs]
    results += missing
    if not results:
        raise ConfigError(f"{first} and {second} hold no pictures (.png) to compare")
    return sorted(results, key=lambda result: result.name)


def _compare_pair(name: str, first: Path, second: Path, channel_tolerance: int, diff_out: Path | None) -> PictureResult:
    """Compare one pair and write its difference picture when asked and there is a difference to show."""
    one, two = load_picture(first), load_picture(second)
    result = compare_arrays(name, one, two, channel_tolerance)
    if diff_out is not None and not result.problem and result.over:
        target = diff_out / Path(name).with_suffix(".diff.png")
        target.parent.mkdir(parents=True, exist_ok=True)
        difference_picture(one, two, channel_tolerance).save(target)
    return result


def summary_line(results: list[PictureResult], channel_tolerance: int, max_share_percent: float) -> str:
    """The totals: how many pictures, the pixels over and the largest difference, and the verdict."""
    over = sum(result.over for result in results)
    largest = max((result.largest for result in results), default=0)
    failed = sum(1 for result in results if not result.within(max_share_percent))
    noun = "picture" if len(results) == 1 else "pictures"
    verdict = "ok" if not failed else f"FAILED ({failed} of {len(results)} past the limit)"
    return (
        f"{len(results)} {noun}: {over} pixels over {channel_tolerance}, largest difference {largest}; "
        f"limit {max_share_percent:g} % of the pixels of each picture: {verdict}"
    )
