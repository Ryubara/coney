# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the xbox commands, the XDVDFS reader and the chunk container, on tiny synthetic discs built byte by byte.

Every name, hash and byte here is made up; nothing comes from a game disc.
"""

from __future__ import annotations

import io
import re
import struct
import zlib
from pathlib import Path

import pytest

from coney_tools import chunks, xbox, xdvdfs
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.disc import SECTOR

TEXTURE_HASH = 0x11111111  # a texture resource in the pack, and again standalone
MODEL_HASH = 0x22222222
STANDALONE_HASH = 0x33333333  # a standalone texture resource with two textures


def xh(path: str) -> int:
    """The Xbox name hash of a path below ee_files, written out independently of `xbox.name_hash`."""
    return zlib.crc32(("ee_files\\" + path).lower().encode())


# --- the chunk container and the texture chunk ---


def chunk(chunk_type: int, data: bytes, hashed: int = 0) -> bytes:
    """One chunk: its 16-byte header, then `data` padded to a multiple of 16."""
    data = data.ljust(-(-len(data) // 16) * 16, b"\0")
    return struct.pack("<4I", chunk_type, len(data), 0, hashed) + data


def resource(hashed: int, *parts: bytes) -> bytes:
    """One resource: its header (dataSize counts the chunks' data, not their headers), then the chunks."""
    data_size = sum(len(part) - 16 for part in parts)
    return struct.pack("<4I", len(parts), data_size, 0, hashed) + b"".join(parts)


def pack(*resources: bytes) -> bytes:
    """One pack: its header (size is everything after it), then the resources."""
    body = b"".join(resources)
    return struct.pack("<4I", len(resources), len(body), 0, chunks.PACK_MARKER) + body


def texture(d3d_format: int, width: int, height: int, mips: int = 1, pixels: int = 32) -> bytes:
    """The data of an Xbox texture chunk: header, 96 bytes of filler, pixels, descriptor, filler."""
    head = struct.pack("<8I", 0, 0x6F, 1, pixels, 0x20, 0x10, 0x12345678, 0) + b"\xcd" * 96
    packed = (width.bit_length() - 1) << 20 | (height.bit_length() - 1) << 24 | mips << 16 | d3d_format << 8 | 0x29
    descriptor = bytearray(b"\xee" * 64)
    struct.pack_into("<5I", descriptor, 0, 0x00040001, 0, 0, packed, 0)
    struct.pack_into("<HH", descriptor, 0x28, width, height)
    return head + b"\x5a" * pixels + bytes(descriptor) + b"\xee" * 16


def test_parse_container_pack_and_resource() -> None:
    model = resource(MODEL_HASH, chunk(0x47, struct.pack("<II", 2, 0x6D)), chunk(0x28, b"bones"))
    parsed = chunks.parse_container(pack(resource(TEXTURE_HASH, chunk(0x2A, texture(0x0C, 8, 4))), model))
    assert parsed is not None and parsed.is_pack
    assert [r.hash for r in parsed.resources] == [TEXTURE_HASH, MODEL_HASH]
    assert parsed.resources[1].types == {0x47, 0x28}
    single = chunks.parse_container(model)
    assert single is not None and not single.is_pack
    assert single.resources[0].chunks[0].offset == 32  # resource header, then chunk header


def test_parse_container_rejects_what_does_not_fit_exactly() -> None:
    model = resource(MODEL_HASH, chunk(0x47, b"m"))
    assert chunks.parse_container(model + b"\0" * 16) is None  # trailing bytes
    assert chunks.parse_container(model[:-16]) is None  # cut off
    bad_size = bytearray(model)
    struct.pack_into("<I", bad_size, 4, 999)  # dataSize disagrees with the chunks
    assert chunks.parse_container(bytes(bad_size)) is None
    assert chunks.parse_container(b"short") is None
    assert chunks.looks_like_container(model[:16], len(model))
    assert not chunks.looks_like_container(model[:16], len(model) + 16)


def test_parse_texture_reads_format_mips_and_size() -> None:
    parsed = xbox.parse_texture(texture(0x0F, 16, 8, mips=4, pixels=64))
    assert parsed == xbox.XboxTexture(0x0F, 4, 16, 8, 64)
    assert parsed.format_name == "DXT4/5"
    assert xbox.XboxTexture(0x99, 1, 1, 1, 0).format_name == "0x99"


def test_parse_texture_rejects_other_shapes() -> None:
    good = bytearray(texture(0x0C, 8, 4))
    assert xbox.parse_texture(bytes(good[:100])) is None
    wrong_tag = bytearray(good)
    struct.pack_into("<I", wrong_tag, 4, 0x70)
    assert xbox.parse_texture(bytes(wrong_tag)) is None
    wrong_width = bytearray(good)
    struct.pack_into("<H", wrong_width, 128 + 32 + 0x28, 16)  # disagrees with log2 width in the Format word
    assert xbox.parse_texture(bytes(wrong_width)) is None


def rw_dictionary(count: int) -> bytes:
    """A RenderWare texture dictionary as far as its count: section 0x16, then a struct section with u16 count."""
    return struct.pack("<6IHH", 0x16, 28, 0x1C02000A, 1, 4, 0x1C02000A, count, 6)


def test_rw_texture_count() -> None:
    assert xbox.rw_texture_count(rw_dictionary(3)) == 3
    assert xbox.rw_texture_count(b"\x10" + rw_dictionary(3)[1:]) is None
    assert xbox.rw_texture_count(b"short") is None


# --- the index and the name hash ---


def test_name_hash_lowercases_and_takes_both_separators() -> None:
    assert xbox.name_hash("paks\\Global.PAK") == xh("paks\\global.pak")
    assert xbox.name_hash("paks/global.pak") == xh("paks\\global.pak")
    assert xbox.name_hash("ee_files\\paks\\global.pak") == xh("paks\\global.pak")
    assert xbox.name_hash("./ee_files/start.lua") == xh("start.lua")


def test_hash_names_tries_bare_names_in_each_folder() -> None:
    table = xbox.hash_names(["walk.anm", "paks\\x.pak", "café.lua"])
    assert table[xh("walk.anm")] == "walk.anm"
    assert table[xh("anims\\walk.anm")] == "anims\\walk.anm"
    assert table[xh("paks\\walk.anm")] == "paks\\walk.anm"
    assert table[xh("paks\\x.pak")] == "paks\\x.pak" and xh("x.pak") not in table
    assert len(table) == 4


def test_read_name_list_takes_the_last_word(tmp_path: Path) -> None:
    names = tmp_path / "names.txt"
    names.write_text("# comment\n\nstart.lua\n  7 Main.wad0 0 16 0badf00d paks\\global.pak\n", encoding="utf-8")
    assert xbox.read_name_list(names) == ["start.lua", "paks\\global.pak"]
    with pytest.raises(ConfigError, match="cannot be read"):
        xbox.read_name_list(tmp_path / "missing.txt")


def index_bytes(volumes: list[str], entries: list[tuple[int, int, int, int]]) -> bytes:
    """An XBoxWad.idx: header, 36-byte volume records (with garbage after the NUL), 16-byte entries."""
    out = struct.pack("<5I", len(volumes), len(entries), 0x00100000, 0x14, 0x14 + 36 * len(volumes))
    for name in volumes:
        out += (name.encode() + b"\0garbage").ljust(32, b"\xab") + b"\xff\xff\xff\xff"
    for volume, offset, size, hashed in entries:
        out += struct.pack("<4I", volume << 24 | offset // 1024, size, 0, hashed)
    return out


def test_parse_index() -> None:
    data = index_bytes(["System.wad0", "Main.wad0"], [(0, 0, 100, 1), (1, 2048, 5, 2)])
    index = xbox.parse_index(data, {"System.wad0": 2048, "Main.wad0": 4096})
    assert index.volumes == ("System.wad0", "Main.wad0") and index.unknown == 0x00100000
    assert index.entries[1] == xbox.XboxEntry(1, 1, 2048, 5, 2)


def test_parse_index_errors() -> None:
    with pytest.raises(ConfigError, match="truncated"):
        xbox.parse_index(b"\x01\x00")
    data = index_bytes(["A.wad0"], [(0, 0, 10, 1)])
    with pytest.raises(ConfigError, match="1 entries"):
        xbox.parse_index(data[:-1])
    with pytest.raises(ConfigError, match="names volume 3 of 1"):
        xbox.parse_index(index_bytes(["A.wad0"], [(3, 0, 10, 1)]))
    with pytest.raises(ConfigError, match=r"past the end of A.wad0"):
        xbox.parse_index(data, {"A.wad0": 9})


# --- XDVDFS ---


def dir_table(records: list[tuple[str, int, int, bool]]) -> bytes:
    """A directory table: the records as a balanced binary search tree by name, laid out in pre-order."""
    ordered = sorted(records, key=lambda record: record[0].lower())
    sizes = [-(-(14 + len(record[0])) // 4) * 4 for record in ordered]
    layout: list[tuple[int, int, int]] = []  # (record index, left, right) as list positions, pre-order

    def place(low: int, high: int) -> int:
        """Lay out ordered[low:high] as a subtree; return its root's position in `layout` (-1: empty)."""
        if low >= high:
            return -1
        middle = (low + high) // 2
        position = len(layout)
        layout.append((middle, -1, -1))
        left = place(low, middle)
        right = place(middle + 1, high)
        layout[position] = (middle, left, right)
        return position

    place(0, len(ordered))
    offsets = []
    cursor = 0
    for record_index, _, _ in layout:
        offsets.append(cursor)
        cursor += sizes[record_index]
    out = bytearray(b"\xff" * (-(-cursor // SECTOR) * SECTOR))
    for (record_index, left, right), offset in zip(layout, offsets, strict=True):
        name, sector, size, is_directory = ordered[record_index]
        left_value = offsets[left] // 4 if left >= 0 else 0
        right_value = offsets[right] // 4 if right >= 0 else 0
        attributes = 0x10 if is_directory else 0x20
        record = struct.pack("<HHIIBB", left_value, right_value, sector, size, attributes, len(name)) + name.encode()
        out[offset : offset + sizes[record_index]] = record.ljust(sizes[record_index], b"\xff")
    return bytes(out)


def partition(files: dict[str, bytes]) -> bytes:
    """An XDVDFS partition holding `files` (paths with at most one folder level), its volume descriptor at sector 32."""
    folders = sorted({path.split("/")[0] for path in files if "/" in path})
    root_sector = 33
    folder_sectors = {folder: root_sector + 1 + n for n, folder in enumerate(folders)}
    cursor = root_sector + 1 + len(folders)
    placed: dict[str, int] = {}
    for path, data in files.items():
        placed[path] = cursor
        cursor += max(1, -(-len(data) // SECTOR))
    tables = {
        folder: dir_table(
            [(p.split("/")[1], placed[p], len(files[p]), False) for p in files if p.startswith(folder + "/")]
        )
        for folder in folders
    }
    root = dir_table(
        [(p, placed[p], len(files[p]), False) for p in files if "/" not in p]
        + [(folder, folder_sectors[folder], len(tables[folder]), True) for folder in folders]
    )
    image = bytearray(cursor * SECTOR)
    descriptor = xdvdfs.MAGIC + struct.pack("<II", root_sector, len(root)) + b"\0" * 8
    image[32 * SECTOR : 32 * SECTOR + len(descriptor)] = descriptor
    image[32 * SECTOR + 0x7EC : 32 * SECTOR + 0x7EC + len(xdvdfs.MAGIC)] = xdvdfs.MAGIC
    image[root_sector * SECTOR : root_sector * SECTOR + len(root)] = root
    for folder, sector in folder_sectors.items():
        image[sector * SECTOR : sector * SECTOR + len(tables[folder])] = tables[folder]
    for path, sector in placed.items():
        image[sector * SECTOR : sector * SECTOR + len(files[path])] = files[path]
    return bytes(image)


class SparseImage(io.RawIOBase):
    """A read-only image that is zero everywhere except one block placed at a large offset, without the zeros."""

    def __init__(self, offset: int, block: bytes) -> None:
        super().__init__()
        self._offset = offset
        self._block = block
        self._pos = 0

    def readable(self) -> bool:
        return True

    def seekable(self) -> bool:
        return True

    def seek(self, offset: int, whence: int = io.SEEK_SET) -> int:
        """Move to an absolute position; only SEEK_SET is needed by the reader."""
        self._pos = offset
        return self._pos

    def readinto(self, buffer: bytearray | memoryview) -> int:  # type: ignore[override]
        """Fill `buffer` with zeros, or the block's bytes where the read overlaps it."""
        end = self._offset + len(self._block)
        size = max(0, min(len(buffer), end - self._pos))
        for n in range(size):
            at = self._pos + n
            buffer[n] = self._block[at - self._offset] if at >= self._offset else 0
        self._pos += size
        return size


def test_xgd1_partition_found_at_its_offset() -> None:
    """A full disc image: the partition starts at 0x18300000, which the reader finds without reading the gap."""
    image = SparseImage(0x18300000, partition({"default.xbe": b"xbe", "bik/a.bik": b"BIKi"}))
    handle = io.BufferedReader(image)
    where = xdvdfs.find_partition(handle, Path("disc.iso"))  # type: ignore[arg-type]
    assert where == 0x18300000
    files, folders = xdvdfs.read_tree(handle, where, Path("disc.iso"))  # type: ignore[arg-type]
    assert folders == 1
    assert files["bik/a.bik"] == xdvdfs.XdvdfsFile("bik/a.bik", files["bik/a.bik"].sector, 4)


def test_xiso_reader_lists_and_streams_files(tmp_path: Path) -> None:
    names = {f"File{n:02d}.bin": bytes([n]) * (n * 300) for n in range(1, 12)}
    image = tmp_path / "game.xiso"
    image.write_bytes(partition({**names, "audio/Bank.xwb": b"WBND" + b"\0" * 5000}))
    disc = xdvdfs.XboxDisc(image)
    assert disc.partition == 0 and disc.directories == 1
    assert len(disc.files()) == 12 and disc.files()[0] == ("audio/Bank.xwb", 5004)
    assert disc.has("file03.BIN") and disc.has("AUDIO\\bank.xwb") and not disc.has("audio")
    assert disc.size("File11.bin") == 3300
    with disc.open("audio/bank.xwb") as handle:
        assert handle.read(4) == b"WBND"
        handle.seek(5000)
        assert handle.read(100) == b"\0" * 4
    with pytest.raises(ConfigError, match=r"no missing.bin"):
        disc.open("missing.bin")


def test_xdvdfs_rejects_other_files_and_loops(tmp_path: Path) -> None:
    junk = tmp_path / "junk.iso"
    junk.write_bytes(b"\0" * (SECTOR * 40))
    with pytest.raises(ConfigError, match="not an Xbox disc image"):
        xdvdfs.XboxDisc(junk)
    with pytest.raises(ConfigError, match="not a folder or an Xbox disc image"):
        xdvdfs.XboxDisc(tmp_path / "nothing")
    loop = bytearray(dir_table([("a", 40, 1, False), ("b", 41, 1, False)]))
    left, right = struct.unpack_from("<HH", loop, 0)
    child = (left or right) * 4
    struct.pack_into("<H", loop, child, child // 4)  # the child's left subtree is the child itself
    with pytest.raises(ConfigError, match="loops back"):
        xdvdfs.parse_table(bytes(loop), "t")
    assert xdvdfs.parse_table(b"\xff" * SECTOR, "t") == []


def test_folder_disc(tmp_path: Path) -> None:
    root = tmp_path / "xbox"
    (root / "audio").mkdir(parents=True)
    (root / "XBoxWad.idx").write_bytes(b"idx")
    (root / "audio" / "x.xwb").write_bytes(b"wave")
    disc = xdvdfs.XboxDisc(root)
    assert disc.partition is None and disc.directories == 1
    assert disc.files() == [("audio/x.xwb", 4), ("XBoxWad.idx", 3)]
    with disc.open("xboxwad.IDX") as handle:
        assert handle.read() == b"idx"


# --- a whole synthetic disc ---

SCENE = struct.pack("<II", 96, 0) + b"intro".ljust(16, b"\0") + b"\0" * 72  # header layout, its own name "intro"
LEVEL = resource(xh("level7.lev"), chunk(0x17, b"header"), chunk(0x03, b"collision"))
LUA = b"\x1bLua\x40\x01\x04\x04\x04\x20\x06\x09\x08" + b"\0require level7_objs.txt\0 combat_room \0"
TEXT = b"1\r\n{crate {1, 2, 3}}\r\n"


def entries() -> list[tuple[int, bytes, int]]:
    """The made-up archive's entries in index order: (volume, bytes, hash)."""
    textures = resource(TEXTURE_HASH, chunk(0x2A, texture(0x0C, 8, 4)))
    model = resource(MODEL_HASH, chunk(0x47, struct.pack("<II", 2, 0x6D)))
    standalone = resource(STANDALONE_HASH, chunk(0x2A, texture(0x0F, 16, 16, mips=5)), chunk(0x2A, texture(0x0C, 4, 4)))
    return [
        (0, pack(textures, model), xh("paks\\global.pak")),
        (1, standalone, 0xAAAA0001),
        (1, LUA, xh("start.lua")),
        (1, SCENE, xh("intro.scn")),
        (1, struct.pack("<II", 4, 0x6F) + b"\0" * 56, 0xAAAA0002),
        (1, TEXT, xh("level7_objs.txt")),
        (1, LEVEL, xh("level7.lev")),
        (1, b"\x99" * 40, 0xAAAA0003),
        (1, resource(xh("combat_room.lev"), chunk(0x17, b"h")), xh("combat_room.lev")),
    ]


def disc_files() -> dict[str, bytes]:
    """The files of the made-up Xbox disc: executable, index, two volumes and a wave bank in a folder."""
    volumes = {0: b"", 1: b""}
    rows = []
    for volume, data, hashed in entries():
        rows.append((volume, len(volumes[volume]), len(data), hashed))
        volumes[volume] += data + b"\0" * (-len(data) % 2048)
    return {
        "default.xbe": b"XBEH\0D:\\XBoxWad.idx\0ee_files\\anims\\walk.anm\0",
        "XBoxWad.idx": index_bytes(["System.wad0", "Main.wad0"], rows),
        "System.wad0": volumes[0],
        "Main.wad0": volumes[1],
        "audio/xbox000.xwb": b"WBND",
    }


@pytest.fixture
def xiso(tmp_path: Path) -> Path:
    """The made-up Xbox disc as an XISO image."""
    path = tmp_path / "xbox.iso"
    path.write_bytes(partition(disc_files()))
    return path


@pytest.fixture
def repo(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A fake checkout as the working directory, so output inside it is refused."""
    root = tmp_path / "checkout"
    root.mkdir()
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(root)
    return root


def test_survey_classifies_every_entry(xiso: Path) -> None:
    disc = xdvdfs.XboxDisc(xiso)
    index = xbox.load_index(disc)
    result = xbox.survey(disc, index, keep_texts=True)
    assert result.kinds == [
        xbox.PACK,
        xbox.TEXTURE,
        xbox.LUA,
        xbox.SCENE,
        xbox.WORLD,
        xbox.TEXT,
        xbox.LEVEL,
        xbox.UNKNOWN,
        xbox.LEVEL,
    ]
    assert result.scene_names == {3: "intro"}
    assert set(result.texts) == {2, 5}
    assert len(result.resources) == 5 and [r.texture is not None for r in result.textures] == [True] * 3
    assert result.signatures[(0x47, 2, 0x6D)] == 1 and result.signatures[(0x2A, 0, 0x6F)] == 3
    keys = result.index()
    assert [entry for entry, _ in keys[(STANDALONE_HASH, 0x2A)]] == [1, 1]


def test_resource_kind_by_chunk_types() -> None:
    # The kind of a made-up resource holding one empty chunk of each type.
    def kind(*types: int) -> str:
        return xbox.resource_kind(chunks.Resource(1, 0, tuple(chunks.Chunk(t, 0, 0, 0) for t in types)))

    assert kind(0x00, 0x02) == xbox.ANIMATION
    assert kind(0x00, 0x02, 0x08, 0x45) == xbox.CHARACTER
    assert kind(0x47, 0x28) == xbox.MODEL
    assert kind(0x2A, 0x4C) == xbox.TEXTURE
    assert kind(0x43) == xbox.GLOBAL
    assert kind(0x17, 0x2A, 0x47) == xbox.LEVEL
    assert kind(0x51) == xbox.OTHER_RESOURCE


def test_files_and_info(xiso: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["xbox", "files", str(xiso)]) == 0
    out = capsys.readouterr().out
    assert "audio/xbox000.xwb" in out and "partition at byte 0x0: 5 files, 1 directories" in out
    names = tmp_path / "names.txt"
    names.write_text("global.pak\nstart.lua\n", encoding="utf-8")
    assert main(["xbox", "info", str(xiso), "--names", str(names)]) == 0
    out = capsys.readouterr().out
    assert "entries: 9" in out and "index header third word: 0x00100000" in out
    assert re.search(r"System.wad0 +1 ", out) and re.search(r"Main.wad0 +8 ", out)
    assert re.search(r"resource: level +2 ", out) and re.search(r"unknown +1 +40\n", out)
    assert "names known: 2 of 9" in out


def test_list_shows_volume_and_names(xiso: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    names = tmp_path / "names.txt"
    names.write_text("global.pak\n", encoding="utf-8")
    assert main(["xbox", "list", str(xiso), "--names", str(names)]) == 0
    lines = capsys.readouterr().out.splitlines()
    first = lines[0].split()
    assert first[:3] == ["0", "System.wad0", "0"]
    assert first[4:] == [f"{xh('paks\\global.pak'):08x}", "paks\\global.pak"]
    assert lines[1].split()[:3] == ["1", "Main.wad0", "0"] and lines[1].split()[-1] == "aaaa0001"
    assert lines[2].split()[2] == "2048"


def test_names_trusts_candidates_and_checks_guesses(
    xiso: Path, tmp_path: Path, repo: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """Candidates and the scene record's own name are kept; guesses only where the entry's kind fits."""
    candidates = tmp_path / "ps2names.txt"
    candidates.write_text("global.pak\n", encoding="utf-8")
    out = tmp_path / "found.txt"
    assert main(["xbox", "names", str(xiso), str(out), "--candidates", str(candidates)]) == 0
    assert "recovered 5 names for 5 of 9 entries" in capsys.readouterr().out
    # global.pak is a candidate and intro.scn the record's own name; level7.lev comes from the level range,
    # level7_objs.txt and combat_room.lev (a bare word) from the Lua bytecode. start.lua is never mentioned, and
    # anims\walk.anm, named in the executable, has no entry.
    assert out.read_text(encoding="utf-8").splitlines() == [
        "combat_room.lev",
        "intro.scn",
        "level7.lev",
        "level7_objs.txt",
        "paks\\global.pak",
    ]


def test_names_refuses_a_file_inside_the_repository(xiso: Path, repo: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["xbox", "names", str(xiso), str(repo / "names.txt")]) == 2
    assert "inside the repository" in capsys.readouterr().err


def test_extract_keeps_folders_and_only(xiso: Path, tmp_path: Path, repo: Path) -> None:
    names = tmp_path / "names.txt"
    names.write_text("paks\\global.pak\nstart.lua\n", encoding="utf-8")
    out = tmp_path / "out"
    assert (
        main(["xbox", "extract", str(xiso), str(out), "--names", str(names), "--only", "global.pak", "aaaa0003"]) == 0
    )
    assert sorted(p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file()) == [
        "aaaa0003.bin",
        "paks/global.pak",
    ]
    assert (out / "aaaa0003.bin").read_bytes() == b"\x99" * 40
    assert main(["xbox", "extract", str(xiso), str(repo / "out")]) == 2
    assert main(["xbox", "extract", str(xiso), str(out), "--only", "nope.lua"]) == 2


def ps2_folder(root: Path) -> Path:
    """A made-up PS2 disc folder: a pack with the shared texture resource (2 textures), the model, and the
    standalone texture resource (2 textures), as RenderWare dictionaries."""
    items = [
        pack(resource(TEXTURE_HASH, chunk(0x2A, rw_dictionary(2))), resource(MODEL_HASH, chunk(0x47, b"clump"))),
        resource(STANDALONE_HASH, chunk(0x2A, rw_dictionary(2))),
        b"\x1bLua not a container",
    ]
    directory = struct.pack("<I12x", len(items))
    wad = b""
    for n, data in enumerate(items):
        directory += struct.pack("<III", len(wad), len(data), n)
        wad += data + b"\0" * (-len(data) % SECTOR)
    root.mkdir()
    (root / "WARRIORS.DIR").write_bytes(directory)
    (root / "WARRIORS.WAD").write_bytes(wad)
    return root


def test_resources_and_ps2_overlap(xiso: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["xbox", "resources", str(xiso), "--ps2", str(ps2_folder(tmp_path / "ps2"))]) == 0
    out = capsys.readouterr().out
    assert "containers: 1 packs, 3 standalone resources" in out
    assert "resources: 5, chunks: 7" in out
    assert "0x2a    0 0x006f       3" in out
    assert "0x2a texture dictionaries        2      2" in out
    assert "0x47 models                      1      1" in out


def test_textures_counts_and_ps2_comparison(xiso: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["xbox", "textures", str(xiso), "--ps2", str(ps2_folder(tmp_path / "ps2"))]) == 0
    out = capsys.readouterr().out
    assert "texture chunks: 3, readable: 3" in out
    assert "DXT1  2" in out and "DXT4/5  1" in out
    assert "16x16  1" in out and "largest side: 16" in out
    # The standalone resource holds 2 textures on both; the pack's holds 1 on the Xbox and 2 on the PS2.
    assert "texture resources on both discs (first instance each): 2" in out
    assert "same texture count: 1, textures: 2" in out
    assert "Xbox -1: 1" in out


def test_info_on_a_damaged_index_is_exit_2(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    files = disc_files()
    files["XBoxWad.idx"] = files["XBoxWad.idx"][:-3]
    image = tmp_path / "bad.iso"
    image.write_bytes(partition(files))
    assert main(["xbox", "info", str(image)]) == 2
    err = capsys.readouterr().err
    assert "XBoxWad.idx" in err and "Traceback" not in err
