# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools extract`, on synthetic data only: made-up textures, ADPCM frames, WAD entries and names."""

import json
import random
import struct
import zlib
from pathlib import Path

import numpy as np
import pytest

from coney_tools import adpcm, audio, extract_audio, extract_raw, extract_wad, ps2tex, rw, wad_kinds
from coney_tools.disc import SECTOR, Disc
from coney_tools.extract_output import Output, safe_part, safe_path
from coney_tools.wad import WadEntry, parse_dir

STAMP = 0x1C02000A


def section(kind: int, body: bytes, stamp: int = STAMP) -> bytes:
    """One RenderWare section."""
    return struct.pack("<III", kind, len(body), stamp) + body


def gs_packet(width: int, height: int, image: bytes) -> bytes:
    """A GS upload packet as the textures carry them: GIF tag, TRXPOS, TRXREG, TRXDIR, image tag, image."""
    qwords = len(image) // 16
    head = struct.pack("<QQ", 3 | (1 << 60), 0xE)  # NLOOP 3, NREG 1, A+D
    head += struct.pack("<QQ", 0, 0x51) + struct.pack("<IIQ", width, height, 0x52) + struct.pack("<QQ", 0, 0x53)
    head += struct.pack("<QQ", qwords | (2 << 58), 0)
    return head + image


def swizzle_8bit(plane: np.ndarray) -> bytes:
    """Lay an 8-bit image out in the order the GS stores it when sent as 32-bit pixels (the inverse of decoding)."""
    height, width = plane.shape
    table = ps2tex.unswizzle_table(width, height)
    out = np.zeros(width * height, dtype=np.uint8)
    out[table] = plane.ravel()
    return out.tobytes()


def native_texture(name: str, plane: np.ndarray, colours: np.ndarray) -> bytes:
    """A texture native section holding an 8-bit swizzled texture and a 256-colour palette in CSM1 order."""
    height, width = plane.shape
    pixels = gs_packet(width // 2, height // 2, swizzle_8bit(plane))
    stored = colours.copy()
    index = np.arange(256)
    group = index & 0x18
    stored[np.where((group == 0x08) | (group == 0x10), index ^ 0x18, index)] = colours
    palette = gs_packet(16, 16, stored.tobytes())
    header = struct.pack("<IIIHhQIIQQIIII", width, height, 8, 0x2504, 2, 0, 0, 0, 0, 0, len(pixels), len(palette), 0, 0)
    raster = section(rw.STRUCT, section(rw.STRUCT, header) + section(rw.STRUCT, pixels + palette))
    body = section(rw.STRUCT, b"PS2\0" + struct.pack("<I", 0x1106))
    body += section(rw.STRING, name.encode() + b"\0" * (4 - len(name) % 4))
    body += section(rw.STRING, b"\0\0\0\0") + raster + section(rw.EXTENSION, b"")
    return section(rw.TEXTURE_NATIVE, body)


def dictionary(*textures: bytes) -> bytes:
    """A texture dictionary section."""
    return section(rw.TEX_DICTIONARY, section(rw.STRUCT, struct.pack("<HH", len(textures), 6)) + b"".join(textures))


def made_up_texture() -> tuple[np.ndarray, np.ndarray, bytes]:
    """A 32 x 8 texture of random indexes, a palette whose alpha runs 0-128, and its dictionary."""
    rng = np.random.default_rng(7)
    plane = rng.integers(0, 256, size=(8, 32), dtype=np.uint8)
    colours = rng.integers(0, 256, size=(256, 4), dtype=np.uint8)
    colours[:, 3] = np.arange(256) % 129
    return plane, colours, dictionary(native_texture("made_up", plane, colours))


def test_a_swizzled_texture_decodes_to_its_image() -> None:
    plane, colours, data = made_up_texture()
    (texture,) = ps2tex.parse_dictionary(data)
    assert (texture.name, texture.header.width, texture.header.height, texture.depth) == ("made_up", 32, 8, 8)
    assert (texture.filter_mode, texture.address_u, texture.address_v) == (6, 1, 1)
    rgba = ps2tex.decode(texture)
    expected = colours[plane].copy()
    expected[..., 3] = np.minimum(expected[..., 3].astype(int) * 255 // 128, 255)
    assert rgba.shape == (8, 32, 4)
    assert np.array_equal(rgba, expected)


def test_the_unswizzle_table_is_a_permutation() -> None:
    for width, height in ((16, 4), (64, 32), (512, 8)):
        table = ps2tex.unswizzle_table(width, height)
        assert sorted(table.tolist()) == list(range(width * height))


def test_a_texture_from_another_platform_is_refused() -> None:
    body = section(rw.STRUCT, b"D3D9" + bytes(4)) + section(rw.STRING, b"x\0\0\0") + section(rw.STRING, bytes(4))
    body += section(rw.STRUCT, b"")
    with pytest.raises(ps2tex.TextureError):
        ps2tex.parse_dictionary(dictionary(section(rw.TEXTURE_NATIVE, body)))


def random_stream(rng: random.Random, frames: int, end: bool = False) -> bytes:
    """Random but valid ADPCM frames (predictors 0-4, shifts 0-12)."""
    data = b""
    for index in range(frames):
        flags = audio.FLAG_END if end and index == frames - 1 else 0
        header = rng.randrange(5) << 4 | rng.randrange(13)
        data += bytes((header, flags)) + bytes(rng.randrange(256) for _ in range(14))
    return data


def test_the_batched_decoder_matches_the_plain_one() -> None:
    rng = random.Random(3)
    streams = [random_stream(rng, n) for n in (1, 5, 40, 2, 0, 17)]
    decoded = adpcm.decode_batch(streams)
    for stream, samples in zip(streams, decoded, strict=True):
        assert samples.tolist() == audio.AdpcmDecoder().decode(stream)


def test_a_bank_sample_is_cut_after_its_end_frame() -> None:
    rng = random.Random(4)
    data = random_stream(rng, 3, end=True) + random_stream(rng, 2)
    assert len(adpcm.cut_at_end(data)) == 3 * audio.FRAME


def test_batches_group_streams_of_similar_length() -> None:
    assert list(adpcm.batches([5, 100, 7, 90], 2)) == [[1, 3], [2, 0]]


def test_sound_names_come_from_strings_patterns_and_their_numeric_neighbours() -> None:
    names = ["vags/speeches/l1/l1_t2_003", "vags/speeches/l1/l1_t2_004", "vags/character/voices/7/taunt_02", "x"]
    hashes = {zlib.crc32(n.encode()) for n in names}
    found = extract_audio.recover_sound_names(hashes, ["l1_t2_003", "x"], ["taunt"])
    assert sorted(found.values()) == sorted(names)


def test_safe_names_avoid_device_names_and_odd_characters() -> None:
    assert safe_part("con.png") == "_con.png"
    assert safe_part("a:b*c") == "a_b_c"
    assert safe_path("../x\\y/./z") == "x/y/z"


def test_output_paths_are_unique_in_any_letter_case(tmp_path: Path) -> None:
    output = Output(tmp_path / "out")
    output.start("t")
    assert output.write("t", "a/Name.png", b"1") == "a/Name.png"
    assert output.write("t", "a/name.png", b"2") == "a/name~2.png"
    output.save_manifest({"disc": "made up"})
    manifest = json.loads((tmp_path / "out" / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["types"]["t"]["count"] == 2
    again = Output(tmp_path / "out")
    again.start("t")  # a second run of the type removes the first run's files
    assert not (tmp_path / "out" / "a" / "Name.png").exists()


def resource(key: int, chunks: list[tuple[int, bytes]]) -> bytes:
    """A standalone resource of chunks (each padded to 16 bytes)."""
    body = b""
    size = 0
    for kind, data in chunks:
        data += b"\0" * (-len(data) % 16)
        body += struct.pack("<IIII", kind, len(data), 0, 0) + data
        size += len(data)
    return struct.pack("<IIII", len(chunks), size, 0, key) + body


def make_disc(root: Path, entries: list[tuple[int, bytes]]) -> Disc:
    """A disc folder whose WAD holds `entries` (hash, bytes), 2048-aligned."""
    wad_bytes = b""
    table = b""
    for key, data in entries:
        table += struct.pack("<III", len(wad_bytes), len(data), key)
        wad_bytes += data + b"\0" * (-len(data) % SECTOR)
    root.mkdir()
    (root / "WARRIORS.DIR").write_bytes(struct.pack("<I12x", len(entries)) + table)
    (root / "WARRIORS.WAD").write_bytes(wad_bytes)
    return Disc(root)


def test_the_wad_pass_classifies_and_writes_each_kind(tmp_path: Path) -> None:
    _, _, textures = made_up_texture()
    lua = bytes.fromhex("1b4c7561400104040420060908") + b"made up"
    scene = struct.pack("<II", 32, 0) + b"made_up_scene\0\0\0" + bytes(8)
    entries = [
        (0x1111, resource(0xAAAA, [(0x2A, textures)])),
        (0x2222, lua),
        (0x3333, scene),
        (0x4444, resource(0xBBBB, [(0x47, b"not really a clump")])),
    ]
    disc = make_disc(tmp_path / "disc", entries)
    output = Output(tmp_path / "out")
    stages: list[extract_wad.Stage] = [
        extract_wad.IndexStage(output),
        extract_wad.ScriptsStage(output),
        extract_wad.TexturesStage(output),
        extract_raw.RawStage(output),
    ]
    records = parse_dir((tmp_path / "disc" / "WARRIORS.DIR").read_bytes())
    extract_wad.walk(disc, records, {0x2222: "made_up.lua"}, lambda key: None, stages)
    files = {kind: sorted(record) for kind, record in output.files.items()}
    assert files["scripts"] == ["scripts/index.json", "scripts/made_up.lua"]
    assert files["textures"] == ["textures/0000aaaa/made_up.png", "textures/index.json"]
    assert "raw/scene/00003333.scn" in files["raw"]
    assert "raw/resources/model/0000bbbb.res.json" in files["raw"]
    index = json.loads((tmp_path / "out" / "index" / "wad.json").read_text(encoding="utf-8"))
    assert [e["kind"] for e in index["entries"]] == ["resource", "lua", "scene", "resource"]
    assert index["resources"]["0000aaaa"]["shape"] == "textures"


def test_a_bank_index_marks_the_entry_before_it_as_samples() -> None:
    kinds = [wad_kinds.UNKNOWN, wad_kinds.BANK_INDEX, wad_kinds.UNKNOWN]
    assert wad_kinds.mark_bank_samples(kinds) == [wad_kinds.BANK_SAMPLES, wad_kinds.BANK_INDEX, wad_kinds.UNKNOWN]


def test_kinds_by_their_structure() -> None:
    assert wad_kinds.classify(struct.pack("<III", 100, 120, 1) + struct.pack("<II", 10, 20)).kind == "world-manifest"
    assert wad_kinds.classify(struct.pack("<IIII", 0x1, 0, 0x2, 0x10) + bytes(16)).kind == "bank-index"
    assert wad_kinds.classify(b"2\r\n{a {1 2 3}}\r\n{b {4 5 6}}\r\n\0\0").kind == "object-list"
    assert wad_kinds.classify(b"METRICS1 ...").kind == "font-metrics"
    assert wad_kinds.classify(b"\0\0\x01\0icon").kind == "icon"
    legacy = section(rw.TEX_DICTIONARY, section(rw.STRUCT, bytes(4), 0x1803FFFF), 0x1803FFFF)
    assert wad_kinds.classify(legacy).kind == "legacy-rw"
    _, _, textures = made_up_texture()
    world = struct.pack("<I", 1) + textures + section(rw.WORLD, b"")
    assert wad_kinds.classify(world).kind == "world-stream"
    parts = struct.pack("<IIII", 1, 0, 0, 5) + textures + struct.pack("<II", 1, 3) + section(rw.ATOMIC, b"")
    assert wad_kinds.classify(parts).kind == "sector-parts"


def test_a_raw_entry_is_described() -> None:
    manifest = struct.pack("<III", 100, 120, 1) + struct.pack("<II", 10, 20)
    entry = extract_wad.Entry(WadEntry(0, 0, len(manifest), 7), manifest, "world-manifest", None, None)
    assert extract_raw.describe(entry) == {"world_size": 100, "world_heap": 120, "parts": [[10, 20]]}
