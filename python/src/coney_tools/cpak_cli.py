# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools cpak ...` commands: list, show and verify a Coney Pack.

They read an archive in place (the index, then each entry streamed when verifying) and print names, sizes and
hashes. The format: docs/guides/cpak-format.md.
"""

from __future__ import annotations

from pathlib import Path

from coney_tools import cpak
from coney_tools.config import ConfigError


def _open_index(path: Path) -> tuple[cpak.Header, list[cpak.Entry]]:
    """The header and entries of the archive at `path`; ConfigError (exit 2) when it cannot be read."""
    try:
        with path.open("rb") as handle:
            return cpak.read_index(handle, path.stat().st_size)
    except OSError as error:
        raise ConfigError(f"cannot read {path}: {error.strerror or error}") from error
    except cpak.CpakError as error:
        raise ConfigError(f"{path}: {error}") from error


def run_list(path: Path) -> int:
    """Print one line per entry: offset, stored size, size, compression, the hash's first 16 digits, the name."""
    header, entries = _open_index(path)
    print(f"{'offset':>12} {'stored':>12} {'size':>12} {'comp':<5} {'sha256':<16} name")
    for entry in entries:
        print(
            f"{entry.offset:>12} {entry.stored_size:>12} {entry.size:>12} {cpak.compression_name(entry.compression):<5}"
            f" {entry.sha256.hex()[:16]:<16} {entry.name}"
        )
    print(f"{len(entries)} entries, alignment {header.alignment}, {header.archive_size} bytes")
    return 0


def run_show(path: Path, name: str | None) -> int:
    """Print the header's values, or with `name` that entry's whole record; exit 2 when there is no such entry."""
    header, entries = _open_index(path)
    if name is None:
        print(f"version: {header.version}")
        print(f"alignment: {header.alignment}")
        print(f"entries: {header.entry_count}")
        print(f"index size: {header.index_size}")
        print(f"archive size: {header.archive_size}")
        print(f"index sha256: {header.index_sha256.hex()}")
        return 0
    entry = next((entry for entry in entries if entry.name == name), None)
    if entry is None:
        raise ConfigError(f"{path} has no entry {name}")
    print(f"name: {entry.name}")
    print(f"offset: {entry.offset}")
    print(f"stored size: {entry.stored_size}")
    print(f"size: {entry.size}")
    print(f"compression: {cpak.compression_name(entry.compression)}")
    print(f"sha256: {entry.sha256.hex()}")
    return 0


def run_verify(paths: list[Path]) -> int:
    """Check each archive's index and every entry's SHA-256; print what failed and a count; 1 if anything failed."""
    failed = 0
    for path in paths:
        try:
            with path.open("rb") as handle:
                _, entries = cpak.read_index(handle, path.stat().st_size)
                bad = 0
                for entry in entries:
                    try:
                        cpak.verify_entry(handle, entry)
                    except (OSError, cpak.CpakError) as error:
                        print(f"{path}: FAILED {entry.name}: {error}")
                        bad += 1
        except (OSError, cpak.CpakError) as error:
            # Opening the file or reading its index failed; an entry's read error is reported by its name above.
            print(f"{path}: FAILED index: {error}")
            failed += 1
            continue
        if bad:
            print(f"{path}: {len(entries) - bad} of {len(entries)} entries verified, {bad} failed")
            failed += 1
        else:
            print(f"{path}: {len(entries)} entries verified")
    if len(paths) > 1:
        print(f"archives: {len(paths) - failed} verified, {failed} failed")
    return 1 if failed else 0
