# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools xbox compare` (coney_tools.xbox_compare): each PS2 chunk and entry looked up on the Xbox.

The archives are tiny synthetic ones built byte by byte (the chunk container's helpers from test_xbox.py); every hash
and byte is made up.
"""

from __future__ import annotations

import struct
from pathlib import Path

import pytest
from test_xbox import chunk, index_bytes, pack, resource

from coney_tools import xbox, xbox_compare
from coney_tools.cli import main
from coney_tools.disc import SECTOR

LEVEL = 0x10101010  # a level resource on both discs
GLOBAL = 0x20202020  # a resource only on the PS2
LUA = b"\x1bLua\x40\x01\x04\x04\x04\x20\x06\x09\x08" + b"\0made-up chunk\0"
SCENE = struct.pack("<II", 96, 0) + b"intro".ljust(16, b"\0") + b"\0" * 72


def ps2_entries() -> list[bytes]:
    """The PS2 side: a pack holding a level resource with five chunks, a resource the Xbox lacks, a script, a scene."""
    level = resource(
        LEVEL,
        chunk(0x40, b"paths, the same"),  # same
        chunk(0x06, b"lists AAAA"),  # same size, other bytes
        chunk(0x03, b"header, 32 bytes on the PS2 side"),  # other size
        chunk(0x51, b"subtitles"),  # no such chunk on the Xbox
        chunk(0x40, b"paths, the same"),  # a repeat: counted once
    )
    return [pack(level), resource(GLOBAL, chunk(0x44, b"list")), LUA, SCENE]


def xbox_entries() -> list[bytes]:
    """The Xbox side: the level with the paths, other lists, a shorter header and no subtitles; the same scene, another
    script, and a resource the PS2 lacks."""
    level = resource(LEVEL, chunk(0x40, b"paths, the same"), chunk(0x06, b"lists BBBB"), chunk(0x03, b"header"))
    return [level, LUA + b"more", SCENE, resource(0x30303030, chunk(0x17, b"x"))]


def test_compare_sorts_each_chunk_into_its_column() -> None:
    result = xbox_compare.compare(xbox_compare.read_side(ps2_entries()), xbox_compare.read_side(xbox_entries()))
    assert result.chunk_rows[0x40] == {"same": 1}
    assert result.chunk_rows[0x06] == {"same size": 1}
    assert result.chunk_rows[0x03] == {"other size": 1}
    assert result.chunk_rows[0x51] == {"no such chunk": 1}
    assert result.chunk_rows[0x44] == {"no such resource": 1}
    assert result.entry_rows[xbox.LUA] == (1, 1, 0)
    assert result.entry_rows[xbox.SCENE] == (1, 1, 1)
    assert (result.ps2_only_resources, result.xbox_only_resources) == (1, 1)


def test_report_is_counts_by_type_and_kind() -> None:
    lines = xbox_compare.report(
        xbox_compare.compare(xbox_compare.read_side(ps2_entries()), xbox_compare.read_side(xbox_entries()))
    )
    text = "\n".join(lines)
    assert "0x40 PathData" in text and "0x51 Subtitles" in text
    row = next(line for line in lines if line.strip().startswith("all"))
    assert row.split()[1:] == ["5", "1", "1", "1", "1", "1"]
    assert "resources only on the PS2: 1; only on the Xbox: 1" in text


def test_unclassified_ps2_entries_count_as_other() -> None:
    side = xbox_compare.read_side([b"\x99" * 40])
    assert side.entry_count == {xbox_compare.OTHER: 1}


def _ps2_disc(root: Path, items: list[bytes]) -> Path:
    """A PS2 disc folder: WARRIORS.DIR and WARRIORS.WAD over `items`."""
    directory = struct.pack("<I12x", len(items))
    wad = b""
    for n, data in enumerate(items):
        directory += struct.pack("<III", len(wad), len(data), n)
        wad += data + b"\0" * (-len(data) % SECTOR)
    root.mkdir()
    (root / "WARRIORS.DIR").write_bytes(directory)
    (root / "WARRIORS.WAD").write_bytes(wad)
    return root


def _xbox_disc(root: Path, items: list[bytes]) -> Path:
    """An Xbox disc folder: XBoxWad.idx over one volume holding `items`."""
    volume, rows = b"", []
    for n, data in enumerate(items):
        rows.append((0, len(volume), len(data), 0xABC00000 + n))
        volume += data + b"\0" * (-len(data) % 2048)
    root.mkdir()
    (root / "XBoxWad.idx").write_bytes(index_bytes(["Main.wad0"], rows))
    (root / "Main.wad0").write_bytes(volume)
    return root


def test_the_command_reads_both_discs(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    ps2 = _ps2_disc(tmp_path / "ps2", ps2_entries())
    other = _xbox_disc(tmp_path / "xbox", xbox_entries())
    assert main(["xbox", "compare", str(ps2), str(other)]) == 0
    out = capsys.readouterr().out
    assert "chunks by type" in out and "resources only on the PS2: 1; only on the Xbox: 1" in out
    assert "made-up" not in out
