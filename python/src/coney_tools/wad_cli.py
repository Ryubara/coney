# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools wad ...` commands: info, list, extract and names."""

from __future__ import annotations

from pathlib import Path

from coney_tools import wad
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
