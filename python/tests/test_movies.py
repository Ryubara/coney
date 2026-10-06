# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Bink header reader and `movies list`, on made-up headers built here byte by byte."""

import struct
from pathlib import Path

import pytest

from coney_tools import movies
from coney_tools.cli import main


def bink(
    frames: int = 3, tracks: tuple[tuple[int, int, int], ...] = ((48000, 0x7000, 0),), fps: tuple[int, int] = (30, 1)
) -> bytes:
    """A Bink header, frame index and frame bytes: `frames` frames of 8 bytes each, the first a key frame."""
    index_at = movies.HEADER + 12 * len(tracks)
    first = index_at + 4 * (frames + 1)
    size = first + 8 * frames
    header = struct.pack("<3sc10I", b"BIK", b"i", size - 8, frames, 8, frames, 640, 448, fps[0], fps[1], 0, len(tracks))
    header += b"".join(struct.pack("<I", 145920) for _ in tracks)
    header += b"".join(struct.pack("<HH", rate, flags) for rate, flags, _ in tracks)
    header += b"".join(struct.pack("<I", track_id) for _, _, track_id in tracks)
    index = [first + 8 * i for i in range(frames + 1)]
    index[0] |= 1
    return header + struct.pack(f"<{frames + 1}I", *index) + bytes(8 * frames)


def test_parse_reads_every_value() -> None:
    data = bink()
    header = movies.parse_header(data, len(data))
    assert (header.revision, header.width, header.height, header.frames, header.keyframes) == ("i", 640, 448, 3, 1)
    assert header.fps == 30.0 and header.seconds == pytest.approx(0.1)
    (track,) = header.audio
    assert (track.rate, track.channels, track.bits, track.codec, track.track_id) == (48000, 2, 16, "DCT", 0)


def test_mono_8bit_rdft_and_ntsc_rate() -> None:
    data = bink(frames=2, tracks=((22050, 0x0000, 5),), fps=(2997, 100))
    header = movies.parse_header(data)
    assert header.fps == pytest.approx(29.97)
    assert (header.audio[0].channels, header.audio[0].bits, header.audio[0].codec) == (1, 8, "RDFT")


def test_no_audio() -> None:
    assert movies.parse_header(bink(tracks=())).audio == ()


@pytest.mark.parametrize(
    "damage",
    [
        lambda d: b"XYZ" + d[3:],  # signature
        lambda d: d[:40],  # cut inside the header
        lambda d: d[: movies.HEADER + 20],  # cut inside the index
        lambda d: d[:8] + struct.pack("<I", 0) + d[12:],  # no frames
    ],
)
def test_damaged_headers_are_refused(damage) -> None:  # type: ignore[no-untyped-def]
    with pytest.raises(movies.MovieError):
        movies.parse_header(damage(bink()))


def test_size_mismatch_is_refused() -> None:
    data = bink()
    with pytest.raises(movies.MovieError, match="file has"):
        movies.parse_header(data, len(data) + 1)


def test_header_size_covers_the_index() -> None:
    data = bink(frames=5)
    assert movies.header_size(data[:64]) == movies.HEADER + 12 + 4 * 6


@pytest.fixture
def repo(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A fake checkout as the working directory (no game_dir)."""
    root = tmp_path / "checkout"
    root.mkdir()
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(root)
    return root


def test_list_reads_a_folder(tmp_path: Path, repo: Path, capsys: pytest.CaptureFixture[str]) -> None:
    folder = tmp_path / "disc" / "pss"  # any case, as on a mounted disc
    folder.mkdir(parents=True)
    for name in movies.MOVIE_NAMES:
        (folder / f"{name}.bik").write_bytes(bink(tracks=() if name == "PLOGO" else ((48000, 0x7000, 0),)))
    assert main(["movies", "list", str(tmp_path / "disc")]) == 0
    out = capsys.readouterr().out
    assert "movies: 16 read, 0 missing or failed" in out
    assert "PLOGO" in out and "none" in out and "48000 Hz 2 ch 16-bit DCT" in out


def test_list_reports_missing_and_bad(tmp_path: Path, repo: Path, capsys: pytest.CaptureFixture[str]) -> None:
    folder = tmp_path / "disc" / "PSS"
    folder.mkdir(parents=True)
    (folder / "LOGO.BIK").write_bytes(b"not a movie" * 8)
    assert main(["movies", "list", str(tmp_path / "disc")]) == 1
    out = capsys.readouterr().out
    assert "LOGO      LOGO: not a Bink file" in out and "PLOGO     missing" in out
    assert "movies: 0 read, 16 missing or failed" in out


def test_list_without_game_dir_is_exit_2(repo: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["movies", "list"]) == 2
    assert "game_dir" in capsys.readouterr().err
