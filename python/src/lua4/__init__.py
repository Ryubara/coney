# SPDX-License-Identifier: GPL-3.0-or-later
"""A decompiler for Lua 4.0 bytecode: compiled chunks back to readable Lua that compiles to the same code.

The package stands alone (it knows nothing of the program that uses it):

* `load` reads a chunk (`chunk.py`);
* `decompile` turns it into source (`decompiler.py` reads statements, `render.py` writes them);
* `compare` checks that two chunks hold the same instructions and constants in every function (`compare.py`);
* `compile_source` runs a stock Lua 4.0 `luac` for the round trip (`luac.py`).

The opcode meanings and the patterns each statement compiles to come from Lua 4.0.1's own source (MIT licence,
Lua.org, PUC-Rio): `lopcodes.h`, `lcode.c`, `lparser.c`. Research: docs/research/formats/lua-bytecode.md.
"""

from __future__ import annotations

from lua4.chunk import ChunkError, LocVar, Proto, load
from lua4.compare import Difference, compare
from lua4.decompiler import decompile_function
from lua4.luac import LuacError, compile_source
from lua4.render import render


def decompile(chunk: bytes | Proto) -> str:
    """The Lua source of a compiled chunk. A function that cannot be read comes out as a comment saying why,
    inside an otherwise complete file; `failures` lists them."""
    main = chunk if isinstance(chunk, Proto) else load(chunk)
    return render(decompile_function(main))


__all__ = [
    "ChunkError",
    "Difference",
    "LocVar",
    "LuacError",
    "Proto",
    "compare",
    "compile_source",
    "decompile",
    "decompile_function",
    "load",
    "render",
]
