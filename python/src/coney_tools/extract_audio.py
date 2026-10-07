# SPDX-License-Identifier: GPL-3.0-or-later
"""The `audio` stage of `coney-tools extract`: every sound, bank sample and music track as a WAV file.

* `audio/sounds/<name>.wav`: each streamed sound of `IOP/BFW.SND` (mono, or stereo for the 248 class-224 beds), cut
  where the game stops it;
* `audio/banks/<bank>/<name>.wav`: each sample of each sound bank (`<bank>.msb` in the WAD), up to its end flag;
* `audio/music/<track>.wav`: each track of `IOP/MUSIC.SND`, stereo, cut at the last block's end;
* `audio/sounds.json`, `audio/music.json`, `audio/classes.json`: the tables, with the file each entry became.

A sound's name is the one the game hashes (`vags/character/voices/5/attack_01`, written without `vags/`); names come
from strings on the disc and from the voice-set pattern (docs/research/sound.md), and a sound whose name is not
recovered is written as `unnamed/<hash>.wav`.

Research: docs/research/formats/audio.md, docs/research/sound.md
"""

from __future__ import annotations

import re
import zlib
from collections.abc import Iterable
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt

from coney_tools import adpcm, audio, audio_cli, wad
from coney_tools.disc import Disc
from coney_tools.extract_output import Output, Report, wav_bytes

KIND = "audio"
#: Voice sets and lines tried for the voice pattern (`config_preload.lua` allocates 350 sets; 54 lines at most).
_VOICE_SETS = 400
_VOICE_LINES = 54
_DIGITS = re.compile(r"\d+")


@dataclass(frozen=True)
class _Job:
    """One sound to decode: where its ADPCM is on the disc, how it is laid out, and where the WAV goes. Jobs carry
    no audio bytes, so a worker process reads its own and the disc's audio is never all in memory at once."""

    path: str
    source: str  # disc file
    offset: int
    size: int
    channels: int
    interleave: int  # bytes per channel per block; 0 for mono
    keep: int  # samples per channel to keep (0: all)
    rate: int
    to_end: bool = False  # a bank sample: stop after the frame flagged as the end

    @property
    def samples(self) -> int:
        """Samples per channel before trimming, for grouping jobs of similar length."""
        return self.size // self.channels // audio.FRAME * audio.FRAME_SAMPLES

    def read(self, disc: Disc) -> list[bytes]:
        """The job's ADPCM, one stream per channel."""
        with disc.open(self.source) as handle:
            handle.seek(self.offset)
            data = handle.read(self.size)
        if self.to_end:
            data = adpcm.cut_at_end(data)
        if self.channels == 1:
            return [data]
        return audio.deinterleave(data, self.channels, self.interleave)


def _try(name: str, wanted: set[int], found: dict[int, str]) -> bool:
    """Hash a candidate name as written; keep it when it names a sound not yet named."""
    key = zlib.crc32(name.encode("latin-1", "replace"))
    if key in wanted and key not in found:
        found[key] = name
        return True
    return False


def _neighbours(name: str) -> Iterable[str]:
    """Names that differ from `name` in one number: the other lines, takes and sets of a family
    (`l11_t25_006` -> `l11_t25_007` ...), keeping a zero-padded number's width."""
    for match in _DIGITS.finditer(name):
        text = match.group()
        top = max(int(text) * 2, 60)
        for value in range(top + 1):
            replaced = str(value).zfill(len(text)) if text.startswith("0") else str(value)
            if replaced != text:
                yield name[: match.start()] + replaced + name[match.end() :]


def recover_sound_names(hashes: set[int], strings: Iterable[str], commands: Iterable[str]) -> dict[int, str]:
    """Name as many sound hashes as possible: each disc string as a name and in the folders the game uses, the
    voice-set pattern `vags/character/voices/<set>/<command>_<nn>`, then the numeric neighbours of every name found
    until no new one turns up."""
    found: dict[int, str] = {}
    for text in strings:
        if not text or len(text) > 96:
            continue
        for candidate in (text, f"vags/{text}", f"vags/music/{text}", f"vags/speeches/{text.split('_', 1)[0]}/{text}"):
            _try(candidate, hashes, found)
    for command in commands:
        if not command:
            continue
        for voice_set in range(_VOICE_SETS):
            for line in range(1, _VOICE_LINES + 1):
                _try(f"vags/character/voices/{voice_set}/{command}_{line:02d}", hashes, found)
    fresh = list(found.values())
    while fresh:
        fresh = [n for name in fresh for n in _neighbours(name) if _try(n, hashes, found)]
    return found


def _file_name(name: str | None, key: int) -> str:
    """The path below a folder for a sound: its name without `vags/`, or `unnamed/<hash>`."""
    if name is None:
        return f"unnamed/{key:08x}.wav"
    return name.removeprefix("vags/") + ".wav"


def _trim(channel_bytes: int, interleave: int, blocks: int, last_block: int) -> int:
    """Samples per channel of an interleaved stream that ends `last_block` bytes into its last block."""
    if blocks <= 0 or last_block <= 0:
        return channel_bytes // audio.FRAME * audio.FRAME_SAMPLES
    return ((blocks - 1) * interleave + last_block) // audio.FRAME * audio.FRAME_SAMPLES


def _decode(disc_path: Path, jobs: list[_Job]) -> list[tuple[str, bytes]]:
    """Decode a group of jobs together and encode each as WAV (run in a worker process)."""
    disc = Disc(disc_path)
    streams = [job.read(disc) for job in jobs]
    decoded = adpcm.decode_batch([c for channels in streams for c in channels])
    result = []
    at = 0
    for job in jobs:
        channels = decoded[at : at + job.channels]
        at += job.channels
        length = min(len(c) for c in channels)
        if job.keep:
            length = min(length, job.keep)
        frames: npt.NDArray[np.int16] = np.stack([c[:length] for c in channels], axis=1)
        result.append((job.path, wav_bytes(frames, job.rate)))
    return result


def _run_jobs(disc: Disc, jobs: list[_Job], output: Output, workers: int) -> None:
    """Decode every job in batches of similar length, in parallel when `workers` > 1, writing in a fixed order."""
    groups = [[jobs[i] for i in indexes] for indexes in adpcm.batches([job.samples for job in jobs], adpcm.BATCH // 2)]

    def write(done: list[tuple[str, bytes]]) -> None:
        for path, data in done:
            output.write(KIND, path, data)

    if workers <= 1:
        for group in groups:
            write(_decode(disc.path, group))
        return
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for done in pool.map(_decode, [disc.path] * len(groups), groups):
            write(done)


def extract(disc: Disc, output: Output, strings: Iterable[str], commands: Iterable[str], workers: int) -> Report:
    """Write every sound, bank sample and music track, and the audio tables, into `output`."""
    report = Report(KIND)
    output.start(KIND)
    tables = audio_cli.load_tables(disc)
    names = recover_sound_names({r.hash for r in tables.sounds}, strings, commands)
    report.count("sound names recovered", len(names))
    jobs: list[_Job] = []
    claimed: dict[str, int] = {}

    def claim(folder: str, name: str | None, key: int) -> str:
        # Two records may share a hash (the list holds a few such pairs): number the later ones.
        path = f"audio/{folder}/{_file_name(name, key)}"
        claimed[path] = claimed.get(path, 0) + 1
        if claimed[path] > 1:
            path = f"{path.removesuffix('.wav')}~{claimed[path]}.wav"
        return path

    # Streamed sounds, from BFW.SND.
    sounds_index = []
    for record in tables.sounds:
        sound_class = tables.sound_class(record)
        entry: dict[str, object] = {
            "hash": f"{record.hash:08x}",
            "name": names.get(record.hash),
            "where": "stream" if sound_class.streamed else "bank",
            "rate": record.sample_rate,
            "class": record.sound_class,
            "volume": record.volume,
            "pitch_variation": record.pitch_variation,
            "size": record.size,
            "offset": record.offset,
        }
        if sound_class.streamed:
            stereo = tables.stereo.get(record.hash)
            path = claim("sounds", names.get(record.hash), record.hash)
            rate = record.sample_rate
            if stereo is not None:
                size = 2 * stereo.interleave * stereo.blocks
                keep = _trim(size // 2, stereo.interleave, stereo.blocks, stereo.last_block)
                jobs.append(_Job(path, audio.SOUND_FILE, record.offset, size, 2, stereo.interleave, keep, rate))
                entry["channels"] = 2
                report.count("stereo sounds")
            else:
                jobs.append(_Job(path, audio.SOUND_FILE, record.offset, record.size, 1, 0, 0, rate))
                entry["channels"] = 1
                report.count("streamed sounds")
            entry["file"] = path
        sounds_index.append(entry)
    # Bank samples, from the WAD.
    by_hash = {r.hash: r for r in tables.sounds}
    for bank in tables.banks:
        for key, offset in bank.pairs:
            sample = by_hash.get(key)
            if sample is None:
                report.problem(f"bank {bank.name}: sound {key:08x} is not in the sound list")
                continue
            path = claim(f"banks/{bank.name}", names.get(key), key)
            start = bank.samples.offset + offset
            jobs.append(_Job(path, wad.WAD_FILE, start, sample.size, 1, 0, 0, sample.sample_rate, to_end=True))
            report.count("bank samples")
    # Music, from MUSIC.SND.
    music_index = []
    for track in tables.music:
        keep = _trim(track.size // track.channels, track.interleave, track.blocks, track.last_block)
        path = claim("music", track.name.removeprefix("music/"), track.hash)
        interleave = track.interleave if track.channels > 1 else 0
        jobs.append(
            _Job(path, audio.MUSIC_FILE, track.offset, track.size, track.channels, interleave, keep, track.sample_rate)
        )
        music_index.append(
            {
                "hash": f"{track.hash:08x}",
                "name": track.name,
                "file": path,
                "rate": track.sample_rate,
                "channels": track.channels,
                "volume": track.volume,
            }
        )
        report.count("music tracks")
    _run_jobs(disc, jobs, output, workers)
    banks = {b.name: [f"{k:08x}" for k, _ in b.pairs] for b in tables.banks}
    output.write_json(KIND, "audio/sounds.json", {"sounds": sounds_index, "banks": banks})
    output.write_json(KIND, "audio/music.json", {"tracks": music_index})
    classes = [
        {"near": c.near, "far": c.far, "flags": c.flags, "priority": c.priority, "channels": c.channels}
        for c in tables.classes
    ]
    output.write_json(KIND, "audio/classes.json", {"classes": classes})
    return report
