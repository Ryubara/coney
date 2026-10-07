# SPDX-License-Identifier: GPL-3.0-or-later
"""PS2 SPU2 ADPCM decoded in bulk with NumPy: many independent channels at once.

The codec (docs/research/formats/audio.md#ps2-adpcm-all-sound-data) is a two-tap prediction filter, so each sample
depends on the two before it and one channel cannot be decoded in parallel with itself. Many channels can: the
decoder steps through sample positions and advances every channel of a batch by one sample per step, so the Python
loop runs once per sample position instead of once per sample. `audio.AdpcmDecoder` is the plain, one-channel
version of the same arithmetic; the tests check that both agree.

Research: docs/research/formats/audio.md
"""

from __future__ import annotations

from collections.abc import Iterator, Sequence

import numpy as np
import numpy.typing as npt

from coney_tools.audio import FLAG_END, FRAME, FRAME_SAMPLES

_F0 = np.array([0, 60, 115, 98, 122], dtype=np.int32)
_F1 = np.array([0, 0, -52, -55, -60], dtype=np.int32)
_WINDOW = 2048  # frames decoded per step of the outer loop, to bound memory on long streams
BATCH = 512  # channels decoded together: wide enough that the per-step overhead is shared


def cut_at_end(data: bytes) -> bytes:
    """A bank sample's bytes up to and including its first frame flagged as the end."""
    for at in range(0, len(data) - FRAME + 1, FRAME):
        if data[at + 1] & FLAG_END:
            return data[: at + FRAME]
    return data


def _frames(data: bytes) -> npt.NDArray[np.uint8]:
    """The whole 16-byte frames of a stream as a (frames, 16) array."""
    count = len(data) // FRAME
    return np.frombuffer(data, dtype=np.uint8, count=count * FRAME).reshape(count, FRAME)


_Ints = npt.NDArray[np.int32]


def _deltas(frames: npt.NDArray[np.uint8]) -> tuple[_Ints, _Ints, _Ints]:
    """Each frame's 28 shifted nibbles (the part of a sample that does not depend on earlier samples) and its two
    filter coefficients."""
    shift = (frames[:, 0] & 0x0F).astype(np.int32)
    shift[shift > 12] = 9  # the SPU2 treats shifts 13-15 as 9
    predictor = np.minimum(frames[:, 0] >> 4, 4)  # and predictors 5-15 as 4
    body = frames[:, 2:]
    nibbles = np.empty((frames.shape[0], FRAME_SAMPLES), dtype=np.int32)
    nibbles[:, 0::2] = body & 0x0F
    nibbles[:, 1::2] = body >> 4
    signed = (nibbles << 12).astype(np.int16).astype(np.int32)  # the nibble in the top four bits of a 16-bit word
    return signed >> shift[:, None], _F0[predictor], _F1[predictor]


def decode_batch(streams: Sequence[bytes]) -> list[npt.NDArray[np.int16]]:
    """Decode up to a few hundred channels together; each result has 28 samples per whole frame of its stream."""
    frame_counts = [len(s) // FRAME for s in streams]
    longest = max(frame_counts, default=0)
    width = len(streams)
    results = [np.empty(count * FRAME_SAMPLES, dtype=np.int16) for count in frame_counts]
    if longest == 0:
        return results
    s1 = np.zeros(width, dtype=np.int32)
    s2 = np.zeros(width, dtype=np.int32)
    parsed = [_deltas(_frames(s)) for s in streams]
    for first in range(0, longest, _WINDOW):
        last = min(first + _WINDOW, longest)
        span = last - first
        # Gather the window of every channel, padded with silent frames (filter 0, no delta) past its end.
        delta = np.zeros((span * FRAME_SAMPLES, width), dtype=np.int32)
        f0 = np.zeros((span, width), dtype=np.int32)
        f1 = np.zeros((span, width), dtype=np.int32)
        for column, (deltas, c0, c1) in enumerate(parsed):
            have = min(last, frame_counts[column]) - first
            if have > 0:
                delta[: have * FRAME_SAMPLES, column] = deltas[first : first + have].ravel()
                f0[:have, column] = c0[first : first + have]
                f1[:have, column] = c1[first : first + have]
        out = np.empty_like(delta)
        for frame in range(span):
            c0 = f0[frame]
            c1 = f1[frame]
            base = frame * FRAME_SAMPLES
            for position in range(base, base + FRAME_SAMPLES):
                sample = delta[position] + ((s1 * c0 + s2 * c1 + 32) >> 6)
                np.clip(sample, -32768, 32767, out=sample)
                out[position] = sample
                s2 = s1
                s1 = sample
        for column, count in enumerate(frame_counts):
            have = min(last, count) - first
            if have > 0:
                results[column][first * FRAME_SAMPLES : (first + have) * FRAME_SAMPLES] = out[
                    : have * FRAME_SAMPLES, column
                ]
    return results


def batches(lengths: Sequence[int], size: int = BATCH) -> Iterator[list[int]]:
    """Indexes of the streams grouped for `decode_batch`: longest first, so a batch's streams are of similar length
    and little time goes on padding."""
    order = sorted(range(len(lengths)), key=lambda i: (-lengths[i], i))
    for start in range(0, len(order), size):
        yield order[start : start + size]
