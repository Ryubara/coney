# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the header and frame index of the game's Bink movies (`PSS/<name>.BIK`), values only.

The layout is the Bink 1 container header as the disc's files have it; what the game does with the movies is on
docs/research/movies.md. Nothing here decodes video or audio.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

#: The movies the game names (boot, level intros and outros, the front end), in the order `movies list` prints them.
#: Research: docs/research/movies.md#the-movies
MOVIE_NAMES = (
    "LOGO",
    "PLOGO",
    "TRAILER",
    "L1_IN",
    "L9_IN",
    "L9_OUT",
    "L31_IN",
    "L31_OUT",
    "L34_OUT",
    "L51_IN",
    "L52_IN",
    "L54_IN",
    "L81_OUT",
    "L84_OUT",
    "L87_OUT",
    "L99_IN",
)
MOVIE_FOLDER = "PSS"
SIGNATURE = b"BIK"
HEADER = 44  # fixed part: signature to the audio track count
_TRACK = 12  # per audio track: u32 largest decoded size, u16 rate, u16 flags, u32 track id
_MAX_FRAMES = 1_000_000  # a sanity bound well above any movie on the disc (18,274 frames)

# Audio track flag bits (the Bink container's meanings).
AUDIO_16BIT = 0x4000
AUDIO_STEREO = 0x2000
AUDIO_DCT = 0x1000


class MovieError(ValueError):
    """A movie file whose header or frame index does not hold together."""


@dataclass(frozen=True)
class AudioTrack:
    """One audio track of a Bink file."""

    largest_decoded: int  # bytes of the largest decoded audio packet
    rate: int  # samples per second
    flags: int
    track_id: int

    @property
    def channels(self) -> int:
        """2 for a stereo track, else 1."""
        return 2 if self.flags & AUDIO_STEREO else 1

    @property
    def bits(self) -> int:
        """Bits per decoded sample: 16, or 8 without the 16-bit flag."""
        return 16 if self.flags & AUDIO_16BIT else 8

    @property
    def codec(self) -> str:
        """`DCT` or `RDFT`: the two Bink audio transforms, chosen by flag 0x1000."""
        return "DCT" if self.flags & AUDIO_DCT else "RDFT"


@dataclass(frozen=True)
class BinkHeader:
    """The values of a Bink file's header and frame index."""

    revision: str  # the fourth byte of the signature, as a letter
    file_size: int  # as the header states it (its stored value plus 8)
    frames: int
    largest_frame: int  # bytes of the largest frame
    width: int
    height: int
    fps_num: int
    fps_den: int
    video_flags: int
    audio: tuple[AudioTrack, ...]
    keyframes: int  # frames whose index entry has bit 0 set

    @property
    def fps(self) -> float:
        """Frames a second."""
        return self.fps_num / self.fps_den

    @property
    def seconds(self) -> float:
        """Running time at the stated rate."""
        return self.frames * self.fps_den / self.fps_num

    @property
    def index_size(self) -> int:
        """Bytes from the start of the file to the end of the frame index (the first frame's data follows)."""
        return HEADER + _TRACK * len(self.audio) + 4 * (self.frames + 1)


def header_size(data: bytes) -> int:
    """How many bytes `parse_header` needs for a file starting with `data` (at least the fixed part)."""
    if len(data) < HEADER:
        return HEADER
    frames, tracks = struct.unpack_from("<I", data, 8)[0], struct.unpack_from("<I", data, 40)[0]
    return HEADER + _TRACK * min(tracks, 256) + 4 * (min(frames, _MAX_FRAMES) + 1)


def parse_header(data: bytes, size: int | None = None, what: str = "movie") -> BinkHeader:
    """Parse the header and frame index at the start of a Bink file; `size` is the file's real size, when known.

    Raises MovieError when the signature, counts, frame rate or index are out of place, or the header's file size
    or the index's last offset disagree with `size`.
    """
    if len(data) < HEADER or data[:3] != SIGNATURE:
        raise MovieError(f"{what}: not a Bink file (no 'BIK' signature)")
    stored_size, frames, largest, _, width, height, fps_num, fps_den, video_flags, tracks = struct.unpack_from(
        "<10I", data, 4
    )
    if not 0 < frames <= _MAX_FRAMES or fps_num == 0 or fps_den == 0 or width == 0 or height == 0:
        raise MovieError(f"{what}: implausible header ({frames} frames, {width}x{height}, {fps_num}/{fps_den} fps)")
    if tracks > 256:
        raise MovieError(f"{what}: {tracks} audio tracks")
    need = HEADER + _TRACK * tracks + 4 * (frames + 1)
    if len(data) < need:
        raise MovieError(f"{what}: header and frame index need {need} bytes, have {len(data)}")
    sizes = struct.unpack_from(f"<{tracks}I", data, HEADER)
    formats = [struct.unpack_from("<HH", data, HEADER + 4 * tracks + 4 * i) for i in range(tracks)]
    ids = struct.unpack_from(f"<{tracks}I", data, HEADER + 8 * tracks)
    audio = tuple(AudioTrack(sizes[i], formats[i][0], formats[i][1], ids[i]) for i in range(tracks))
    index = struct.unpack_from(f"<{frames + 1}I", data, HEADER + _TRACK * tracks)
    offsets = [entry & ~1 for entry in index]
    if offsets != sorted(offsets) or offsets[0] < need:
        raise MovieError(f"{what}: frame index out of order or overlapping the header")
    file_size = stored_size + 8
    if size is not None and (file_size != size or offsets[-1] != size):
        raise MovieError(f"{what}: header says {file_size} bytes, index ends at {offsets[-1]}, file has {size}")
    keyframes = sum(entry & 1 for entry in index[:frames])
    return BinkHeader(
        chr(data[3]), file_size, frames, largest, width, height, fps_num, fps_den, video_flags, audio, keyframes
    )
