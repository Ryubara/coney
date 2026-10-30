# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Coney Pack reader and writer and `coney-tools cpak`, on synthetic archives built here.

The layout is docs/guides/cpak-format.md; the C++ tests (tests/core/cpak_*_test.cpp) check the same sample archive's
hash, so the two implementations write the same bytes.
"""

import hashlib
import io
import struct
from pathlib import Path

import pytest

from coney_tools import cpak
from coney_tools.cli import main

#: The format page's sample: three synthetic entries, one empty, 16-byte alignment (docs/guides/cpak-format.md#example).
SAMPLE = [("readme", b"Coney"), ("art/logo", bytes([0, 1, 2])), ("empty", b"")]
SAMPLE_SHA256 = "188b0b7a40e4125e66ddf7ae6b81c61436070adc0e2406d5947b85441df6c486"


def hand_built(entries: list[tuple[str, int, bytes]], alignment: int = 16, version: int = 1) -> bytes:
    """An archive of (name, offset, bytes) entries in the order given, laid out by hand from the format page."""
    names = b"".join(name.encode() for name, _, _ in entries)
    index_size = 64 * len(entries) + len(names)
    end = max([64 + index_size] + [offset + len(data) for _, offset, data in entries])
    out = bytearray(end)
    head = b"CPAK" + struct.pack("<HHIIIIQ", version, 0, alignment, len(entries), index_size, 0, end)
    records = bytearray()
    at = 0
    for name, offset, data in entries:
        records += struct.pack("<QQQIHBB", offset, len(data), len(data), at, len(name), 0, 0)
        records += hashlib.sha256(data).digest()
        at += len(name)
        out[offset : offset + len(data)] = data
    index = bytes(records) + names
    out[0:64] = head + hashlib.sha256(head + index).digest()
    out[64 : 64 + index_size] = index
    return bytes(out)


def read(data: bytes) -> tuple[cpak.Header, list[cpak.Entry]]:
    """The header and entries of `data`."""
    return cpak.read_index(io.BytesIO(data), len(data))


def test_the_writer_makes_the_format_pages_sample_byte_for_byte() -> None:
    data = cpak.write(SAMPLE, alignment=16)
    assert len(data) == 309
    assert hashlib.sha256(data).hexdigest() == SAMPLE_SHA256


def test_a_written_archive_reads_back_sorted_aligned_and_verified() -> None:
    data = cpak.write(SAMPLE, alignment=16)
    header, entries = read(data)
    assert (header.version, header.alignment, header.archive_size) == (1, 16, len(data))
    assert [entry.name for entry in entries] == ["art/logo", "empty", "readme"]
    for entry, (_, payload) in zip(entries, sorted(SAMPLE, key=lambda item: item[0].encode()), strict=True):
        assert entry.offset % 16 == 0
        assert data[entry.offset : entry.offset + entry.stored_size] == payload
        assert entry.sha256 == hashlib.sha256(payload).digest()
        cpak.verify_entry(io.BytesIO(data), entry)


def test_a_hand_built_archive_is_read_as_the_page_lays_it_out() -> None:
    data = hand_built([("a/one", 208, b"first"), ("b/two", 224, b"second!")])
    header, entries = read(data)
    assert (header.entry_count, header.index_size) == (2, 138)
    assert [(e.name, e.offset, e.size, e.compression) for e in entries] == [
        ("a/one", 208, 5, 0),
        ("b/two", 224, 7, 0),
    ]


@pytest.mark.parametrize("at", [200, 64 + 8, 8, 0x30])
def test_a_damaged_index_is_refused(at: int) -> None:
    data = bytearray(hand_built([("a/one", 208, b"first"), ("b/two", 224, b"second!")]))
    data[at] ^= 1
    with pytest.raises(cpak.CpakError):
        read(bytes(data))


@pytest.mark.parametrize(
    "entries",
    [
        [("b/two", 208, b"first"), ("a/one", 224, b"second!")],  # out of name order
        [("a/one", 209, b"first")],  # off the alignment
        [("a/../b", 208, b"first")],  # an ill-formed name
        [("a/one", 208, b"0123456789abcdefXY"), ("b/two", 224, b"second!")],  # overlapping
    ],
)
def test_records_that_break_the_rules_are_refused(entries: list[tuple[str, int, bytes]]) -> None:
    with pytest.raises(cpak.CpakError):
        read(hand_built(entries))


def test_a_wrong_version_or_size_is_refused() -> None:
    with pytest.raises(cpak.CpakError, match="version"):
        read(hand_built([("a", 208, b"x")], version=2))
    data = hand_built([("a", 208, b"x")])
    with pytest.raises(cpak.CpakError):
        cpak.read_index(io.BytesIO(data), len(data) - 1)


def test_a_damaged_entry_fails_verification() -> None:
    data = bytearray(cpak.write(SAMPLE, alignment=16))
    _, entries = read(bytes(data))
    readme = next(entry for entry in entries if entry.name == "readme")
    data[readme.offset] ^= 1
    with pytest.raises(cpak.CpakError, match="readme"):
        cpak.verify_entry(io.BytesIO(bytes(data)), readme)


@pytest.mark.parametrize("name", ["", "/a", "a/", "a//b", "a/./b", "a/../b", "a\\b", "c:x", "a\0b", "x" * 1025])
def test_bad_names_are_refused(name: str) -> None:
    with pytest.raises(cpak.CpakError):
        cpak.check_name(name)
    with pytest.raises(cpak.CpakError):
        cpak.write([(name, b"")])


def rehashed(data: bytearray) -> bytes:
    """`data` with its index hash written again over its header's first 32 bytes and its index, as the page says."""
    index_size = struct.unpack_from("<I", data, 0x10)[0]
    data[0x20:0x40] = hashlib.sha256(bytes(data[0:32]) + bytes(data[64 : 64 + index_size])).digest()
    return bytes(data)


@pytest.mark.parametrize("count", [3, 0x0400_0000, 0xFFFF_FFFF])
def test_an_entry_count_larger_than_the_index_holds_is_refused(count: int) -> None:
    data = bytearray(hand_built([("a/one", 208, b"first"), ("b/two", 224, b"second!")]))
    struct.pack_into("<I", data, 0x0C, count)
    with pytest.raises(cpak.CpakError, match="smaller than its records"):
        read(rehashed(data))


def test_no_bytes_at_all_are_refused() -> None:
    with pytest.raises(cpak.CpakError):
        read(b"")


def test_a_name_that_is_not_utf8_is_refused_even_when_the_hash_matches() -> None:
    data = bytearray(hand_built([("a/one", 208, b"first")]))
    data[64 + 64 + 2] = 0xFF  # "a/one" becomes "a/\xffne" in the name table
    with pytest.raises(cpak.CpakError, match="UTF-8"):
        read(rehashed(data))


def test_names_differing_only_by_case_are_refused() -> None:
    with pytest.raises(cpak.CpakError, match="case"):
        cpak.write([("Textures/HUD", b""), ("textures/hud", b"")])


def test_cpak_list_prints_one_line_per_entry(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    archive = tmp_path / "sample.cpak"
    archive.write_bytes(cpak.write(SAMPLE, alignment=16))
    assert main(["cpak", "list", str(archive)]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert [line.split()[-1] for line in lines[1:4]] == ["art/logo", "empty", "readme"]
    assert "3 entries" in lines[-1]


def test_cpak_show_prints_the_header_or_one_entry(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    archive = tmp_path / "sample.cpak"
    archive.write_bytes(cpak.write(SAMPLE, alignment=16))
    assert main(["cpak", "show", str(archive)]) == 0
    out = capsys.readouterr().out
    assert "alignment: 16" in out and "entries: 3" in out and "archive size: 309" in out
    assert main(["cpak", "show", str(archive), "readme"]) == 0
    out = capsys.readouterr().out
    assert f"sha256: {hashlib.sha256(b'Coney').hexdigest()}" in out and "compression: none" in out
    assert main(["cpak", "show", str(archive), "missing"]) == 2


def test_cpak_verify_passes_a_good_archive_and_fails_a_damaged_one(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    good = tmp_path / "good.cpak"
    good.write_bytes(cpak.write(SAMPLE, alignment=16))
    bad = tmp_path / "bad.cpak"
    data = bytearray(good.read_bytes())
    data[-1] ^= 1  # the last byte of readme
    bad.write_bytes(bytes(data))
    assert main(["cpak", "verify", str(good)]) == 0
    assert "3 entries verified" in capsys.readouterr().out
    assert main(["cpak", "verify", str(good), str(bad)]) == 1
    out = capsys.readouterr().out
    assert "readme" in out and "1 failed" in out


def test_cpak_list_refuses_a_file_that_is_no_archive(tmp_path: Path) -> None:
    other = tmp_path / "other.bin"
    other.write_bytes(b"not an archive" * 10)
    assert main(["cpak", "list", str(other)]) == 2
    assert main(["cpak", "verify", str(other)]) == 1
