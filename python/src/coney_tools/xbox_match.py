# SPDX-License-Identifier: GPL-3.0-or-later
"""Which Xbox texture is which PS2 texture, and when the Xbox one replaces it.

Xbox textures carry no names, so a PS2 texture is matched by content: the Xbox candidate is shrunk to the PS2
texture's size with a box filter and compared with it. The score is the mean difference of the colour channels,
weighted by both textures' alpha, plus the mean difference of alpha, on the 0-255 scale. Correct pairs score
about 5-15 (the PS2 palette against the Xbox DXT blocks and the extra detail); unrelated ones 25 and more.

A low score alone is not enough: shrunk far enough, any texture of the same overall colour scores low against a
small or blurry PS2 one. So the pair must also have the same structure: the correlation of their alpha-weighted
luminance at the PS2 size must reach STRUCTURE_LIMIT, and STRUCTURE_LIMIT_FAR when the Xbox texture is more than
twice as wide. Right pairs correlate at 0.9 and more; same-coloured wrong ones near 0. A flat texture (no structure
to compare) matches on the score alone, but is never replaced by one more than twice as wide.

The candidates for a texture are:

- for a texture dictionary resource (chunk `0x2a`): the Xbox textures of the resource with the same resource hash,
  first the one at the same position when both hold the same number;
- for the streamed world: the textures of the level's `.xlev` and of its `.xsec` sectors (the PS2 splits a level's
  world into `s` and `d` halves and many more files, the Xbox into one level file and its sectors).

The rule: the Xbox texture replaces the PS2 one when it matches (score and structure), has the same aspect ratio and
is larger. An Xbox texture of the same size is the PS2 image re-encoded from a palette to DXT blocks, which adds
nothing, so the PS2 one stays.

Research: docs/research/xbox-assets.md#textures
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt
from PIL import Image

#: Scores at or above this are not the same picture.
MATCH_LIMIT = 20.0
#: The luminance correlation a match needs.
STRUCTURE_LIMIT = 0.5
#: The luminance correlation needed when the Xbox texture is more than twice as wide as the PS2 one.
STRUCTURE_LIMIT_FAR = 0.9
_FLAT = 1.0  # a luminance standard deviation (0-255 scale) at or under this has no structure to compare
_THUMB = 16  # side of the thumbnails that rank candidates before the full comparison
_SHORTLIST = 4  # candidates compared at full size

FloatImage = npt.NDArray[np.float32]


def thumbnail(rgba: npt.NDArray[np.uint8]) -> FloatImage:
    """A 16 x 16 box-filtered copy, for ranking candidates cheaply."""
    image = Image.fromarray(np.ascontiguousarray(rgba), "RGBA").resize((_THUMB, _THUMB), Image.Resampling.BOX)
    return np.asarray(image, dtype=np.float32)


def _shrink(xbox: npt.NDArray[np.uint8], width: int, height: int) -> FloatImage:
    """The Xbox texture box-filtered down to the PS2 texture's size."""
    small = Image.fromarray(np.ascontiguousarray(xbox), "RGBA").resize((width, height), Image.Resampling.BOX)
    return np.asarray(small, dtype=np.float32)


def score(ps2: npt.NDArray[np.uint8], xbox: npt.NDArray[np.uint8]) -> float:
    """How different the Xbox texture, shrunk to the PS2 one's size, is from it (0: the same; see the module)."""
    height, width = ps2.shape[:2]
    return _score(ps2.astype(np.float32), _shrink(xbox, width, height))


def _score(original: FloatImage, shrunk: FloatImage) -> float:
    """score() on two float RGBA arrays of the same size."""
    weight = np.minimum(original[:, :, 3], shrunk[:, :, 3])[:, :, None] / 255.0
    total = float(weight.sum()) * 3
    colour = float((np.abs(shrunk[:, :, :3] - original[:, :, :3]) * weight).sum()) / total if total else 0.0
    alpha = float(np.abs(shrunk[:, :, 3] - original[:, :, 3]).mean())
    return colour + alpha


def _luminance(rgba: FloatImage) -> FloatImage:
    """Luminance weighted by alpha, so a cut-out's shape counts and the colour under transparent texels does not."""
    luma = rgba[:, :, 0] * 0.299 + rgba[:, :, 1] * 0.587 + rgba[:, :, 2] * 0.114
    return np.asarray(luma * rgba[:, :, 3] / 255.0, dtype=np.float32)


def structure(ps2: npt.NDArray[np.uint8], xbox: npt.NDArray[np.uint8]) -> float | None:
    """The correlation (-1 to 1) of the two textures' luminance at the PS2 size; None when either is flat."""
    height, width = ps2.shape[:2]
    return _structure(ps2.astype(np.float32), _shrink(xbox, width, height))


def _structure(original: FloatImage, shrunk: FloatImage) -> float | None:
    """structure() on two float RGBA arrays of the same size."""
    first, second = _luminance(original), _luminance(shrunk)
    spread_first, spread_second = float(first.std()), float(second.std())
    if spread_first <= _FLAT or spread_second <= _FLAT:
        return None
    covariance = float(((first - first.mean()) * (second - second.mean())).mean())
    return covariance / (spread_first * spread_second)


@dataclass(frozen=True)
class ImageLocation:
    """Where an Xbox texture is: its resource image's bytes in an archive volume, and its number in the image."""

    volume: int
    offset: int  # bytes into the volume
    size: int  # bytes of the resource image
    number: int  # the texture's position among the image's texture headers


@dataclass
class Candidate:
    """An Xbox texture that a PS2 texture may match: where it is from, its size and its pixels."""

    source: str  # e.g. `sectors/level99.xlev#12` or `resource 0a1b2c3d#0`
    width: int
    height: int
    rgba: npt.NDArray[np.uint8]
    _thumb: FloatImage | None = None
    location: ImageLocation | None = None  # None for a made-up candidate (tests)

    @property
    def thumb(self) -> FloatImage:
        """The candidate's thumbnail, made on first use."""
        if self._thumb is None:
            self._thumb = thumbnail(self.rgba)
        return self._thumb


@dataclass(frozen=True)
class Match:
    """A candidate for a PS2 texture with its score and structure (None: one of the two is flat)."""

    candidate: Candidate
    score: float
    structure: float | None = 1.0

    @property
    def matches(self) -> bool:
        """Whether this is the same picture: a score under MATCH_LIMIT and, unless flat, the same structure."""
        return self.score < MATCH_LIMIT and (self.structure is None or self.structure >= STRUCTURE_LIMIT)


def compare(ps2: npt.NDArray[np.uint8], candidate: Candidate) -> Match:
    """Score a candidate against a PS2 texture (both measures from one shrunk copy)."""
    height, width = ps2.shape[:2]
    original, shrunk = ps2.astype(np.float32), _shrink(candidate.rgba, width, height)
    return Match(candidate, _score(original, shrunk), _structure(original, shrunk))


def same_aspect(width: int, height: int, other_width: int, other_height: int) -> bool:
    """Whether two sizes have the same aspect ratio."""
    return width * other_height == height * other_width


def best_match(ps2: npt.NDArray[np.uint8], candidates: list[Candidate], first: int | None = None) -> Match | None:
    """The candidate (of the same aspect ratio) that best matches `ps2`, or None when there is none.

    `first` is the index of the candidate to try before ranking: the one at the same position in a resource with
    the same texture count. It is taken when it matches; otherwise every candidate is ranked by thumbnail and the
    best few compared at full size. The lowest-scoring one that matches wins; when none matches, the lowest-scoring
    one is returned (its `matches` is False).
    """
    height, width = ps2.shape[:2]
    if first is not None and 0 <= first < len(candidates):
        guess = candidates[first]
        if same_aspect(width, height, guess.width, guess.height):
            match = compare(ps2, guess)
            if match.matches:
                return match
    fitting = [c for c in candidates if same_aspect(width, height, c.width, c.height)]
    if not fitting:
        return None
    thumb = thumbnail(ps2)
    ranked = sorted(fitting, key=lambda c: float(np.abs(c.thumb - thumb).mean()))[:_SHORTLIST]
    scored = sorted((compare(ps2, c) for c in ranked), key=lambda m: m.score)
    return next((m for m in scored if m.matches), scored[0])


def replaces(ps2_width: int, ps2_height: int, match: Match | None) -> bool:
    """The rule: the Xbox texture replaces the PS2 one when it matches and is larger (same aspect ratio); when more
    than twice as wide, only with a structure of STRUCTURE_LIMIT_FAR or more."""
    if match is None or not match.matches:
        return False
    xbox = match.candidate
    if not same_aspect(ps2_width, ps2_height, xbox.width, xbox.height) or xbox.width <= ps2_width:
        return False
    if xbox.width > 2 * ps2_width:
        return match.structure is not None and match.structure >= STRUCTURE_LIMIT_FAR
    return True
