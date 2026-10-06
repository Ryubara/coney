# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools audio ...` commands: info, list and decode, over the player's own disc.

They read the sound tables of `warriors.glr`, the banks in the WAD and the two streamed files of `IOP/`, and print
counts, offsets and hashes only; `decode` writes one sound as a WAV file outside the repository, for listening.

Research: docs/research/formats/audio.md
"""

from __future__ import annotations

import hashlib
import itertools
import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

from coney_tools import audio, wad
from coney_tools.chunks import parse_container
from coney_tools.config import ConfigError
from coney_tools.disc import Disc
from coney_tools.wad_cli import open_disc

#: Bank names whose `.msd` hash is known (docs/research/formats/audio.md#banks); others print as hashes.
KNOWN_BANKS = (
    "armies", "armload", "birdie", "diego", "gallery", "level3", "level81", "lizzie", "luther", "menu", "pause",
    "sound", "spookorama", *(f"load_{i:02d}" for i in range(7)),
)  # fmt: skip
_MAX_INDEX = 1 << 16  # a `.msd` is a few KB; larger entries are not looked at


@dataclass(frozen=True)
class Bank:
    """A sound bank: its name (or hash), its index pairs, and the WAD entry of its samples."""

    name: str
    pairs: list[tuple[int, int]]
    samples: wad.WadEntry


@dataclass(frozen=True)
class Tables:
    """Everything the audio commands read from the disc, once."""

    sounds: list[audio.SoundRecord]
    classes: list[audio.SoundClass]
    stereo: dict[int, audio.StereoInfo]
    music: list[audio.MusicTrack]
    chunk_digests: dict[int, str]
    banks: list[Bank]

    def sound_class(self, record: audio.SoundRecord) -> audio.SoundClass:
        """The class of a record (a placeholder class when the index is out of the table)."""
        if record.sound_class < len(self.classes):
            return self.classes[record.sound_class]
        return audio.SoundClass(0, 0, 0, 0, 1)


def _digest(data: bytes) -> str:
    """A short SHA-1 to tell copies of the data apart without printing it."""
    return hashlib.sha1(data).hexdigest()[:12]


def _read(handle: BinaryIO, entry: wad.WadEntry) -> bytes:
    """One WAD entry's bytes."""
    handle.seek(entry.offset)
    return handle.read(entry.size)


def _is_bank_index(data: bytes) -> bool:
    """A `.msd`: pairs from offset 0, rising, 16-aligned, then a zero pair and nothing but zeros."""
    pairs = audio.parse_bank_index(data)
    if not pairs or pairs[0][1] != 0 or len(data) % 8:
        return False
    rising = all(a[1] < b[1] for a, b in itertools.pairwise(pairs))
    return rising and all(o % 16 == 0 for _, o in pairs) and not any(data[len(pairs) * 8 :])


def load_tables(disc: Disc) -> Tables:
    """Read the sound tables of `warriors.glr` and find the banks."""
    entries = wad.load_entries(disc)
    by_hash = {entry.hash: entry for entry in entries}
    glr = by_hash.get(wad.name_hash(wad.NAME_PREFIX + "warriors.glr"))
    if glr is None:
        raise ConfigError(f"{disc.path}: no warriors.glr in the WAD")
    with disc.open(wad.WAD_FILE) as handle:
        data = _read(handle, glr)
        container = parse_container(data)
        if container is None:
            raise ConfigError("warriors.glr is not a chunk container")
        chunks = {c.type: data[c.offset : c.offset + c.size] for r in container.resources for c in r.chunks}
        wanted = (audio.CHUNK_SOUND_LIST, audio.CHUNK_MUSIC_LIST, audio.CHUNK_SOUND_CLASSES, audio.CHUNK_STEREO_TABLE)
        missing = [hex(t) for t in wanted if t not in chunks]
        if missing:
            raise ConfigError(f"warriors.glr lacks the sound chunks {', '.join(missing)}")
        names = {wad.name_hash(wad.NAME_PREFIX + n + ".msd"): n for n in KNOWN_BANKS}
        banks = []
        for i, entry in enumerate(entries):
            if i == 0 or entry.size > _MAX_INDEX or entry.size % 8:
                continue
            index = _read(handle, entry)
            if _is_bank_index(index):
                banks.append(
                    Bank(names.get(entry.hash, f"0x{entry.hash:08x}"), audio.parse_bank_index(index), entries[i - 1])
                )
    return Tables(
        sounds=audio.parse_sound_list(chunks[audio.CHUNK_SOUND_LIST]),
        classes=audio.parse_sound_classes(chunks[audio.CHUNK_SOUND_CLASSES]),
        stereo={s.hash: s for s in audio.parse_stereo_table(chunks[audio.CHUNK_STEREO_TABLE])},
        music=audio.parse_music_list(chunks[audio.CHUNK_MUSIC_LIST]),
        chunk_digests={t: _digest(chunks[t]) for t in wanted},
        banks=sorted(banks, key=lambda b: b.name),
    )


def _kind(tables: Tables, record: audio.SoundRecord) -> str:
    """`stream`, `stereo` or `bank`: where a sound's bytes are."""
    sound_class = tables.sound_class(record)
    if not sound_class.streamed:
        return "bank"
    return "stereo" if record.hash in tables.stereo else "stream"


def run_info(disc_arg: str | None) -> int:
    """Print the counts of every kind of sound data, with short hashes of the tables."""
    disc = open_disc(disc_arg)
    tables = load_tables(disc)
    kinds = Counter(_kind(tables, r) for r in tables.sounds)
    print(f"sound list: {len(tables.sounds)} records (chunk 0x29, sha1 {tables.chunk_digests[audio.CHUNK_SOUND_LIST]})")
    for kind in ("stream", "stereo", "bank"):
        print(f"  {kind:<7} {kinds[kind]}")
    print(f"sound classes: {len(tables.classes)} (chunk 0x48, sha1 {tables.chunk_digests[audio.CHUNK_SOUND_CLASSES]})")
    print(f"stereo table: {len(tables.stereo)} (chunk 0x49, sha1 {tables.chunk_digests[audio.CHUNK_STEREO_TABLE]})")
    rates = Counter(r.sample_rate for r in tables.sounds)
    print("sample rates: " + ", ".join(f"{rate} Hz x{count}" for rate, count in rates.most_common(6)))
    music_rates = Counter(t.sample_rate for t in tables.music)
    print(
        f"music list: {len(tables.music)} tracks (chunk 0x31, sha1 {tables.chunk_digests[audio.CHUNK_MUSIC_LIST]}); "
        + ", ".join(f"{rate} Hz x{count}" for rate, count in music_rates.most_common())
    )
    for name in (audio.SOUND_FILE, audio.MUSIC_FILE):
        print(f"{name}: {disc.size(name) if disc.has(name) else 'missing'} bytes")
    in_list = {r.hash for r in tables.sounds}
    print(f"banks: {len(tables.banks)}")
    for bank in tables.banks:
        listed = sum(1 for h, _ in bank.pairs if h in in_list)
        print(f"  {bank.name:<12} {len(bank.pairs):>4} sounds ({listed} in the list), {bank.samples.size} bytes")
    return 0


def run_list(disc_arg: str | None, what: str) -> int:
    """Print one line per sound, music track or bank sound: hashes, offsets, sizes and the facts of its records."""
    tables = load_tables(open_disc(disc_arg))
    if what == "music":
        for t in tables.music:
            print(
                f"0x{t.hash:08x} {t.offset:>10} {t.size:>9} {t.sample_rate:>6} Hz {t.channels} ch "
                f"interleave 0x{t.interleave:x} last 0x{t.last_block:x} volume {t.volume:g} {t.name}"
            )
    elif what == "banks":
        for bank in tables.banks:
            for key, offset in bank.pairs:
                print(f"{bank.name} 0x{key:08x} {offset:>8}")
    else:
        for r in tables.sounds:
            c = tables.sound_class(r)
            print(
                f"0x{r.hash:08x} {_kind(tables, r):<6} {r.offset:>10} {r.size:>8} {r.sample_rate:>5} Hz "
                f"class {r.sound_class:>3} flags 0x{c.flags:02x} prio {c.priority:>2} dist {c.near}-{c.far} "
                f"vol {r.volume}% pitch +-{r.pitch_variation}%"
            )
    return 0


def _parse_target(target: str) -> int:
    """A name (hashed as written) or a `0x` hash."""
    if target.lower().startswith("0x"):
        try:
            return int(target, 16)
        except ValueError as error:
            raise ConfigError(f"{target}: not a hash") from error
    return zlib.crc32(target.encode("latin-1"))


def run_decode(disc_arg: str | None, target: str, out: Path, bank_name: str | None) -> int:
    """Decode one sound or music track to a WAV file outside the repository; print its facts and a waveform check."""
    wad.refuse_inside_repo(out)
    disc = open_disc(disc_arg)
    tables = load_tables(disc)
    key = _parse_target(target)
    track = next((t for t in tables.music if t.hash == key), None)
    if track is not None:
        with disc.open(audio.MUSIC_FILE) as handle:
            handle.seek(track.offset)
            data = handle.read(track.size)
        channels = audio.decode_channels(audio.deinterleave(data, track.channels, track.interleave))
        rate = track.sample_rate
    else:
        record = next((r for r in tables.sounds if r.hash == key), None)
        if record is None:
            raise ConfigError(f"{target}: not in the sound list or the music list")
        rate = record.sample_rate
        kind = "bank" if bank_name else _kind(tables, record)
        if kind == "bank":
            channels = [_decode_bank_sound(disc, tables, record, bank_name)]
        else:
            stereo = tables.stereo.get(key)
            size = 2 * stereo.interleave * stereo.blocks if stereo else record.size
            with disc.open(audio.SOUND_FILE) as handle:
                handle.seek(record.offset)
                data = handle.read(size)
            parts = audio.deinterleave(data, 2, stereo.interleave) if stereo else [data]
            channels = audio.decode_channels(parts)
    out.parent.mkdir(parents=True, exist_ok=True)
    audio.write_wav(out, channels, rate)
    peak, rms, clipped = audio.stats([s for c in channels for s in c])
    seconds = len(channels[0]) / rate if rate else 0.0
    print(f"{out}: {len(channels)} ch, {rate} Hz, {seconds:.2f} s; peak {peak}, rms {rms:.0f}, clipped {clipped:.2%}")
    return 0


def _decode_bank_sound(disc: Disc, tables: Tables, record: audio.SoundRecord, bank_name: str | None) -> list[int]:
    """Decode a sound from the first bank (or the named one) that holds it; a bank sample stops at its end flag."""
    for bank in tables.banks:
        if bank_name and bank.name != bank_name:
            continue
        offset = next((o for h, o in bank.pairs if h == record.hash), None)
        if offset is not None:
            with disc.open(wad.WAD_FILE) as handle:
                handle.seek(bank.samples.offset + offset)
                data = handle.read(record.size)
            return audio.AdpcmDecoder().decode(data, stop_at_end=True)
    raise ConfigError(f"0x{record.hash:08x}: in no bank" + (f" named {bank_name}" if bank_name else ""))
