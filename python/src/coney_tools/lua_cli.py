# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools lua ...`: the command line of the Lua 4.0 decompiler (`lua_decompile.py`).

* `lua decompile [DISC] NAME OUT_DIR` writes one chunk's decompile into OUT_DIR;
* `lua decompile [DISC] --all` writes every chunk, the guard's fingerprints and the index into the shared folder.

Both take `--chunks DIR` (a folder of extracted chunks instead of the disc), `--luac PATH` and `--roundtrip`; `--all`
also takes `--out DIR` and the frozen form's `--hashes FILE`, `--update-hashes` and `--new-form` (`lua_hashes.py`):
it checks every decompile's hash against the committed list. Guide: docs/guides/coney-tools-fidelity.md#lua.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from coney_tools import lua_decompile
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
    decompile.add_argument("--luac", type=Path, metavar="PATH", help="the stock Lua 4.0 luac (default: luac_program)")
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
        help="compile each output with luac and compare it with the chunk; prints counts only; exit 1 on a mismatch",
    )


def run(args: argparse.Namespace) -> int:
    """Dispatch a `lua` command."""
    root = find_repo_root(Path.cwd())
    paths: list[str] = args.paths
    if args.all:
        if len(paths) > (0 if args.chunks else 1):
            raise ConfigError("expected `lua decompile [DISC] --all` (no DISC with --chunks)")
        options = lua_decompile.Options(
            chunks=args.chunks,
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
    if len(paths) not in ((2,) if args.chunks else (2, 3)):
        raise ConfigError("expected `lua decompile [DISC] NAME OUT_DIR` (no DISC with --chunks), or --all")
    disc = paths[0] if len(paths) == 3 else None
    options = lua_decompile.Options(chunks=args.chunks, luac=args.luac, roundtrip=args.roundtrip)
    return lua_decompile.run_one(root, disc, paths[-2], Path(paths[-1]), options)
