# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools wad ...` commands: info, list, extract, names and scenes."""

from __future__ import annotations

import hashlib
from collections.abc import Callable
from pathlib import Path

from coney_tools import scenes, wad
from coney_tools.config import ConfigError, find_repo_root, load_config
from coney_tools.disc import Disc

_TOP_KINDS = 20  # kinds `wad info` lists before summing up the rest


class OutputClosedError(Exception):
    """Stdout was closed by its reader while a command was still printing."""


def open_disc(given: str | None) -> Disc:
    """Open the disc named on the command line, else the `game_dir` of `coney.local.toml`."""
    if given is not None:
        return Disc(Path(given))
    game_dir = load_config(find_repo_root(Path.cwd())).paths["game_dir"]
    if game_dir is None:
        raise ConfigError("no disc given and game_dir is not set in coney.local.toml; pass a folder or .iso path")
    return Disc(game_dir)


def split_disc_and_target(paths: list[str], what: str) -> tuple[str | None, Path]:
    """Split `[DISC] TARGET` positionals into the optional disc and the required target."""
    if len(paths) > 2:
        raise ConfigError(f"too many arguments; expected [DISC] {what}")
    return (paths[0] if len(paths) == 2 else None), Path(paths[-1])


def run_info(disc_arg: str | None, names_file: Path | None) -> int:
    """Print the entry count, sizes, a breakdown by first four bytes and how many names a names file resolves."""
    disc = open_disc(disc_arg)
    entries = wad.load_entries(disc)
    names = wad.load_names(names_file)
    known = sum(1 for entry in entries if entry.hash in names)
    print(f"entries: {len(entries)}")
    print(f"entry bytes: {sum(entry.size for entry in entries)}")
    print(f"{wad.WAD_FILE} bytes: {disc.size(wad.WAD_FILE)}")
    with disc.open(wad.WAD_FILE) as handle:
        kinds = wad.count_kinds(handle, entries)
    print("by first four bytes:")
    for kind, count in kinds.most_common(_TOP_KINDS):
        print(f"  {kind:>10}  {count}")
    if len(kinds) > _TOP_KINDS:
        rest = sum(count for _, count in kinds.most_common()[_TOP_KINDS:])
        print(f"  ({len(kinds) - _TOP_KINDS} more kinds, {rest} entries)")
    print(f"names known: {known} of {len(entries)}" + ("" if names_file else " (no --names file given)"))
    return 0


def run_list(disc_arg: str | None, names_file: Path | None) -> int:
    """Print one line per entry: index, offset, size, hash and the name when known."""
    disc = open_disc(disc_arg)
    entries = wad.load_entries(disc)
    names = wad.load_names(names_file)
    for entry in entries:
        line = f"{entry.index:5d} {entry.offset:10d} {entry.size:10d} {entry.hash:08x}"
        name = names.get(entry.hash)
        try:
            print(f"{line} {name}" if name else line)
        except OSError as error:
            # Only a failed write to stdout lands here, so any OSError means its reader went away (`list | head`).
            # Windows reports that as EINVAL rather than EPIPE, so the error number cannot tell us.
            raise OutputClosedError from error
    return 0


def run_extract(paths: list[str], names_file: Path | None, only: list[str] | None) -> int:
    """Write entries to OUT_DIR, refusing a folder inside the repository."""
    disc_arg, out_dir = split_disc_and_target(paths, "OUT_DIR")
    wad.refuse_inside_repo(out_dir)
    disc = open_disc(disc_arg)
    entries = wad.load_entries(disc)
    names = wad.load_names(names_file)
    wanted = wad.resolve_only(only, entries) if only else None
    written = wad.extract(disc, entries, names, out_dir, wanted)
    print(f"extracted {len(written)} entries, {sum(entry.size for entry, _ in written)} bytes")
    return 0


def run_names(paths: list[str]) -> int:
    """Recover names by hashing candidate strings from the executable and the WAD; write them to OUT_FILE."""
    disc_arg, out_file = split_disc_and_target(paths, "OUT_FILE")
    wad.refuse_inside_repo(out_file)
    disc = open_disc(disc_arg)
    entries = wad.load_entries(disc)
    found = wad.recover_names(disc, entries, progress=True)
    try:
        out_file.parent.mkdir(parents=True, exist_ok=True)
        out_file.write_text("".join(f"{name}\n" for name in sorted(found.values())), encoding="utf-8")
    except OSError as error:
        raise ConfigError(f"{out_file}: cannot be written ({error})") from error
    print(f"recovered {len(found)} of {len(entries)} names")
    return 0


def _scene_reader(disc: Disc, entries: list[wad.WadEntry]) -> tuple[Callable[[scenes.ListEntry], bytes | None], bytes]:
    """Return a reader of one listed scene record by name, and the bytes of `scene_list.cnk`.

    A record is found by the hash of `<name>.scn`. The names the list cuts to 16 characters hash to nothing, so those
    records are found among the WAD entries no listed name claims: the one of the same size whose own name field
    (+0x08 in a header, +0x04 in a segment) is the cut name.
    """
    by_hash = {entry.hash: entry for entry in entries}
    handle = disc.open(wad.WAD_FILE)

    def read(entry: wad.WadEntry) -> bytes:
        handle.seek(entry.offset)
        return handle.read(entry.size)

    listing = by_hash.get(wad.name_hash(wad.NAME_PREFIX + scenes.SCENE_LIST_FILE))
    if listing is None:
        raise ConfigError(f"no {scenes.SCENE_LIST_FILE} in {wad.WAD_FILE}")
    list_bytes = read(listing)
    listed = scenes.parse_scene_list(list_bytes)
    claimed = {wad.name_hash(f"{wad.NAME_PREFIX}{item.name}.scn") for item in listed}
    unclaimed: dict[int, list[wad.WadEntry]] = {}
    for entry in entries:
        if entry.hash not in claimed:
            unclaimed.setdefault(entry.size, []).append(entry)

    def read_scene(item: scenes.ListEntry) -> bytes | None:
        entry = by_hash.get(wad.name_hash(f"{wad.NAME_PREFIX}{item.name}.scn"))
        if entry is not None:
            return read(entry)
        name = item.name.encode("latin-1")
        for candidate in unclaimed.get(item.size, []):
            data = read(candidate)
            own = data[8:24] if scenes.is_header(data) else data[4:20]
            if own.split(b"\0", 1)[0] == name:
                return data
        return None

    return read_scene, list_bytes


def run_scenes(disc_arg: str | None) -> int:
    """Parse every record of the scene list and print counts and hashes only; exit 1 when any record fails."""
    disc = open_disc(disc_arg)
    entries = wad.load_entries(disc)
    read_scene, list_bytes = _scene_reader(disc, entries)
    listed = scenes.parse_scene_list(list_bytes)
    digest = hashlib.sha256()

    def read_and_hash(item: scenes.ListEntry) -> bytes | None:
        data = read_scene(item)
        digest.update(hashlib.sha256(data or b"").digest())
        return data

    result = scenes.survey(listed, read_and_hash)
    print(f"{scenes.SCENE_LIST_FILE}: {len(listed)} records, sha256 {hashlib.sha256(list_bytes).hexdigest()}")
    print(f"parsed: {result.headers} headers, {result.segments} segments, {len(result.errors)} failed")
    print(
        f"headers hold: {result.roles} human roles, {result.objects} objects, {result.cameras} cameras,"
        f" {result.lights} lights, {result.frames} frames ({result.frames / scenes.FPS:.0f} s)"
    )
    print(
        f"segment chains: {result.broken_chains} broken; frame counts that differ from the parts: "
        f"{result.frame_mismatches}"
    )
    print("clip event types: " + ", ".join(f"{k}:{v}" for k, v in sorted(result.clip_events.items())))
    print("track event types: " + ", ".join(f"{k}:{v}" for k, v in sorted(result.track_events.items())))
    print(f"records sha256: {digest.hexdigest()}")
    for error in result.errors[:20]:
        print(f"  {error}")
    return 1 if result.errors else 0
