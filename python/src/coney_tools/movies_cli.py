# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools movies ...` command: list the game's Bink movies with their header values.

It reads the player's own disc and prints sizes, header values and a hash only, so the table on
docs/research/movies.md can be repeated by anyone who owns the disc.
"""

from __future__ import annotations

import hashlib
from collections.abc import Callable
from pathlib import Path
from typing import BinaryIO

from coney_tools import movies
from coney_tools.config import ConfigError, find_repo_root, load_config
from coney_tools.disc import Disc
from coney_tools.wad_cli import OutputClosedError

_HEAD = 64  # bytes read first, enough for `movies.header_size` to see the frame and track counts


def _print(line: str) -> None:
    """Print a line, turning a failed write to a closed stdout (`movies list | head`) into OutputClosedError."""
    try:
        print(line)
    except OSError as error:
        # Windows reports a closed pipe as EINVAL rather than EPIPE, so any OSError on stdout counts.
        raise OutputClosedError from error


def _movie_opener(disc_arg: str | None) -> Callable[[str], tuple[BinaryIO, int] | None]:
    """A function opening `PSS/<name>.BIK` on the disc given (else `game_dir`) with its size, or None if missing.

    A folder (a mounted disc) is read directly; an image goes through `Disc`, which names a file in a folder
    `PSS/<FILE>`.
    """
    if disc_arg is None:
        game_dir = load_config(find_repo_root(Path.cwd())).paths["game_dir"]
        if game_dir is None:
            raise ConfigError("no disc given and game_dir is not set in coney.local.toml; pass a folder or .iso path")
        root = game_dir
    else:
        root = Path(disc_arg)
    if root.is_dir():
        # Match the folder and file names in any case, as the disc's ISO 9660 names are upper case.
        folders = [child for child in root.iterdir() if child.is_dir() and child.name.upper() == movies.MOVIE_FOLDER]
        files = {child.name.upper(): child for child in folders[0].iterdir()} if folders else {}

        def open_file(name: str) -> tuple[BinaryIO, int] | None:
            path = files.get(f"{name}.BIK")
            return (path.open("rb"), path.stat().st_size) if path else None

        return open_file
    disc = Disc(root)

    def open_in_image(name: str) -> tuple[BinaryIO, int] | None:
        key = f"{movies.MOVIE_FOLDER}/{name}.BIK"
        return (disc.open(key), disc.size(key)) if disc.has(key) else None

    return open_in_image


def run_list(disc_arg: str | None) -> int:
    """Print one line per movie the game names, then totals and a hash of the headers; 1 if any is missing or bad."""
    open_movie = _movie_opener(disc_arg)
    digest = hashlib.sha256()
    found = failed = 0
    total_bytes = 0
    total_seconds = 0.0
    _print(f"{'name':<9} {'bytes':>10} {'rev':>3}  {'size':<7}  {'frames':>6}  {'fps':<8} {'seconds':>8}  audio")
    for name in movies.MOVIE_NAMES:
        opened = open_movie(name)
        if opened is None:
            _print(f"{name:<9} missing")
            failed += 1
            continue
        handle, size = opened
        with handle:
            head = handle.read(_HEAD)
            data = head + handle.read(max(0, movies.header_size(head) - len(head)))
        try:
            header = movies.parse_header(data, size, name)
        except movies.MovieError as error:
            _print(f"{name:<9} {error}")
            failed += 1
            continue
        found += 1
        total_bytes += size
        total_seconds += header.seconds
        digest.update(data[: header.index_size])
        audio = ", ".join(
            f"track {t.track_id}: {t.rate} Hz {t.channels} ch {t.bits}-bit {t.codec} (flags {t.flags:#06x})"
            for t in header.audio
        )
        _print(
            f"{name:<9} {size:>10} {header.revision:>3}  {header.width}x{header.height}  {header.frames:>6}"
            f"  {header.fps_num}/{header.fps_den:<4} {header.seconds:>8.2f}  {audio or 'none'}"
        )
    print(f"movies: {found} read, {failed} missing or failed; {total_bytes} bytes, {total_seconds:.1f} s")
    print(f"headers and frame indexes sha256: {digest.hexdigest()}")
    return 1 if failed else 0
