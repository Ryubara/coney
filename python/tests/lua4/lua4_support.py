# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the lua4 decompiler's tests: the synthetic fixtures and the `luac` that checks them.

The fixtures are our own Lua 4.0 source (`fixtures/<name>.lua`), never a game script. Each is committed with the
chunks a stock Lua 4.0.1 `luac` made of it, with and without debug information (`<name>.luac`, `<name>.s.luac`),
and with the decompiler's output for each (`<name>.expected.lua`, `<name>.s.expected.lua`), so the tests run
without `luac`. When `luac` is found the tests also compile each source and each expected output and compare the
code (the round trip). To rewrite the chunks and expected outputs after a change, run with `LUA4_REGENERATE=1`
(it needs `luac`): docs/guides/coney-tools.md#lua.
"""

from __future__ import annotations

import os
import shutil
from pathlib import Path

FIXTURES = Path(__file__).parent / "fixtures"
#: The fixture sources, by name.
NAMES = sorted(path.name.removesuffix(".lua") for path in FIXTURES.glob("*.lua") if path.name.count(".") == 1)
#: The two ways each source is compiled: with debug information ("") and stripped (".s", as the game's chunks are).
VARIANTS = ("", ".s")


def find_luac() -> Path | None:
    """The `luac` for the round trip: `LUA4_LUAC`, else coney.local.toml's `luac_program` (or its default), else
    a `luac4` on PATH; None when there is none."""
    given = os.environ.get("LUA4_LUAC")
    if given:
        return Path(given)
    try:
        from coney_tools import lua_decompile  # Coney's setting; the lua4 package itself knows nothing of it

        found = lua_decompile.luac_program(Path(__file__).resolve().parents[3], required=False)
    except Exception:  # no Coney configuration: fall back to PATH
        found = None
    if found is not None and found.is_file():
        return found
    on_path = shutil.which("luac4")
    return Path(on_path) if on_path else None


def regenerate() -> bool:
    """Whether this run rewrites the fixtures' chunks and expected outputs."""
    return os.environ.get("LUA4_REGENERATE") == "1"
