# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools xbox ...` commands: files, info, list, names, extract, resources, index, texture-map
and textures.

They read the player's own Xbox disc and print counts, hashes and offsets only, so the survey in
docs/research/xbox-assets.md can be repeated by anyone who owns the disc.
"""

from __future__ import annotations

import hashlib
import tempfile
from collections import Counter
from pathlib import Path

from coney_tools import xbox, xbox_index
from coney_tools.config import ConfigError
from coney_tools.disc import Disc
from coney_tools.wad import refuse_inside_repo
from coney_tools.wad_cli import OutputClosedError
from coney_tools.xdvdfs import XboxDisc

_TOP_SIZES = 12  # texture sizes `xbox textures` lists before summing up the rest
#: Chunk types whose resources `xbox resources --ps2` compares, with the names the survey uses.
_COMPARED_TYPES = {0x2A: "texture dictionaries", 0x47: "models", 0x00: "animations", 0x08: "characters"}


def _print(line: str) -> None:
    """Print a line, turning a failed write to a closed stdout (`xbox list | head`) into OutputClosedError."""
    try:
        print(line)
    except OSError as error:
        # Windows reports a closed pipe as EINVAL rather than EPIPE, so any OSError on stdout counts.
        raise OutputClosedError from error


def run_files(disc_arg: str) -> int:
    """Print every file on the disc with its size, then the totals."""
    disc = XboxDisc(Path(disc_arg))
    files = disc.files()
    for path, size in files:
        _print(f"{size:12d} {path}")
    where = "folder" if disc.partition is None else f"partition at byte {disc.partition:#x}"
    print(f"{where}: {len(files)} files, {disc.directories} directories, {sum(size for _, size in files)} bytes")
    return 0


def run_info(disc_arg: str, names_file: Path | None) -> int:
    """Print the index header, entries and bytes per volume, entries and bytes per kind, and names known."""
    disc = XboxDisc(Path(disc_arg))
    index = xbox.load_index(disc)
    names = xbox.load_names(names_file)
    result = xbox.survey(disc, index)
    print(f"entries: {len(index.entries)}")
    print(f"entry bytes: {sum(entry.size for entry in index.entries)}")
    print(f"index header third word: {index.unknown:#010x}")
    print("by volume (entries, entry bytes, volume bytes):")
    for number, volume in enumerate(index.volumes):
        mine = [entry for entry in index.entries if entry.volume == number]
        print(f"  {volume:<12} {len(mine):6d} {sum(e.size for e in mine):12d} {disc.size(volume):12d}")
    print("by kind (entries, bytes):")
    counts: Counter[str] = Counter(result.kinds)
    sizes: Counter[str] = Counter()
    for entry, kind in zip(index.entries, result.kinds, strict=True):
        sizes[kind] += entry.size
    for kind, count in counts.most_common():
        print(f"  {kind:<30} {count:6d} {sizes[kind]:12d}")
    known = sum(1 for entry in index.entries if entry.hash in names)
    print(f"names known: {known} of {len(index.entries)}" + ("" if names_file else " (no --names file given)"))
    return 0


def run_list(disc_arg: str, names_file: Path | None) -> int:
    """Print one line per entry: index, volume, offset, size, hash and the name when known."""
    disc = XboxDisc(Path(disc_arg))
    index = xbox.load_index(disc)
    names = xbox.load_names(names_file)
    for entry in index.entries:
        line = (
            f"{entry.index:5d} {index.volumes[entry.volume]:<11} {entry.offset:10d} {entry.size:10d} {entry.hash:08x}"
        )
        name = names.get(entry.hash)
        _print(f"{line} {name}" if name else line)
    return 0


def run_names(disc_arg: str, out_file: Path, candidates: list[Path] | None) -> int:
    """Match candidate names (files, the scene records' own names, level names, strings on the disc); write them."""
    refuse_inside_repo(out_file)
    disc = XboxDisc(Path(disc_arg))
    index = xbox.load_index(disc)
    given = [name for path in candidates or [] for name in xbox.read_name_list(path)]
    result = xbox.survey(disc, index, keep_texts=True)
    found = xbox.recover_names(disc, index, given, result)
    try:
        out_file.parent.mkdir(parents=True, exist_ok=True)
        out_file.write_text("".join(f"{name}\n" for name in sorted(found.values())), encoding="utf-8")
    except OSError as error:
        raise ConfigError(f"{out_file}: cannot be written ({error})") from error
    named = sum(1 for entry in index.entries if entry.hash in found)
    print(f"recovered {len(found)} names for {named} of {len(index.entries)} entries")
    by_kind: Counter[str] = Counter()
    for entry, kind in zip(index.entries, result.kinds, strict=True):
        by_kind[kind] += entry.hash not in found
    print("unnamed by kind:")
    for kind, count in by_kind.most_common():
        if count:
            print(f"  {kind:<30} {count:6d}")
    return 0


def run_extract(disc_arg: str, out_dir: Path, names_file: Path | None, only: list[str] | None) -> int:
    """Write entries to OUT_DIR, refusing a folder inside the repository."""
    refuse_inside_repo(out_dir)
    disc = XboxDisc(Path(disc_arg))
    index = xbox.load_index(disc)
    names = xbox.load_names(names_file)
    wanted = xbox.resolve_only(only, index) if only else None
    written = xbox.extract(disc, index, names, out_dir, wanted)
    print(f"extracted {len(written)} entries, {sum(entry.size for entry, _ in written)} bytes")
    return 0


def run_resources(disc_arg: str, ps2_arg: str | None) -> int:
    """Print the resource index's size and graphics-chunk signatures; with a PS2 disc, the shared resource hashes."""
    disc = XboxDisc(Path(disc_arg))
    index = xbox.load_index(disc)
    result = xbox.survey(disc, index)
    packs = sum(1 for kind in result.kinds if kind == xbox.PACK)
    standalone = sum(1 for _, in_pack, _ in result.resources if not in_pack)
    keys = result.index()
    print(f"containers: {packs} packs, {standalone} standalone resources")
    print(f"resources: {len(result.resources)}, chunks: {sum(len(r.chunks) for _, _, r in result.resources)}")
    print(f"distinct (resource hash, chunk type): {len(keys)}, resource hashes: {len({h for h, _ in keys})}")
    print("graphics chunks by first two words (type, words, chunks):")
    for (chunk_type, first, second), count in sorted(result.signatures.items()):
        print(f"  0x{chunk_type:02x}  {first:3d} {second:#06x}  {count:6d}")
    if ps2_arg is not None:
        mine = xbox.summarise_xbox(result)
        theirs = xbox.scan_ps2(Disc(Path(ps2_arg)))
        print("PS2 resource hashes found on the Xbox, by chunk type (on both / on the PS2):")
        for chunk_type, label in _COMPARED_TYPES.items():
            ps2 = theirs.by_type.get(chunk_type, set())
            both = ps2 & mine.by_type.get(chunk_type, set())
            print(f"  0x{chunk_type:02x} {label:<22} {len(both):6d} {len(ps2):6d}")
    return 0


def run_index(disc_arg: str, out_file: Path) -> int:
    """Write the resource index (xbox_index's layout) to `out_file`; print its record count and SHA-256."""
    refuse_inside_repo(out_file)
    disc = XboxDisc(Path(disc_arg))
    index = xbox.load_index(disc)
    data = xbox_index.to_bytes(xbox_index.build(disc, index, xbox.survey(disc, index)))
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_bytes(data)
    records = (len(data) - xbox_index.HEADER_SIZE) // xbox_index.RECORD_SIZE
    print(f"{out_file}: {records} records, {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}")
    return 0


def run_texture_map(ps2_arg: str, disc_arg: str, out_file: Path, index_file: Path | None) -> int:
    """Match every PS2 texture against the Xbox disc and write the texture map to `out_file`.

    It is `extract --only textures --xbox ... --xbox-texture-map` into a temporary folder that is deleted after.
    """
    from coney_tools import extract  # loads NumPy

    refuse_inside_repo(out_file)
    with tempfile.TemporaryDirectory(prefix="coney-texture-map-") as scratch:
        return extract.run(ps2_arg, Path(scratch), ["textures"], False, None, Path(disc_arg), index_file, out_file)


def run_textures(disc_arg: str, ps2_arg: str | None) -> int:
    """Print counts of the texture chunks by format, mip count and size; with a PS2 disc, the equal-count resources."""
    disc = XboxDisc(Path(disc_arg))
    index = xbox.load_index(disc)
    result = xbox.survey(disc, index)
    textures = [record.texture for record in result.textures if record.texture is not None]
    print(f"texture chunks: {len(result.textures)}, readable: {len(textures)}")
    chunk_bytes = sum(c.size for _, _, r in result.resources for c in r.chunks if c.type == xbox.TEXTURE_CHUNK)
    print(f"texture chunk bytes (every instance): {chunk_bytes}")
    print(f"texture resources (distinct hashes): {len({record.resource_hash for record in result.textures})}")
    _print_counts("by format", Counter(t.format_name for t in textures))
    _print_counts("by mip count", Counter(str(t.mips) for t in textures))
    sizes = Counter(f"{t.width}x{t.height}" for t in textures)
    _print_counts("by size", Counter(dict(sizes.most_common(_TOP_SIZES))))
    if len(sizes) > _TOP_SIZES:
        rest = sum(count for _, count in sizes.most_common()[_TOP_SIZES:])
        print(f"  ({len(sizes) - _TOP_SIZES} more sizes, {rest} textures)")
    print(f"largest side: {max((max(t.width, t.height) for t in textures), default=0)}")
    if ps2_arg is not None:
        _compare_textures(result, Disc(Path(ps2_arg)))
    return 0


def _print_counts(title: str, counts: Counter[str]) -> None:
    """Print a titled table of counts, most common first."""
    print(f"{title}:")
    for key, count in counts.most_common():
        print(f"  {key:>10}  {count}")


def _compare_textures(result: xbox.Survey, ps2_disc: Disc) -> None:
    """Print how many texture resources both discs share and how many hold the same number of textures."""
    mine = xbox.summarise_xbox(result).first_textures
    theirs = xbox.scan_ps2(ps2_disc).first_textures
    shared = sorted(set(mine) & set(theirs))
    equal = [key for key in shared if mine[key] == theirs[key]]
    differences = Counter(mine[key] - theirs[key] for key in shared if mine[key] != theirs[key])
    print(f"texture resources on both discs (first instance each): {len(shared)}")
    print(f"  same texture count: {len(equal)}, textures: {sum(mine[key] for key in equal)}")
    print(
        f"  textures in all shared resources: Xbox {sum(mine[k] for k in shared)}, PS2 {sum(theirs[k] for k in shared)}"
    )
    for delta, count in sorted(differences.items()):
        print(f"  Xbox {delta:+d}: {count}")
    first: dict[int, tuple[int, int]] = {}  # resource hash -> its first instance: (entry, resource offset)
    formats: Counter[str] = Counter()
    for record in result.textures:
        where = (record.entry, record.resource_offset)
        if record.resource_hash in theirs and first.setdefault(record.resource_hash, where) == where and record.texture:
            formats[record.texture.format_name] += 1
    _print_counts("formats in the shared resources (Xbox, first instance each)", formats)
