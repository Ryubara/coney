# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the audio tables, the PS2 ADPCM codec and the `audio` commands, on synthetic data with made-up names."""

import math
import struct
import wave
import zlib
from pathlib import Path

import pytest

from coney_tools import audio
from coney_tools.cli import main
from coney_tools.disc import SECTOR, Disc

# --- the codec ---


def frame(header: int, flags: int, nibbles: list[int]) -> bytes:
    """One ADPCM frame from 28 nibbles (0-15), low nibble first."""
    return bytes((header, flags)) + bytes(nibbles[i] | nibbles[i + 1] << 4 for i in range(0, 28, 2))


def test_shift_twelve_without_prediction_gives_the_nibbles() -> None:
    nibbles = [1, 2, 7, 8, 15, 0] + [0] * 22  # 8 is -8 and 15 is -1 as signed nibbles
    assert audio.AdpcmDecoder().frame(frame(0x0C, 0, nibbles))[:6] == [1, 2, 7, -8, -1, 0]


def test_shift_zero_puts_the_nibble_in_the_top_bits() -> None:
    assert audio.AdpcmDecoder().frame(frame(0x00, 0, [1, 15] + [0] * 26))[:2] == [4096, -4096]


def test_predictor_one_decays_by_sixty_sixty_fourths() -> None:
    samples = audio.AdpcmDecoder().frame(frame(0x10, 0, [7] + [0] * 27))  # shift 0, filter (60, 0)
    assert samples[0] == 7 << 12
    assert samples[1] == (samples[0] * 60 + 32) >> 6
    assert samples[2] == (samples[1] * 60 + 32) >> 6


def test_the_decoder_keeps_its_history_across_frames() -> None:
    decoder = audio.AdpcmDecoder()
    first = decoder.frame(frame(0x00, 0, [3] * 28))
    assert decoder.frame(frame(0x1C, 0, [0] * 28))[0] == (first[-1] * 60 + 32) >> 6


def test_decode_stops_at_the_end_flag_only_when_asked() -> None:
    data = frame(0x0C, 0, [1] * 28) + frame(0x0C, audio.FLAG_END, [1] * 28) + frame(0x0C, 7, [0] * 28)
    assert len(audio.AdpcmDecoder().decode(data, stop_at_end=True)) == 56
    assert len(audio.AdpcmDecoder().decode(data)) == 84


def test_an_encoded_sine_decodes_close_to_itself() -> None:
    sine = [round(9000 * math.sin(i * 0.2)) for i in range(28 * 8)]
    data = b"".join(audio.encode_frame(sine[i : i + 28]) for i in range(0, len(sine), 28))
    decoded = audio.AdpcmDecoder().decode(data)
    assert max(abs(a - b) for a, b in zip(sine, decoded, strict=True)) <= 1024  # half a step at shift 1


def test_deinterleave_splits_blocks_and_a_short_last_block() -> None:
    data = b"L" * 32 + b"R" * 32 + b"l" * 16 + b"r" * 16
    assert audio.deinterleave(data, 2, 32) == [b"L" * 32 + b"l" * 16, b"R" * 32 + b"r" * 16]
    assert audio.deinterleave(b"abc", 1, 32) == [b"abc"]


# --- the tables ---


def sound_record(size: int, offset: int, key: int, pitch: int, volume: int, rate: int, klass: int) -> bytes:
    """One 16-byte sound list record."""
    return struct.pack("<III4B", size, offset, key, pitch, volume, rate, klass)


def music_record(name: str, rate: int, offset: int, interleave: int, blocks: int, last: int) -> bytes:
    """One 104-byte music list record."""
    head = struct.pack("<f9I", 1.0, rate, 64, 32, offset, 2, interleave, blocks, last, zlib.crc32(name.encode()))
    return head + name.encode().ljust(64, b"\0")


def test_sound_list_records_and_rates() -> None:
    data = struct.pack("<I", 2) + sound_record(32, 2048, 5, 10, 120, 36, 1) + sound_record(16, 0, 9, 0, 100, 34, 0)
    first, second = audio.parse_sound_list(data)
    assert (first.size, first.offset, first.hash, first.pitch_variation, first.volume) == (32, 2048, 5, 10, 120)
    assert (first.sample_rate, second.sample_rate, first.sound_class) == (22500, 22050, 1)
    with pytest.raises(ValueError, match="do not fit"):
        audio.parse_sound_list(struct.pack("<I", 3) + sound_record(0, 0, 0, 0, 0, 0, 0))


def test_sound_classes_flags() -> None:
    near, positional_loop, voice = audio.parse_sound_classes(
        bytes([0, 0, 0x00, 3, 1, 5, 20, 0x07, 6, 1, 1, 10, 0x1C, 8, 1]) + b"\0\0"
    )
    assert (near.near, near.far, near.positional, near.streamed) == (0, 0, False, False)
    assert (positional_loop.far, positional_loop.looped, positional_loop.positional) == (100, True, True)
    assert (voice.streamed, voice.positional, voice.looped, voice.priority) == (True, True, False, 8)


def test_stereo_table_and_bank_index() -> None:
    stereo = audio.parse_stereo_table(struct.pack("<I12x4I", 1, 7, 0x8000, 0x100, 3))
    assert stereo == [audio.StereoInfo(7, 0x8000, 0x100, 3)]
    assert audio.parse_bank_index(struct.pack("<6I", 4, 0, 5, 48, 0, 0)) == [(4, 0), (5, 48)]


def test_music_list() -> None:
    (track,) = audio.parse_music_list(struct.pack("<I", 1) + music_record("music/made_up", 30000, 4096, 64, 3, 32))
    assert (track.name, track.sample_rate, track.offset, track.channels, track.size) == (
        "music/made_up",
        30000,
        4096,
        2,
        384,
    )
    assert track.hash == zlib.crc32(b"music/made_up")


# --- the commands, on a made-up disc ---


def chunk(kind: int, data: bytes) -> bytes:
    """A chunk header and its data, padded to 16 bytes."""
    data = data.ljust(-(-len(data) // 16) * 16, b"\0")
    return struct.pack("<IIII", kind, len(data), 0, 0) + data


def tone(frames: int, value: int) -> bytes:
    """ADPCM frames of one repeated nibble at shift 12 (a constant small value)."""
    return b"".join(frame(0x0C, 0, [value] * 28) for _ in range(frames))


def make_disc(root: Path) -> Path:
    """A disc folder: a WAD with warriors.glr and one bank, and IOP/ with a mono, a stereo and a music stream."""
    mono, stereo, banked = "made/mono", "made/stereo", "made/banked"
    sounds = sorted(
        [
            (zlib.crc32(mono.encode()), sound_record(32, 0, zlib.crc32(mono.encode()), 0, 100, 36, 1)),
            (zlib.crc32(stereo.encode()), sound_record(64, SECTOR, zlib.crc32(stereo.encode()), 0, 100, 49, 2)),
            (zlib.crc32(banked.encode()), sound_record(32, 0, zlib.crc32(banked.encode()), 0, 100, 36, 0)),
        ]
    )
    sound_list = struct.pack("<I", len(sounds)) + b"".join(r for _, r in sounds)
    classes = bytes([0, 0, 0x00, 3, 1, 1, 10, 0x06, 6, 1, 1, 10, 0x06, 6, 2])
    stereo_table = struct.pack("<I12x4I", 1, zlib.crc32(stereo.encode()), 32, 32, 2)
    music = struct.pack("<I", 1) + music_record("music/made_up", 30000, 0, 32, 2, 32)
    body = b"".join(chunk(k, d) for k, d in ((0x31, music), (0x48, classes), (0x49, stereo_table), (0x29, sound_list)))
    glr = struct.pack("<IIII", 4, len(body) - 4 * 16, 0, 0x1234) + body
    msb = tone(1, 2) + frame(0x0C, 1, [2] * 28)
    msd = struct.pack("<4I", zlib.crc32(banked.encode()), 0, 0, 0)
    files = [("warriors.glr", glr), ("made.msb", msb), ("made.msd", msd)]
    wad_bytes, entries = b"", b""
    for name, data in files:
        entries += struct.pack("<III", len(wad_bytes), len(data), zlib.crc32(("./ee_files/" + name).encode()))
        wad_bytes += data + b"\0" * (-len(data) % SECTOR)
    root.mkdir()
    (root / "WARRIORS.DIR").write_bytes(struct.pack("<I12x", len(files)) + entries)
    (root / "WARRIORS.WAD").write_bytes(wad_bytes)
    (root / "IOP").mkdir()
    bfw = tone(2, 1).ljust(SECTOR, b"\0") + tone(2, 3) + tone(2, 5) + tone(2, 3) + tone(2, 5)
    (root / "IOP" / "BFW.SND").write_bytes(bfw)
    (root / "IOP" / "MUSIC.SND").write_bytes(tone(2, 4) + tone(2, 6) + tone(2, 4) + tone(2, 6))
    return root


def read_wav(path: Path) -> tuple[int, int, list[int]]:
    """Channels, rate and the interleaved samples of a WAV file."""
    with wave.open(str(path), "rb") as wav:
        raw = wav.readframes(wav.getnframes())
        return wav.getnchannels(), wav.getframerate(), list(struct.unpack(f"<{len(raw) // 2}h", raw))


def test_the_folder_disc_sees_files_in_folders(tmp_path: Path) -> None:
    disc = Disc(make_disc(tmp_path / "d"))
    assert disc.has("IOP/BFW.SND") and disc.has("iop\\music.snd;1")


def test_info_counts_each_kind(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["audio", "info", str(make_disc(tmp_path / "d"))]) == 0
    out = capsys.readouterr().out
    assert "sound list: 3 records" in out and "stream  1" in out and "stereo  1" in out and "bank    1" in out
    assert "music list: 1 tracks" in out and "banks: 1" in out


def test_list_prints_one_line_per_item(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    disc = str(make_disc(tmp_path / "d"))
    assert main(["audio", "list", "sounds", disc]) == 0
    assert len(capsys.readouterr().out.splitlines()) == 3
    assert main(["audio", "list", "music", disc]) == 0
    assert "music/made_up" in capsys.readouterr().out


def test_decode_mono_stereo_music_and_bank(tmp_path: Path) -> None:
    disc = str(make_disc(tmp_path / "d"))
    out = tmp_path / "out"
    assert main(["audio", "decode", disc, "made/mono", str(out / "mono.wav")]) == 0
    assert read_wav(out / "mono.wav") == (1, 22500, [1] * 28 + [1] * 28)
    assert main(["audio", "decode", disc, "made/stereo", str(out / "stereo.wav")]) == 0
    channels, rate, samples = read_wav(out / "stereo.wav")
    assert (channels, rate) == (2, 32250) and samples[0:2] == [3, 5] and len(samples) == 2 * 2 * 56
    assert main(["audio", "decode", disc, "music/made_up", str(out / "music.wav")]) == 0
    assert read_wav(out / "music.wav")[2][0:2] == [4, 6]
    assert main(["audio", "decode", disc, "made/banked", str(out / "bank.wav")]) == 0
    assert read_wav(out / "bank.wav")[2] == [2] * 56


def test_decode_refuses_unknown_names(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["audio", "decode", str(make_disc(tmp_path / "d")), "made/none", str(tmp_path / "x.wav")]) == 2
    assert "not in the sound list" in capsys.readouterr().err
