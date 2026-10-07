# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Xbox disc as an asset source: resource images, DXT decoding, texture matching, the replacement rules
and the extract hooks, on a made-up Xbox disc (an extracted folder) built here byte by byte."""

from __future__ import annotations

import hashlib
import io
import struct
import zlib
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

from coney_tools import extract_xbox, movies, xbox_gfx, xbox_index, xbox_match, xbox_texture_map
from coney_tools.config import ConfigError
from coney_tools.extract_output import Output, Report, png_bytes
from coney_tools.extract_wad import _Dictionary
from coney_tools.xbox_source import XboxSource, ps2_world_level

RESOURCE_HASH = 0x5EED0001  # a texture dictionary resource on both discs
RED, GREEN, BLUE, GREY = (255, 0, 0), (0, 255, 0), (0, 0, 255), (131, 133, 131)  # exact in RGB565


def rgb565(colour: tuple[int, int, int]) -> int:
    """A colour as RGB565 (each channel rounded down)."""
    red, green, blue = colour
    return (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3


def solid_dxt1(colours: list[list[tuple[int, int, int]]]) -> bytes:
    """A DXT1 level whose 4 x 4 blocks are each one colour (rows of blocks, top to bottom)."""
    return b"".join(struct.pack("<HHI", rgb565(c), rgb565(c), 0) for row in colours for c in row)


def blocks_image(colours: list[list[tuple[int, int, int]]], scale: int = 4) -> np.ndarray:
    """The RGBA picture of `solid_dxt1(colours)` at `scale` pixels a block."""
    rows = [np.concatenate([np.full((scale, scale, 4), (*c, 255), np.uint8) for c in row], axis=1) for row in colours]
    return np.concatenate(rows, axis=0)


def image(kind: int, tag: int, data: bytes, textures: list[tuple[int, int, int, int]], obj: bytes = b"") -> bytes:
    """A resource image: header and garbage, data, texture headers (offset, format, width, height), object."""
    headers = b""
    for offset, fmt, width, height in textures:
        packed = (width.bit_length() - 1) << 20 | (height.bit_length() - 1) << 24 | 1 << 16 | fmt << 8 | 0x29
        headers += struct.pack("<5I", 0x00040001, offset, 0, packed, 0)
    headers += b"\xee" * (-len(headers) % 16)
    head = struct.pack("<6I", kind, tag, len(textures), len(data), len(headers), len(obj)).ljust(0x80, b"\xcd")
    return head + data + headers + obj


def chunk(chunk_type: int, data: bytes, hashed: int = 0) -> bytes:
    """One chunk of the container: its 16-byte header, then `data` padded to 16."""
    data = data.ljust(-(-len(data) // 16) * 16, b"\0")
    return struct.pack("<4I", chunk_type, len(data), 0, hashed) + data


def resource(hashed: int, *parts: bytes) -> bytes:
    """One standalone resource."""
    return struct.pack("<4I", len(parts), sum(len(p) - 16 for p in parts), 0, hashed) + b"".join(parts)


def xh(path: str) -> int:
    """The Xbox archive's hash of a path below ee_files."""
    return zlib.crc32(("ee_files\\" + path).lower().encode())


def bink(width: int, height: int, frames: int, fps: tuple[int, int] = (30, 1)) -> bytes:
    """A Bink file with no audio: header, frame index and `frames` frames of 8 bytes."""
    first = movies.HEADER + 4 * (frames + 1)
    size = first + 8 * frames
    head = struct.pack("<3sc10I", b"BIK", b"i", size - 8, frames, 8, frames, width, height, fps[0], fps[1], 0, 0)
    index = [first + 8 * i for i in range(frames + 1)]
    index[0] |= 1
    return head + struct.pack(f"<{frames + 1}I", *index) + bytes(8 * frames)


#: The resource's one texture (16 x 16, four colours) and the level's two (16 x 8 and 4 x 4).
RESOURCE_BLOCKS = [[RED, RED, GREEN, GREEN], [RED, RED, GREEN, GREEN], [BLUE] * 4, [GREY, BLUE, GREY, BLUE]]
WIDE_BLOCKS = [[GREEN, BLUE, BLUE, GREEN], [GREY, GREY, RED, RED]]
SMALL_BLOCKS = [[GREY]]


@pytest.fixture
def xbox_folder(tmp_path: Path) -> Path:
    """A made-up extracted Xbox disc: an archive with one texture resource and one level's world, and two movies."""
    root = tmp_path / "xbox"
    (root / "bik").mkdir(parents=True)
    texture = image(0, 0x6F, solid_dxt1(RESOURCE_BLOCKS), [(0, xbox_gfx.DXT1, 16, 16)], b"\0" * 16)
    wide, small = solid_dxt1(WIDE_BLOCKS), solid_dxt1(SMALL_BLOCKS)
    level = image(
        5,
        0x6A,
        wide + small,
        [(0, xbox_gfx.DXT1, 16, 8), (len(wide), xbox_gfx.DXT1, 4, 4)],
        b"\0" * 32,
    )
    entries = [(resource(RESOURCE_HASH, chunk(0x2A, texture)), 0xAAAA0001), (level, xh("sectors\\level9.xlev"))]
    volume, rows = b"", []
    for data, hashed in entries:
        rows.append((1 << 24 | len(volume) // 1024, len(data), 0, hashed))
        volume += data + b"\0" * (-len(data) % 2048)
    names = [b"System.wad0", b"Main.wad0"]
    index = struct.pack("<5I", 2, len(rows), 0x00100000, 0x14, 0x14 + 36 * 2)
    index += b"".join(name.ljust(32, b"\0") + struct.pack("<i", -1) for name in names)
    index += b"".join(struct.pack("<4I", *row) for row in rows)
    files = {
        "default.xbe": b"XBEH",
        "XBoxWad.idx": index,
        "System.wad0": b"",
        "Main.wad0": volume,
        "bik/l9_in_hd.bik": bink(1280, 720, 30),
        "bik/logo_hd.bik": bink(1280, 720, 90),
    }
    for name, data in files.items():
        (root / name).write_bytes(data)
    return root


# --- resource images and DXT ---


def test_parse_image_reads_headers_and_object() -> None:
    raw = image(5, 0x6A, b"\x11" * 24, [(0, xbox_gfx.DXT1, 4, 4), (8, xbox_gfx.DXT5, 8, 4)], b"OBJECT")
    parsed = xbox_gfx.parse_image(raw)
    assert (parsed.kind, parsed.tag, len(parsed.data), parsed.object) == (5, 0x6A, 24, b"OBJECT")
    first, second = parsed.textures()
    assert (first.offset, first.format_name, first.width, first.height) == (0, "DXT1", 4, 4)
    assert (second.offset, second.format_name, second.width, second.height) == (8, "DXT4/5", 8, 4)


def test_parse_image_refuses_sizes_that_do_not_fit() -> None:
    raw = image(2, 0x6D, b"\0" * 8, [(0, xbox_gfx.DXT1, 4, 4)])
    with pytest.raises(xbox_gfx.XboxGfxError):
        xbox_gfx.parse_image(raw[:-8])
    with pytest.raises(xbox_gfx.XboxGfxError):
        xbox_gfx.parse_image(raw[:0x40])


def test_vertex_and_index_buffer_headers() -> None:
    headers = struct.pack("<3I", 0x00800001, 0, 0) + struct.pack("<3I", 0x00010001, 0x90, 0) + b"\xee" * 8
    raw = struct.pack("<6I", 2, 0x6D, 2, 0x9C, len(headers), 0).ljust(0x80, b"\0") + b"\0" * 0x9C + headers
    vertices, indices = xbox_gfx.parse_image(raw).resources
    assert vertices.is_vertex_buffer and indices.is_index_buffer and indices.offset == 0x90


def test_dxt1_four_colours_and_transparent_black() -> None:
    # Four colours (first end point greater): indices 0, 1, 2, 3 along the first row.
    four = struct.pack("<HHI", rgb565(RED), rgb565(BLUE), 0b11100100)
    pixels = xbox_gfx.decode_dxt(four, xbox_gfx.DXT1, 4, 4)
    assert pixels[0, :, :3].tolist() == [[255, 0, 0], [0, 0, 255], [170, 0, 85], [85, 0, 170]]
    # Three colours (first end point not greater): index 3 is transparent black.
    three = struct.pack("<HHI", rgb565(BLUE), rgb565(RED), 0b11100100)
    pixels = xbox_gfx.decode_dxt(three, xbox_gfx.DXT1, 4, 4)
    assert pixels[0, 2].tolist() == [127, 0, 127, 255] and pixels[0, 3].tolist() == [0, 0, 0, 0]


def test_dxt3_and_dxt5_alpha() -> None:
    colour = struct.pack("<HHI", rgb565(GREY), rgb565(GREY), 0)
    explicit = bytes([0x10, 0xF0] + [0] * 6) + colour  # nibbles 0, 1, 0, 15 on the first row
    assert xbox_gfx.decode_dxt(explicit, xbox_gfx.DXT3, 4, 4)[0, :, 3].tolist() == [0, 17, 0, 255]
    # Eight-value mode (200 > 100): index 0 = 200, 1 = 100, 2 = (6*200 + 100) / 7.
    indices = 0 | 1 << 3 | 2 << 6
    interpolated = bytes([200, 100]) + indices.to_bytes(6, "little") + colour
    assert xbox_gfx.decode_dxt(interpolated, xbox_gfx.DXT5, 4, 4)[0, :3, 3].tolist() == [200, 100, 185]
    # Six-value mode (100 <= 200): index 6 is 0 and 7 is 255.
    indices = 6 | 7 << 3
    interpolated = bytes([100, 200]) + indices.to_bytes(6, "little") + colour
    assert xbox_gfx.decode_dxt(interpolated, xbox_gfx.DXT5, 4, 4)[0, :2, 3].tolist() == [0, 255]


def test_blocks_land_in_place_and_mips_add_up() -> None:
    pixels = xbox_gfx.decode_dxt(solid_dxt1(WIDE_BLOCKS), xbox_gfx.DXT1, 16, 8)
    assert np.array_equal(pixels, blocks_image(WIDE_BLOCKS))
    texture = xbox_gfx.D3DResource(4, 0, xbox_gfx.DXT5, 3, 16, 8)
    assert xbox_gfx.texture_size(texture) == (4 * 2 + 2 * 1 + 1 * 1) * 16
    with pytest.raises(xbox_gfx.XboxGfxError):
        xbox_gfx.decode_dxt(b"\0" * 8, xbox_gfx.DXT1, 8, 8)
    with pytest.raises(xbox_gfx.XboxGfxError):
        xbox_gfx.decode_dxt(b"\0" * 64, 0x06, 4, 4)


# --- matching and the rule ---


def shrink(rgba: np.ndarray, factor: int = 2) -> np.ndarray:
    """A box-filtered copy `factor` times smaller (what the PS2 texture of an Xbox one looks like)."""
    height, width = rgba.shape[0] // factor, rgba.shape[1] // factor
    return np.asarray(Image.fromarray(rgba).resize((width, height), Image.Resampling.BOX))


def test_score_tells_the_same_picture_from_another() -> None:
    big = blocks_image(RESOURCE_BLOCKS)
    assert xbox_match.score(shrink(big), big) == 0
    assert xbox_match.score(shrink(big), np.flipud(big)) > xbox_match.MATCH_LIMIT


def test_best_match_prefers_the_position_then_ranks() -> None:
    big, other = blocks_image(RESOURCE_BLOCKS), blocks_image([[GREY] * 4] * 4)
    candidates = [xbox_match.Candidate("other", 16, 16, other), xbox_match.Candidate("big", 16, 16, big)]
    ps2 = shrink(big)
    match = xbox_match.best_match(ps2, candidates, first=0)  # the guess fails, the ranking finds it
    assert match is not None and match.candidate.source == "big"
    assert xbox_match.best_match(ps2, [xbox_match.Candidate("wide", 16, 8, big[:8])]) is None


def test_structure_tells_a_same_coloured_texture_apart() -> None:
    big = blocks_image(RESOURCE_BLOCKS)
    assert xbox_match.structure(shrink(big), big) == pytest.approx(1.0)
    # The same colours rearranged: the score (mean colour) can be low, the structure is not.
    shuffled = blocks_image([[GREEN, RED, GREEN, RED], [BLUE, GREY, BLUE, GREY], [RED, GREEN, RED, GREEN], [BLUE] * 4])
    value = xbox_match.structure(shrink(big, 4), shuffled)
    assert value is not None and value < xbox_match.STRUCTURE_LIMIT
    flat = blocks_image([[GREY] * 4] * 4)
    assert xbox_match.structure(shrink(flat), flat) is None


def test_the_rule_wants_a_larger_matching_texture() -> None:
    big = blocks_image(RESOURCE_BLOCKS)
    larger = xbox_match.Match(xbox_match.Candidate("x", 16, 16, big), 4.0)
    assert xbox_match.replaces(8, 8, larger)
    assert not xbox_match.replaces(16, 16, larger)  # same size: a DXT re-encode of the PS2 image
    assert not xbox_match.replaces(8, 8, xbox_match.Match(larger.candidate, xbox_match.MATCH_LIMIT))
    assert not xbox_match.replaces(8, 8, None)
    assert not xbox_match.replaces(8, 8, xbox_match.Match(larger.candidate, 4.0, 0.2))  # another picture


def test_more_than_twice_as_large_needs_a_closer_structure() -> None:
    candidate = xbox_match.Candidate("x", 32, 32, blocks_image(RESOURCE_BLOCKS, 8))
    assert xbox_match.replaces(8, 8, xbox_match.Match(candidate, 4.0, 0.95))
    assert not xbox_match.replaces(8, 8, xbox_match.Match(candidate, 4.0, 0.8))
    assert not xbox_match.replaces(8, 8, xbox_match.Match(candidate, 4.0, None))  # flat: no telling at 4x
    assert xbox_match.replaces(16, 16, xbox_match.Match(candidate, 4.0, None))  # flat at 2x


# --- the resource index ---


def test_resource_index_round_trip_and_lookup(xbox_folder: Path) -> None:
    with XboxSource(xbox_folder) as source:
        index = source.resources
        (texture,) = index.chunks(RESOURCE_HASH, 0x2A)
        assert texture.volume == 1 and texture.entry == 0 and texture.offset == 32  # resource and chunk headers
        assert index.chunks(RESOURCE_HASH, 0x47) == [] and index.chunks(0x0BAD0BAD, 0x2A) == []
        data = xbox_index.to_bytes(index)
        assert len(data) == xbox_index.HEADER_SIZE + xbox_index.RECORD_SIZE * len(index.records)
        assert data[:8] == b"CXRI" + struct.pack("<I", 1)
        assert xbox_index.from_bytes(data) == index
        assert index.index_sha1 == hashlib.sha1((xbox_folder / "XBoxWad.idx").read_bytes()).digest()


def test_resource_index_refuses_bad_files() -> None:
    good = xbox_index.to_bytes(xbox_index.ResourceIndex(bytes(20), (xbox_index.ChunkLocation(1, 0x2A, 0, 64, 8, 0),)))
    with pytest.raises(ConfigError, match="not version"):
        xbox_index.from_bytes(b"XXXX" + good[4:])
    with pytest.raises(ConfigError, match="records need"):
        xbox_index.from_bytes(good[:-1])
    with pytest.raises(ConfigError, match="truncated"):
        xbox_index.from_bytes(good[:10])


def test_source_writes_the_index_once_and_reuses_it(tmp_path: Path, xbox_folder: Path) -> None:
    path = tmp_path / "index" / "xbox-resources.bin"
    with XboxSource(xbox_folder, path) as source:
        first = source.resource_textures(RESOURCE_HASH)
    written = path.read_bytes()
    assert [c.source for c in first] == [f"{RESOURCE_HASH:08x}#0"]
    # A file built from this disc is read, not rebuilt: an emptied copy with the right SHA-1 finds nothing.
    emptied = xbox_index.ResourceIndex(xbox_index.from_bytes(written).index_sha1, ())
    path.write_bytes(xbox_index.to_bytes(emptied))
    with XboxSource(xbox_folder, path) as source:
        assert source.resource_textures(RESOURCE_HASH) == []
    # One built from another archive (another SHA-1) is stale and rebuilt.
    path.write_bytes(xbox_index.to_bytes(xbox_index.ResourceIndex(bytes(20), ())))
    with XboxSource(xbox_folder, path):
        pass
    assert path.read_bytes() == written


def test_ps2_world_level() -> None:
    assert ps2_world_level("level99s_sec.wld") == "level99"
    assert ps2_world_level("level99d_ms12.sec") == "level99"
    assert ps2_world_level("objarena_ms3.sec") == "objarena"
    assert ps2_world_level("warriors.glr") is None


# --- the extract hooks ---


def written_png(output: Output, path: str) -> np.ndarray:
    """A PNG the extraction wrote, as RGBA."""
    return np.asarray(Image.open(io.BytesIO((output.root / path).read_bytes())).convert("RGBA"))


def ps2_dictionary(output: Output, folder: str, source: str, resource: int | None, pictures: list[np.ndarray]):  # type: ignore[no-untyped-def]
    """Write PS2 textures as the textures stage would and return its dictionary record."""
    records = []
    for number, picture in enumerate(pictures):
        path = output.write("textures", f"{folder}/t{number}.png", png_bytes(picture))
        records.append({"name": f"t{number}", "width": picture.shape[1], "height": picture.shape[0], "file": path})
    return _Dictionary(folder, source, records, resource_hash=resource)


def test_larger_xbox_textures_replace_the_ps2_ones(tmp_path: Path, xbox_folder: Path) -> None:
    output = Output(tmp_path / "out")
    output.start("textures")
    resource_dictionary = ps2_dictionary(
        output, "textures/props", "props", RESOURCE_HASH, [shrink(blocks_image(RESOURCE_BLOCKS))]
    )
    world = ps2_dictionary(
        output,
        "textures/worlds/level9s_sec.wld",
        "level9s_sec.wld",
        None,
        [shrink(blocks_image(WIDE_BLOCKS)), blocks_image(SMALL_BLOCKS), blocks_image([[RED, GREEN], [BLUE, RED]])],
    )
    unknown = ps2_dictionary(output, "textures/other", "other", 0x0BAD0BAD, [blocks_image(SMALL_BLOCKS)])
    report = Report("textures")
    with XboxSource(xbox_folder) as source:
        extract_xbox.upgrade_textures(output, [resource_dictionary, world, unknown], source, report)
    replaced = resource_dictionary.textures[0]
    assert replaced["source"] == "xbox" and replaced["ps2_size"] == [8, 8] and replaced["width"] == 16
    assert np.array_equal(written_png(output, replaced["file"]), blocks_image(RESOURCE_BLOCKS))
    assert (
        output.files["textures"][replaced["file"]]
        == hashlib.sha256((output.root / replaced["file"]).read_bytes()).hexdigest()
    )
    wide, small, unmatched = world.textures
    assert (
        wide["source"] == "xbox"
        and wide["xbox"] == "sectors/level9.xlev#0"
        and (wide["width"], wide["height"]) == (16, 8)
    )
    assert "source" not in small and "source" not in unmatched and "source" not in unknown.textures[0]
    assert report.counts == {
        "ps2 textures": 5,
        "xbox textures (larger)": 2,
        "ps2 kept: xbox not larger": 1,
        "ps2 kept: no matching xbox texture": 1,
        "ps2 kept: no xbox counterpart": 1,
    }


def test_replacements_make_the_texture_map(tmp_path: Path, xbox_folder: Path) -> None:
    output = Output(tmp_path / "out")
    output.start("textures")
    props = ps2_dictionary(output, "textures/props", "props", RESOURCE_HASH, [shrink(blocks_image(RESOURCE_BLOCKS))])
    world = ps2_dictionary(
        output, "textures/worlds/level9s_sec.wld", "level9s_sec.wld", None, [shrink(blocks_image(WIDE_BLOCKS))]
    )
    world.entry_hash = 0x00C0FFEE
    with XboxSource(xbox_folder) as source:
        extract_xbox.upgrade_textures(output, [props, world], source, Report("textures"))
        replacements = source.replacements
        (texture,) = source.resources.chunks(RESOURCE_HASH, 0x2A)
    texture_map = xbox_texture_map.make(bytes(20), bytes(range(20)), replacements)
    found = texture_map.find(xbox_texture_map.RESOURCE, RESOURCE_HASH, "T0")  # names are case-blind
    assert found is not None and (found.volume, found.image_offset, found.texture_number) == (1, texture.offset, 0)
    assert (found.width, found.height, found.image_size) == (16, 16, texture.size)
    wide = texture_map.find(xbox_texture_map.WORLD, 0x00C0FFEE, "t0")
    assert wide is not None and wide.texture_number == 0 and (wide.width, wide.height) == (16, 8)
    assert texture_map.find(xbox_texture_map.WORLD, 0x00C0FFEE, "t1") is None
    data = xbox_texture_map.to_bytes(texture_map)
    assert len(data) == xbox_texture_map.HEADER_SIZE + 2 * xbox_texture_map.RECORD_SIZE
    assert xbox_texture_map.from_bytes(data) == texture_map
    with pytest.raises(ConfigError, match="records need"):
        xbox_texture_map.from_bytes(data[:-4])


def test_texture_map_drops_keys_with_two_answers() -> None:
    first = xbox_texture_map.Replacement(1, 2, xbox_texture_map.RESOURCE, 0, 1, 4096, 512, 32, 32)
    other = xbox_texture_map.Replacement(1, 2, xbox_texture_map.RESOURCE, 1, 1, 4096, 512, 32, 32)
    kept = xbox_texture_map.Replacement(1, 3, xbox_texture_map.RESOURCE, 0, 1, 8192, 512, 32, 32)
    texture_map = xbox_texture_map.make(bytes(20), bytes(20), [first, kept, other, kept])
    assert texture_map.records == (kept,)


def test_output_replace_only_takes_the_types_own_files(tmp_path: Path) -> None:
    output = Output(tmp_path / "out")
    output.start("textures")
    path = output.write("textures", "textures/a.png", b"one")
    output.replace("textures", path, b"three")
    assert (output.root / path).read_bytes() == b"three" and output.sizes["textures"][path] == 5
    with pytest.raises(Exception, match="cannot be replaced"):
        output.replace("textures", "textures/b.png", b"x")


def test_hd_movie_replaces_the_ps2_one_only_when_as_long(xbox_folder: Path) -> None:
    ps2 = movies.parse_header(bink(640, 448, 30))
    with XboxSource(xbox_folder) as source:
        choice = extract_xbox.movie_choice("L9_IN", ps2, source)
        assert choice is not None and choice[0].lower() == "bik/l9_in_hd.bik" and choice[1].width == 1280
        assert extract_xbox.movie_choice("LOGO", ps2, source) is None  # three times as long
        assert extract_xbox.movie_choice("L1_IN", ps2, source) is None  # not on the Xbox disc
