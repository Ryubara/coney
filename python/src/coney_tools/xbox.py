# SPDX-License-Identifier: GPL-3.0-or-later
"""The Xbox disc's archive: `XBoxWad.idx` over eight volumes, its name hash, entry kinds, resources and textures.

Everything here is read-only and streamed; nothing is decoded beyond headers, and the commands print counts only.

Research: docs/research/xbox-assets.md (The archive, Name hash, Kinds of entry, Formats, kind by kind)
"""

from __future__ import annotations

import re
import struct
import sys
import zlib
from collections import Counter
from collections.abc import Callable, Iterable, Iterator
from contextlib import ExitStack
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO

from coney_tools import chunks, wad
from coney_tools.config import ConfigError
from coney_tools.disc import Disc
from coney_tools.wad import read_chunks
from coney_tools.xdvdfs import XboxDisc

INDEX_FILE = "XBoxWad.idx"
XBE_FILE = "default.xbe"
#: The folder the game puts before every name it hashes.
NAME_PREFIX = "ee_files\\"
#: Subfolders of `ee_files\` that the names seen so far live in: none, packs, standalone animations.
FOLDERS: tuple[str, ...] = ("", "paks\\", "anims\\")
_HEADER = 20  # volumeCount, entryCount, unknown, volumesOffset, entriesOffset
_VOLUME = 36  # char name[32], u32 0xffffffff
_ENTRY = 16  # location, size, 0, nameHash
_UNIT = 1024  # an entry's offset is stored in 1,024-byte units
_HEAD = 64  # bytes of each entry read to tell its kind
_TEXT_LIMIT = 4 << 20  # larger entries are never taken for text, so they are not read whole for the test

# Kinds of entry, as named in xbox-assets.md's "Kinds of entry" table.
PACK = "pack"
TEXTURE = "resource: texture dictionary"
MODEL = "resource: model"
ANIMATION = "resource: animation"
CHARACTER = "resource: character"
LEVEL = "resource: level"
GLOBAL = "resource: global, scene list"
OTHER_RESOURCE = "resource: other"
SCENE = "scene record"
LUA = "lua bytecode"
TEXT = "text"
WORLD = "streamed world"
XACT = "xact sound bank"
BITMAP = "bitmap"
UNKNOWN = "unknown"

#: Chunk types found only in the global resource and the scene list.
_GLOBAL_TYPES = frozenset({0x29, 0x31, 0x43, 0x44, 0x46, 0x48, 0x49, 0x4D, 0x4E, 0x4F})
TEXTURE_CHUNK = 0x2A
MODEL_CHUNK = 0x47
WORLD_CHUNK = 0x15
#: Direct3D format numbers of the Xbox texture chunks (the XDK's), by name.
D3D_FORMATS = {0x0C: "DXT1", 0x0E: "DXT2/3", 0x0F: "DXT4/5"}
_LUA_MAGIC = b"\x1bLua"
_TEXT_BYTES = frozenset(range(0x20, 0x80)) | {0x09, 0x0A, 0x0D}  # DEL included: the font metrics hold one


def name_hash(name: str) -> int:
    """Return the hash `XBoxWad.idx` stores for `name`, a path below `ee_files\\` (`paks\\global.pak`).

    CRC-32 of `ee_files\\<name>`, lowercased. `/` is taken as `\\`, as the game's path normalisation splits on both.
    Research: docs/research/xbox-assets.md#name-hash
    """
    return zlib.crc32((NAME_PREFIX + relative_name(name)).lower().encode("ascii"))


def relative_name(name: str) -> str:
    """`name` with `\\` separators and without a leading `ee_files\\` or PS2 `./ee_files/`."""
    text = name.replace("/", "\\")
    for prefix in (".\\ee_files\\", NAME_PREFIX):
        if text.lower().startswith(prefix):
            return text[len(prefix) :]
    return text


@dataclass(frozen=True)
class XboxEntry:
    """One `XBoxWad.idx` entry: which volume, where in it, how big, and the hash of its name."""

    index: int  # position in the index
    volume: int  # index into XboxIndex.volumes
    offset: int  # bytes into the volume
    size: int  # bytes
    hash: int  # name_hash of its path below ee_files\


@dataclass(frozen=True)
class XboxIndex:
    """The parsed `XBoxWad.idx`: volume file names in order, and the entries."""

    volumes: tuple[str, ...]
    entries: tuple[XboxEntry, ...]
    unknown: int  # the header's third word, 0x00100000 on the disc; its meaning is unknown


def parse_index(data: bytes, volume_sizes: dict[str, int] | None = None) -> XboxIndex:
    """Parse the bytes of `XBoxWad.idx`.

    `volume_sizes` maps each volume's name to its byte size; when given, every entry must lie inside its volume.
    Raises ConfigError when the file is truncated, its size does not fit its counts, or an entry is out of range.
    """
    if len(data) < _HEADER:
        raise ConfigError(f"{INDEX_FILE}: truncated ({len(data)} bytes, the header alone is {_HEADER})")
    volume_count, entry_count, unknown, volumes_at, entries_at = struct.unpack_from("<5I", data, 0)
    expected = entries_at + entry_count * _ENTRY
    if volumes_at + volume_count * _VOLUME > entries_at or len(data) != expected:
        raise ConfigError(
            f"{INDEX_FILE}: header says {volume_count} volumes and {entry_count} entries from byte {entries_at} "
            f"({expected} bytes) but the file has {len(data)}"
        )
    # Volume names are NUL-terminated; the bytes after the NUL are whatever the build tool left there.
    volumes = tuple(
        data[volumes_at + n * _VOLUME : volumes_at + n * _VOLUME + 32].split(b"\0", 1)[0].decode("latin-1")
        for n in range(volume_count)
    )
    entries = []
    for index in range(entry_count):
        location, size, _, hashed = struct.unpack_from("<4I", data, entries_at + index * _ENTRY)
        volume, offset = location >> 24, (location & 0xFFFFFF) * _UNIT
        if volume >= volume_count:
            raise ConfigError(f"{INDEX_FILE}: entry {index} ({hashed:08x}) names volume {volume} of {volume_count}")
        if volume_sizes is not None and offset + size > volume_sizes.get(volumes[volume], 0):
            raise ConfigError(
                f"{INDEX_FILE}: entry {index} ({hashed:08x}) ends at {offset + size}, past the end of {volumes[volume]}"
            )
        entries.append(XboxEntry(index, volume, offset, size, hashed))
    return XboxIndex(volumes, tuple(entries), unknown)


def load_index(disc: XboxDisc) -> XboxIndex:
    """Read and validate the archive index of `disc`; every volume it names must be on the disc."""
    with disc.open(INDEX_FILE) as handle:
        data = handle.read()
    index = parse_index(data)
    sizes = {name: disc.size(name) for name in index.volumes}  # raises for a missing volume
    return parse_index(data, sizes)


def hash_names(names: Iterable[str]) -> dict[int, str]:
    """Map hash to path for each candidate name.

    A name with a folder (`paks\\global.pak`) is tried as given; a bare name in each of FOLDERS as well, since a
    PS2 names file has no folders. The value is the path below `ee_files\\`.
    """
    table: dict[int, str] = {}
    for name in names:
        if not name.isascii():
            continue
        path = relative_name(name)
        candidates = [path] if "\\" in path else [folder + path for folder in FOLDERS]
        for candidate in candidates:
            table.setdefault(name_hash(candidate), candidate)
    return table


def read_name_list(path: Path) -> list[str]:
    """Read a names file: the last word of each line, blank lines and `#` comments ignored.

    Taking the last word lets the output of `wad list` and `xbox list` serve as a names file too. Raises
    ConfigError when the file cannot be read.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    return [line.split()[-1] for line in text.splitlines() if line.strip() and not line.startswith("#")]


def load_names(path: Path | None) -> dict[int, str]:
    """Read a names file (Xbox paths or bare PS2 names) into a hash table; empty when `path` is None."""
    return hash_names(read_name_list(path)) if path is not None else {}


# --- kinds of entry ---


def _scene_name(head: bytes, size: int) -> str | None:
    """The record's own name if `head` starts a scene record of `size` bytes, else None.

    A record starts with its size; a header record then has a zero word and the name, a segment the name directly.
    """
    if len(head) < 8 or struct.unpack_from("<I", head, 0)[0] != size:
        return None
    start = 8 if struct.unpack_from("<I", head, 4)[0] == 0 else 4
    raw = head[start : start + 16].split(b"\0", 1)[0]
    if not raw or not re.fullmatch(rb"[A-Za-z0-9_][\x21-\x7e]*", raw):
        return None
    return raw.decode("ascii")


def resource_kind(resource: chunks.Resource) -> str:
    """Name the kind of a standalone resource by the chunk types it holds."""
    types = resource.types
    if 0x17 in types:  # a level header
        return LEVEL
    if types & _GLOBAL_TYPES:
        return GLOBAL
    if 0x08 in types:  # character data
        return CHARACTER
    if TEXTURE_CHUNK in types:
        return TEXTURE
    if MODEL_CHUNK in types:
        return MODEL
    if types <= {0x00, 0x02}:  # keyframes and their descriptor
        return ANIMATION
    return OTHER_RESOURCE


def is_text(data: bytes) -> bool:
    """Whether `data` is ASCII text (printable, DEL, tabs and line ends), ignoring trailing NUL padding."""
    body = data.rstrip(b"\0")
    return bool(body) and all(byte in _TEXT_BYTES for byte in body)


def classify(head: bytes, size: int, read_all: Callable[[], bytes]) -> tuple[str, chunks.Container | None, bytes]:
    """Tell an entry's kind from its first bytes, reading the whole entry (`read_all`) only when a test needs it.

    Returns the kind, the parsed container for packs and resources, and the whole entry when it was read (else
    `b""`). The tests follow xbox-assets.md's "Kinds of entry".
    """
    if chunks.looks_like_container(head, size):
        data = read_all()
        container = chunks.parse_container(data)
        if container is not None:
            return (PACK if container.is_pack else resource_kind(container.resources[0])), container, data
    if head.startswith(_LUA_MAGIC):
        return LUA, None, b""
    if head.startswith(b"SDBK"):
        return XACT, None, b""
    if head.startswith(b"BM") and len(head) >= 6 and struct.unpack_from("<I", head, 2)[0] == size:
        return BITMAP, None, b""
    if len(head) >= 8 and struct.unpack_from("<II", head, 0) in ((4, 0x6F), (5, 0x6A)):
        return WORLD, None, b""
    if _scene_name(head, size) is not None:
        return SCENE, None, b""
    if size <= _TEXT_LIMIT and is_text(head):
        data = read_all()
        if is_text(data):
            return TEXT, None, data
    return UNKNOWN, None, b""


# --- textures ---


@dataclass(frozen=True)
class XboxTexture:
    """The header fields of one Xbox texture chunk (`0x2a`): its Direct3D format, mip count and size."""

    format: int  # D3D format number: 0x0c DXT1, 0x0e DXT2/3, 0x0f DXT4/5
    mips: int
    width: int
    height: int
    data_size: int  # bytes of pixel data, every mip level

    @property
    def format_name(self) -> str:
        """The format's name (`DXT1`), or its number in hex for one not seen on the disc."""
        return D3D_FORMATS.get(self.format, f"0x{self.format:02x}")


_TEXTURE_HEAD = 32  # {0, 0x6f, 1, dataSize, 0x20, 0x10, pointer, 0}
_TEXTURE_PIXELS = 128  # the pixels start after the header and 96 bytes of build-tool garbage
_DESCRIPTOR = 64  # the D3D texture header, 0xee filler, u16 width and u16 height at +0x28


def parse_texture(data: bytes | memoryview) -> XboxTexture | None:
    """Read the header and descriptor of an Xbox texture chunk's data; None when it does not have that shape.

    The descriptor's Format word holds the D3D format (bits 8-15), mip count (16-19) and log2 of width (20-23) and
    height (24-27); a chunk whose stored width and height disagree with them is rejected.
    Research: docs/research/xbox-assets.md (Formats, kind by kind: Textures)
    """
    if len(data) < _TEXTURE_PIXELS + _DESCRIPTOR:
        return None
    kind, tag, _, data_size = struct.unpack_from("<4I", data, 0)
    at = _TEXTURE_PIXELS + data_size
    if kind != 0 or tag != 0x6F or at + _DESCRIPTOR > len(data):
        return None
    common, _, _, packed = struct.unpack_from("<4I", data, at)
    width, height = struct.unpack_from("<HH", data, at + 0x28)
    if common != 0x00040001 or width != 1 << ((packed >> 20) & 0xF) or height != 1 << ((packed >> 24) & 0xF):
        return None
    return XboxTexture((packed >> 8) & 0xFF, (packed >> 16) & 0xF, width, height, data_size)


def rw_texture_count(data: bytes | memoryview) -> int | None:
    """The texture count of a RenderWare texture dictionary (a PS2 `0x2a` chunk's data); None if it is not one.

    The dictionary is section `0x16` whose first child, a struct section (type 1), starts with `u16 count`.
    """
    if len(data) < 26:
        return None
    section, _, _, child, _, _, count = struct.unpack_from("<6IH", data, 0)
    return count if section == 0x16 and child == 1 else None


# --- one pass over the archive ---


@dataclass(frozen=True)
class TextureRecord:
    """A texture chunk found in the archive: the entry and resource it is in, and its header (None if unreadable)."""

    entry: int
    resource_hash: int
    resource_offset: int  # of the resource's header in the entry, which tells instances apart
    texture: XboxTexture | None


@dataclass
class Survey:
    """What one pass over the whole archive finds: each entry's kind, every resource and every texture."""

    kinds: list[str] = field(default_factory=list)  # by entry index
    resources: list[tuple[int, bool, chunks.Resource]] = field(default_factory=list)  # (entry, in a pack, resource)
    textures: list[TextureRecord] = field(default_factory=list)
    #: (chunk type, first word, second word) of each graphics chunk (types 0x15, 0x2a, 0x47).
    signatures: Counter[tuple[int, int, int]] = field(default_factory=Counter)
    scene_names: dict[int, str] = field(default_factory=dict)  # entry index -> the record's own name
    texts: dict[int, bytes] = field(default_factory=dict)  # entry index -> Lua bytecode or text, for name harvests

    def index(self) -> dict[tuple[int, int], list[tuple[int, chunks.Chunk]]]:
        """Every chunk keyed by `(resource hash, chunk type)`, with the entry that holds it, in archive order."""
        table: dict[tuple[int, int], list[tuple[int, chunks.Chunk]]] = {}
        for entry, _, resource in self.resources:
            for chunk in resource.chunks:
                table.setdefault((resource.hash, chunk.type), []).append((entry, chunk))
        return table


def iter_entries(disc: XboxDisc, index: XboxIndex) -> Iterator[tuple[XboxEntry, BinaryIO]]:
    """Yield each entry with an open volume handle positioned at its start; each volume is opened once."""
    with ExitStack() as stack:
        handles: dict[int, BinaryIO] = {}
        for entry in index.entries:
            if entry.volume not in handles:
                handles[entry.volume] = stack.enter_context(disc.open(index.volumes[entry.volume]))
            handle = handles[entry.volume]
            handle.seek(entry.offset)
            yield entry, handle


class _LazyEntry:
    """An entry's whole bytes, read on the first call only; the handle must still be just past `head`."""

    def __init__(self, handle: BinaryIO, head: bytes, size: int) -> None:
        self._handle = handle
        self._head = head
        self._size = size
        self._data: bytes | None = None

    def __call__(self) -> bytes:
        """Return the entry's bytes, reading the rest of it the first time."""
        if self._data is None:
            self._data = self._head + b"".join(read_chunks(self._handle, self._size - len(self._head)))
        return self._data


def survey(disc: XboxDisc, index: XboxIndex, keep_texts: bool = False, progress: bool = False) -> Survey:
    """Classify every entry and collect its resources, texture headers and graphics-chunk signatures.

    Reads each pack and resource whole (about 1 GB on the disc), the rest by its first bytes only. `keep_texts`
    also keeps Lua and text entries' bytes (about 12 MB) for name recovery.
    """
    result = Survey()
    for entry, handle in iter_entries(disc, index):
        head = handle.read(min(_HEAD, entry.size))
        read_all = _LazyEntry(handle, head, entry.size)
        kind, container, data = classify(head, entry.size, read_all)
        result.kinds.append(kind)
        if kind == SCENE:
            result.scene_names[entry.index] = _scene_name(head, entry.size) or ""
        if keep_texts and kind in (LUA, TEXT):
            result.texts[entry.index] = read_all()
        if container is not None:
            _collect_resources(result, entry.index, container, data)
        if progress and entry.index % 1000 == 999:
            print(f"  read {entry.index + 1}/{len(index.entries)} entries", file=sys.stderr)
    return result


def _collect_resources(result: Survey, entry: int, container: chunks.Container, data: bytes) -> None:
    """Add a container's resources, texture headers and graphics-chunk signatures to `result`."""
    view = memoryview(data)
    for resource in container.resources:
        result.resources.append((entry, container.is_pack, resource))
        for chunk in resource.chunks:
            if chunk.type in (WORLD_CHUNK, TEXTURE_CHUNK, MODEL_CHUNK) and chunk.size >= 8:
                first, second = struct.unpack_from("<II", data, chunk.offset)
                result.signatures[(chunk.type, first, second)] += 1
            if chunk.type == TEXTURE_CHUNK:
                texture = parse_texture(view[chunk.offset : chunk.offset + chunk.size])
                result.textures.append(TextureRecord(entry, resource.hash, resource.offset, texture))


# --- names ---

#: A file-name-like token, as in `wad.harvest`.
_TOKEN = re.compile(rb"[\w/\-]{1,100}(?:\.[A-Za-z0-9_-]{1,32})+")
#: A bare identifier, tried as a level name (`<word>.lev`): levels are named in scripts without the extension.
_WORD = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{2,63}")
_LEVEL_RANGE = 256  # level numbers tried as `level<N>.lev`
#: The kind of entry each extension names. A guessed name is kept only on an entry of its kind, which rejects
#: chance matches; one with any other extension is not kept.
_EXTENSION_KINDS = {
    ".scn": SCENE,
    ".lua": LUA,
    ".pak": PACK,
    ".lev": LEVEL,
    ".anm": ANIMATION,
    ".txt": TEXT,
    ".glr": GLOBAL,
    ".cnk": GLOBAL,
}


def harvest(data: bytes) -> set[str]:
    """Candidate names in `data`: the last component of every file-name-like token, and `<word>.lev` per identifier."""
    found = {match.group().decode("ascii").replace("/", "\\").rsplit("\\", 1)[-1] for match in _TOKEN.finditer(data)}
    found.update(match.group().decode("ascii") + ".lev" for match in _WORD.finditer(data))
    return found


def fits_kind(path: str, kind: str) -> bool:
    """Whether the extension of `path` names entries of `kind` (see _EXTENSION_KINDS)."""
    dot = path.rfind(".")
    return dot >= 0 and _EXTENSION_KINDS.get(path[dot:].lower()) == kind


def recover_names(disc: XboxDisc, index: XboxIndex, candidates: Iterable[str], result: Survey) -> dict[int, str]:
    """Match names against the index; return hash to path below `ee_files\\` for every entry hash a name matched.

    Trusted names are kept on any entry: the `candidates` (from names files) and the scene records' own names plus
    `.scn`. Guessed names are kept only on an entry of the kind their extension names: `level<N>.lev`, and the
    names harvested from the executable and from the Lua and text entries in `result` (a survey run with
    `keep_texts`).
    """
    kinds = {entry.hash: kind for entry, kind in zip(index.entries, result.kinds, strict=True)}
    found: dict[int, str] = {}
    own = [name + ".scn" for name in result.scene_names.values()]
    for hashed, path in hash_names([*candidates, *own]).items():
        if hashed in kinds:
            found.setdefault(hashed, path)
    guesses = {f"level{number}.lev" for number in range(_LEVEL_RANGE)}
    if disc.has(XBE_FILE):
        with disc.open(XBE_FILE) as xbe:
            guesses |= harvest(xbe.read())
    for data in result.texts.values():
        guesses |= harvest(data)
    for hashed, path in hash_names(sorted(guesses)).items():
        if hashed in kinds and hashed not in found and fits_kind(path, kinds[hashed]):
            found[hashed] = path
    return found


def resolve_only(only: list[str], index: XboxIndex) -> set[int]:
    """Turn `--only` arguments (8 hex digits, or a name) into entry indexes. Raises ConfigError for an unknown one."""
    by_hash: dict[int, list[int]] = {}
    for entry in index.entries:
        by_hash.setdefault(entry.hash, []).append(entry.index)
    wanted: set[int] = set()
    for item in only:
        text = item.lower().removeprefix("0x")
        keys = [int(text, 16)] if re.fullmatch(r"[0-9a-f]{8}", text) and int(text, 16) in by_hash else []
        if not keys and item.isascii():
            keys = [hashed for hashed in hash_names([item]) if hashed in by_hash]
        if not keys:
            raise ConfigError(f"--only {item}: no entry with that hash or name")
        for key in keys:
            wanted.update(by_hash[key])
    return wanted


def extract(
    disc: XboxDisc, index: XboxIndex, names: dict[int, str], out_dir: Path, wanted: set[int] | None
) -> list[tuple[XboxEntry, Path]]:
    """Write entries (all, or those whose index is in `wanted`) into `out_dir`, streaming each.

    A named entry keeps its folder (`paks/global.pak`); an unnamed one is `<hash>.bin`. Raises ConfigError when a
    file cannot be written.
    """
    written = []
    used: set[str] = set()
    for entry, handle in iter_entries(disc, index):
        if wanted is not None and entry.index not in wanted:
            continue
        name = names.get(entry.hash, "").replace("\\", "/")
        parts = name.split("/")
        if not name or any(part in ("", ".", "..") for part in parts) or name.lower() in used:
            name = f"{entry.hash:08x}.bin"
        used.add(name.lower())
        target = out_dir.joinpath(*name.split("/"))
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("wb") as out:
                for piece in read_chunks(handle, entry.size):
                    out.write(piece)
        except OSError as error:
            raise ConfigError(f"{target}: cannot be written ({error})") from error
        written.append((entry, target))
    return written


# --- comparison with the PS2 disc ---


@dataclass
class ResourceSummary:
    """Resource hashes by chunk type, and the texture count of each texture resource's first instance."""

    by_type: dict[int, set[int]] = field(default_factory=dict)  # chunk type -> hashes of resources holding it
    first_textures: dict[int, int] = field(default_factory=dict)  # resource hash -> textures in its first instance

    def add(self, resource: chunks.Resource, texture_count: int | None) -> None:
        """Add one resource, in archive order, with its texture count (None: unreadable, left out of the counts).

        Only the first instance of each resource hash counts towards `first_textures`.
        """
        for chunk_type in resource.types:
            self.by_type.setdefault(chunk_type, set()).add(resource.hash)
        first = TEXTURE_CHUNK in resource.types and resource.hash not in self.first_textures
        if first and texture_count is not None:
            self.first_textures[resource.hash] = texture_count


def summarise_xbox(result: Survey) -> ResourceSummary:
    """The Xbox side of the comparison: one texture per `0x2a` chunk."""
    summary = ResourceSummary()
    for _, _, resource in result.resources:
        summary.add(resource, sum(1 for chunk in resource.chunks if chunk.type == TEXTURE_CHUNK))
    return summary


def _ps2_texture_count(data: bytes, resource: chunks.Resource) -> int | None:
    """Textures in a PS2 resource: the sum of its RenderWare texture dictionaries' counts (None if one is not)."""
    total = 0
    for chunk in resource.chunks:
        if chunk.type == TEXTURE_CHUNK:
            count = rw_texture_count(data[chunk.offset : chunk.offset + chunk.size])
            if count is None:
                return None
            total += count
    return total


def scan_ps2(disc: Disc, progress: bool = False) -> ResourceSummary:
    """Summarise the PS2 archive's resources (in packs and standalone) for the comparison.

    Reads every pack and resource entry of WARRIORS.WAD whole, one at a time; other entries by 64 bytes.
    Research: docs/research/formats/wad-contents.md (Chunk container)
    """
    entries = wad.load_entries(disc)
    summary = ResourceSummary()
    with disc.open(wad.WAD_FILE) as handle:
        for entry in entries:
            handle.seek(entry.offset)
            head = handle.read(min(_HEAD, entry.size))
            if chunks.looks_like_container(head, entry.size):
                data = _LazyEntry(handle, head, entry.size)()
                container = chunks.parse_container(data)
                for resource in container.resources if container else ():
                    summary.add(resource, _ps2_texture_count(data, resource))
            if progress and entry.index % 1000 == 999:
                print(f"  read {entry.index + 1}/{len(entries)} PS2 entries", file=sys.stderr)
    return summary
