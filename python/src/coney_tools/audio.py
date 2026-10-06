# SPDX-License-Identifier: GPL-3.0-or-later
"""The game's sound data: the sound list and its tables in `warriors.glr`, the sound banks, and the PS2 ADPCM codec.

Where the audio lives (docs/research/formats/audio.md):

* `IOP/BFW.SND` on the disc: every named sound, raw PS2 ADPCM, streamed from the disc. The **sound list** (chunk
  `0x29` of `warriors.glr`) gives each sound's size, offset, name hash, pitch variation, volume, sample-rate index
  and class; the **sound classes** (chunk `0x48`) give the class's distances, flags, priority and channel count; the
  **stereo table** (chunk `0x49`) gives a stereo sound's interleave.
* `IOP/MUSIC.SND`: the music, stereo PS2 ADPCM interleaved in blocks; the **music list** (chunk `0x31`) gives each
  track's name, rate, offset, interleave and block count.
* `<bank>.msb` / `<bank>.msd` in the WAD: banks loaded into sound RAM; the `.msd` maps a name hash to an offset in the
  `.msb`, and the sound list gives the size and rate.

The ADPCM decoder is the public PS2 SPU2 ("VAG") algorithm: 16-byte frames of 28 samples, a header byte holding the
shift (low nibble) and the predictor (high nibble), a flag byte, then 14 bytes of 4-bit samples, low nibble first.
"""

from __future__ import annotations

import struct
import wave
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

#: Chunk types of `warriors.glr` that hold sound tables (docs/research/chunk-system.md).
CHUNK_SOUND_LIST = 0x29
CHUNK_MUSIC_LIST = 0x31
CHUNK_SOUND_CLASSES = 0x48
CHUNK_STEREO_TABLE = 0x49

#: Disc files that hold the streamed sound and the music.
SOUND_FILE = "IOP/BFW.SND"
MUSIC_FILE = "IOP/MUSIC.SND"

#: The sample rates a sound list record indexes with its `rate` byte: steps of 750 Hz with the common rates
#: (8000, 11025, 16000, 22050, 44100 ...) slotted in; read from the executable at 0x0050a9e0 (73 entries).
SAMPLE_RATES: tuple[int, ...] = (
    0, 750, 1500, 2250, 3000, 3750, 4500, 5250, 6000, 6750, 7500, 8000, 8250, 9000, 9750, 10500, 11000, 11025,
    11250, 12000, 12750, 13500, 14250, 15000, 15750, 16000, 16500, 17250, 18000, 18750, 19500, 20250, 21000, 21750,
    22050, 22250, 22500, 23250, 24000, 24750, 25500, 26250, 27000, 27750, 28500, 29250, 30000, 30750, 31500, 32250,
    33000, 33750, 34500, 35250, 36000, 36750, 37500, 38250, 39000, 39750, 40500, 41250, 42000, 42750, 43500, 44100,
    44250, 45000, 45750, 46500, 47250, 48000, 9999,
)  # fmt: skip

FRAME = 16  # bytes of one ADPCM frame
FRAME_SAMPLES = 28
_COEFFICIENTS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))

#: Frame flag bits (byte 1 of a frame), as the SPU2 reads them.
FLAG_END = 0x01  # the last frame of a sample or a loop
FLAG_REPEAT = 0x02  # with FLAG_END: jump to the loop start instead of stopping
FLAG_LOOP_START = 0x04


@dataclass(frozen=True)
class SoundRecord:
    """One sound of the sound list (16 bytes)."""

    size: int  # bytes of ADPCM
    offset: int  # in BFW.SND
    hash: int  # CRC-32 of the sound's name as written
    pitch_variation: int  # percent: each play picks a pitch factor in 1 +- variation / 100
    volume: int  # percent, 100 = as recorded
    rate_index: int  # into SAMPLE_RATES
    sound_class: int  # into the sound classes

    @property
    def sample_rate(self) -> int:
        """The sample rate in Hz, 0 when the index is out of the table."""
        return SAMPLE_RATES[self.rate_index] if self.rate_index < len(SAMPLE_RATES) else 0


@dataclass(frozen=True)
class SoundClass:
    """One sound class (5 bytes): how sounds of the class are placed, prioritised and played."""

    near: int  # metres: full volume within this distance
    far: int  # metres: silent beyond (the record stores far / 5)
    flags: int
    priority: int  # lower is more important
    channels: int  # 1 or 2

    @property
    def looped(self) -> bool:
        """Bit 0: the sound loops until stopped."""
        return bool(self.flags & 0x01)

    @property
    def positional(self) -> bool:
        """Bits 1 or 3: the sound has a position in the world (3D)."""
        return bool(self.flags & 0x0A)

    @property
    def streamed(self) -> bool:
        """Bit 2: the sound streams from BFW.SND; clear, it plays from a bank loaded into sound RAM."""
        return bool(self.flags & 0x04)


@dataclass(frozen=True)
class StereoInfo:
    """A stereo sound's layout in BFW.SND (chunk 0x49, 16 bytes)."""

    hash: int
    interleave: int  # bytes per channel per block
    last_block: int  # bytes per channel in the last block
    blocks: int


@dataclass(frozen=True)
class MusicTrack:
    """One track of the music list (104 bytes)."""

    name: str
    hash: int
    volume: float
    sample_rate: int
    offset: int  # in MUSIC.SND
    channels: int
    interleave: int  # bytes per channel per block
    blocks: int
    last_block: int  # bytes per channel of the last block

    @property
    def size(self) -> int:
        """The bytes the track spans in MUSIC.SND (whole blocks, as the game asks for them)."""
        return self.channels * self.interleave * self.blocks


def _count_and_body(data: bytes, record: int) -> tuple[int, int]:
    """A chunk that starts with a 32-bit count: the count, checked against the chunk's size."""
    if len(data) < 4:
        raise ValueError("chunk too short for its count")
    count = struct.unpack_from("<I", data)[0]
    if 4 + count * record > len(data):
        raise ValueError(f"{count} records of {record} bytes do not fit in {len(data)} bytes")
    return count, 4


def parse_sound_list(data: bytes) -> list[SoundRecord]:
    """Chunk 0x29: `u32 count`, then `count` records `{u32 size, u32 offset, u32 hash, u8 pitch, u8 volume, u8
    rate, u8 class}`, sorted by hash (the game finds a sound by binary search)."""
    count, start = _count_and_body(data, 16)
    return [SoundRecord(*struct.unpack_from("<III4B", data, start + 16 * i)) for i in range(count)]


def parse_sound_classes(data: bytes) -> list[SoundClass]:
    """Chunk 0x48: 5-byte records with no count `{u8 near, u8 far / 5, u8 flags, u8 priority, u8 channels}`."""
    classes = []
    for i in range(len(data) // 5):
        near, far5, flags, priority, channels = data[5 * i : 5 * i + 5]
        if channels == 0:  # the chunk's padding
            break
        classes.append(SoundClass(near, far5 * 5, flags, priority, channels))
    return classes


def parse_stereo_table(data: bytes) -> list[StereoInfo]:
    """Chunk 0x49: `u32 count`, 12 bytes, then `count` records `{u32 hash, u32 interleave, u32 last, u32 blocks}`."""
    count = struct.unpack_from("<I", data)[0] if len(data) >= 4 else 0
    if 16 + 16 * count > len(data):
        raise ValueError(f"{count} stereo records do not fit in {len(data)} bytes")
    return [StereoInfo(*struct.unpack_from("<4I", data, 16 + 16 * i)) for i in range(count)]


def parse_music_list(data: bytes) -> list[MusicTrack]:
    """Chunk 0x31: `u32 count`, then `count` records of 104 bytes: `f32 volume, u32 rate, u32 64, u32 32, u32
    offset, u32 channels, u32 interleave, u32 blocks, u32 last, u32 hash, char name[64]`."""
    count, start = _count_and_body(data, 104)
    tracks = []
    for i in range(count):
        at = start + 104 * i
        volume, rate, _, _, offset, channels, interleave, blocks, last, key = struct.unpack_from("<f9I", data, at)
        name = data[at + 40 : at + 104].split(b"\0", 1)[0].decode("latin-1")
        tracks.append(MusicTrack(name, key, volume, rate, offset, channels, interleave, blocks, last))
    return tracks


def parse_bank_index(data: bytes) -> list[tuple[int, int]]:
    """A `.msd`: `{u32 hash, u32 offset}` pairs into the `.msb`, ended by a zero pair."""
    pairs = []
    for at in range(0, len(data) - 7, 8):
        key, offset = struct.unpack_from("<II", data, at)
        if key == 0 and offset == 0:
            break
        pairs.append((key, offset))
    return pairs


class AdpcmDecoder:
    """PS2 SPU2 ADPCM decoder for one channel; keeps the two previous samples across calls."""

    def __init__(self) -> None:
        self.s1 = 0
        self.s2 = 0

    def frame(self, block: bytes | memoryview) -> list[int]:
        """Decode one 16-byte frame into 28 signed 16-bit samples."""
        shift = block[0] & 0x0F
        predictor = min(block[0] >> 4, 4)  # the SPU2 treats 5-15 as 4
        f0, f1 = _COEFFICIENTS[predictor]
        shift = 9 if shift > 12 else shift  # the SPU2 treats shifts 13-15 as 9
        out = []
        s1, s2 = self.s1, self.s2
        for byte in block[2:16]:
            for nibble in (byte & 0x0F, byte >> 4):
                value = ((nibble << 12) & 0xFFFF) - (0x10000 if nibble & 0x8 else 0)
                sample = (value >> shift) + ((s1 * f0 + s2 * f1 + 32) >> 6)
                sample = max(-32768, min(32767, sample))
                out.append(sample)
                s2, s1 = s1, sample
        self.s1, self.s2 = s1, s2
        return out

    def decode(self, data: bytes | memoryview, stop_at_end: bool = False) -> list[int]:
        """Decode whole frames; with `stop_at_end`, stop after the first frame flagged as the end of the sample."""
        out: list[int] = []
        view = memoryview(data)
        for at in range(0, len(view) - FRAME + 1, FRAME):
            block = view[at : at + FRAME]
            out.extend(self.frame(block))
            if stop_at_end and block[1] & FLAG_END:
                break
        return out


def frame_flags(data: bytes) -> dict[int, int]:
    """How many frames carry each flag byte: a quick look at whether a span is one sample or a stream."""
    counts: dict[int, int] = {}
    for at in range(1, len(data), FRAME):
        counts[data[at]] = counts.get(data[at], 0) + 1
    return counts


def deinterleave(data: bytes, channels: int, interleave: int) -> list[bytes]:
    """Split interleaved blocks (`interleave` bytes of channel 0, then of channel 1, ...) into one stream per
    channel; a short final block is split evenly."""
    if channels == 1:
        return [data]
    parts: list[list[bytes]] = [[] for _ in range(channels)]
    step = channels * interleave
    for at in range(0, len(data), step):
        block = data[at : at + step]
        size = len(block) // channels if len(block) < step else interleave
        size -= size % FRAME
        for c in range(channels):
            parts[c].append(block[c * size : (c + 1) * size])
    return [b"".join(p) for p in parts]


def decode_channels(streams: Iterable[bytes], stop_at_end: bool = False) -> list[list[int]]:
    """Decode each channel's ADPCM with its own decoder."""
    return [AdpcmDecoder().decode(s, stop_at_end) for s in streams]


def interleave_samples(channels: list[list[int]]) -> Iterator[int]:
    """Frame-interleave decoded channels for a WAV file (shortest channel wins)."""
    for frame in zip(*channels, strict=False):
        yield from frame


def write_wav(path: Path, channels: list[list[int]], rate: int) -> None:
    """Write 16-bit PCM WAV."""
    samples = list(interleave_samples(channels))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(len(channels))
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def encode_frame(samples: list[int], flags: int = 0) -> bytes:
    """Encode 28 samples as one frame with predictor 0 and the smallest shift that fits; for synthetic test data."""
    if len(samples) != FRAME_SAMPLES:
        raise ValueError("a frame holds 28 samples")
    peak = max(abs(s) for s in samples)
    shift = 12  # a nibble n decodes to n * 2^(12 - shift): the largest shift whose step still reaches the peak
    while shift > 0 and peak > 7 << (12 - shift):
        shift -= 1
    nibbles = []
    for s in samples:
        value = max(-8, min(7, round(s / (1 << (12 - shift)))))
        nibbles.append(value & 0x0F)
    body = bytes(nibbles[i] | (nibbles[i + 1] << 4) for i in range(0, FRAME_SAMPLES, 2))
    return bytes((shift, flags)) + body


def stats(samples: list[int]) -> tuple[int, float, float]:
    """Peak, RMS and the share of samples at full scale: a waveform sanity check without listening."""
    if not samples:
        return 0, 0.0, 0.0
    peak = max(abs(s) for s in samples)
    rms = (sum(s * s for s in samples) / len(samples)) ** 0.5
    clipped = sum(1 for s in samples if s in (32767, -32768)) / len(samples)
    return peak, rms, clipped
