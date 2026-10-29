# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools lua ...`: the command line of the Lua 4.0 decompiler (`lua_decompile.py`).

* `lua decompile [DISC] NAME OUT_DIR` writes one chunk's decompile into OUT_DIR;
* `lua decompile [DISC] --all` writes every chunk, the guard's fingerprints and the index into the shared folder.

Both take `--chunks DIR` (a folder of extracted chunks instead of the disc), `--xbox-disc XBOX_DISC` (the Xbox
disc's scripts instead of the PS2 disc's), `--luac PATH` and `--roundtrip`; `--all` also takes `--out DIR` and the
frozen form's `--hashes FILE`, `--update-hashes` and `--new-form` (`lua_hashes.py`): it checks every decompile's
hash against the committed list.

* `lua compare DIR_A DIR_B` compares two `--all` outputs script by script (`lua_compare.py`), counts and names only.

Guide: docs/guides/coney-tools-fidelity.md#lua.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from coney_tools import lua_compare, lua_decompile
from coney_tools.config import ConfigError, find_repo_root


def add_parser(groups: Any) -> None:
    """Register `coney-tools lua ...`."""
    group = groups.add_parser("lua", help="the game's compiled Lua 4.0 scripts: decompile them for research")
    commands = group.add_subparsers(dest="command", required=True)
    decompile = commands.add_parser(
        "decompile",
        help="decompile one chunk (NAME OUT_DIR) or every chunk (--all) to readable Lua, outside the repository",
    )
    decompile.add_argument(
        "paths",
        nargs="*",
        metavar="[DISC] NAME OUT_DIR",
        help="a mounted disc, folder or .iso (default: game_dir), the chunk's file name or hash in hex, the folder "
        "to write it to; with --all, only [DISC]",
    )
    decompile.add_argument("--all", action="store_true", help="every chunk, into the shared folder (lua_dir)")
    decompile.add_argument("--out", type=Path, metavar="DIR", help="with --all: write here instead of lua_dir")
    decompile.add_argument(
        "--chunks", type=Path, metavar="DIR", help="read chunks from this folder (`wad extract` output), not a disc"
    )
    decompile.add_argument(
        "--xbox-disc",
        dest="xbox",
        type=Path,
        metavar="XBOX_DISC",
        help="read the scripts of your Xbox disc (image, XISO or folder) instead of the PS2 disc",
    )
    decompile.add_argument(
        "--luac",
        type=Path,
        metavar="PATH",
        help="the compiler for --roundtrip: a stock Lua 4.0 luac (default: luac_program) or Coney's coney_luac",
    )
    decompile.add_argument(
        "--hashes",
        type=Path,
        metavar="FILE",
        help="with --all: the list of decompile hashes to check (default: research/lua-decompile-hashes.toml)",
    )
    decompile.add_argument(
        "--update-hashes",
        action="store_true",
        help="with --all: add the scripts the list lacks (another disc's); refuses to change a listed hash",
    )
    decompile.add_argument(
        "--new-form",
        action="store_true",
        help="with --all: on purpose, rewrite the list from this run as the next form (the output changed)",
    )
    decompile.add_argument(
        "--roundtrip",
        action="store_true",
        help="compile each output with luac and compare its code and the natives it calls with the chunk's; prints "
        "counts and hashes only; exit 1 when the code differs",
    )
    compare = commands.add_parser(
        "compare",
        help="compare two `decompile --all` folders script by script (the PS2 and Xbox discs'); counts and names only",
    )
    compare.add_argument("first", type=Path, metavar="DIR_A", help="the first decompile folder (the PS2 disc's)")
    compare.add_argument("second", type=Path, metavar="DIR_B", help="the second decompile folder (the Xbox disc's)")
    compare.add_argument(
        "--list", action="store_true", help="also one line per script: result, chunk hash, its name in each folder"
    )


def run(args: argparse.Namespace) -> int:
    """Dispatch a `lua` command."""
    if args.command == "compare":
        return lua_compare.run(args.first, args.second, listing=args.list)
    root = find_repo_root(Path.cwd())
    paths: list[str] = args.paths
    if args.chunks is not None and args.xbox is not None:
        raise ConfigError("--chunks and --xbox-disc each name where the chunks come from; give one")
    other_source = args.chunks is not None or args.xbox is not None
    if args.all:
        if len(paths) > (0 if other_source else 1):
            raise ConfigError("expected `lua decompile [DISC] --all` (no DISC with --chunks or --xbox-disc)")
        options = lua_decompile.Options(
            chunks=args.chunks,
            xbox=args.xbox,
            out=args.out,
            luac=args.luac,
            roundtrip=args.roundtrip,
            hashes=args.hashes,
            update_hashes=args.update_hashes,
            new_form=args.new_form,
        )
        return lua_decompile.run_all(root, paths[0] if paths else None, options)
    if args.out is not None:
        raise ConfigError("--out goes with --all; one chunk is written to the OUT_DIR given")
    if args.hashes is not None or args.update_hashes or args.new_form:
        raise ConfigError("--hashes, --update-hashes and --new-form go with --all")
    if len(paths) not in ((2,) if other_source else (2, 3)):
        raise ConfigError(
            "expected `lua decompile [DISC] NAME OUT_DIR` (no DISC with --chunks or --xbox-disc), or --all"
        )
    disc = paths[0] if len(paths) == 3 else None
    options = lua_decompile.Options(chunks=args.chunks, xbox=args.xbox, luac=args.luac, roundtrip=args.roundtrip)
    return lua_decompile.run_one(root, disc, paths[-2], Path(paths[-1]), options)
