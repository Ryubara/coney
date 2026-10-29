# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile Lua 4.0 source with a stock `luac` (built from Lua 4.0.1's source), or a program that takes its command
line (Coney's `coney_luac`), for the round trip.

The decompiler is checked by compiling its output and comparing the result with the original chunk
(`compare.py`). `luac` writes its chunk to a file, so the source and the chunk go through a temporary folder.
"""

from __future__ import annotations

import re
import subprocess
import tempfile
from pathlib import Path

# `luac` says "at line 7"; Coney's `coney_luac` says "file.lua:7: ..." as later versions of Lua do.
_LINE = re.compile(r"at line (\d+)|:(\d+): ")


class LuacError(Exception):
    """`luac` refused the source. `line` is the line it named (0 when none); the message is luac's own and may quote
    the source, so a report about the game's scripts prints only `line`."""

    def __init__(self, message: str) -> None:
        super().__init__(message)
        match = _LINE.search(message)
        self.line = int(match.group(1) or match.group(2)) if match else 0


def compile_source(source: str | bytes, luac: Path, strip: bool = False, timeout: float = 60) -> bytes:
    """The chunk `luac` makes of `source` (text is written as Latin-1, one byte per character); with `strip`, without
    its debug information (`luac -s`), as the game's chunks are."""
    data = source.encode("latin-1") if isinstance(source, str) else source
    with tempfile.TemporaryDirectory(prefix="lua4-") as folder:
        source_file = Path(folder) / "source.lua"
        chunk_file = Path(folder) / "source.out"
        source_file.write_bytes(data)
        try:
            done = subprocess.run(
                [str(luac), *(["-s"] if strip else []), "-o", str(chunk_file), str(source_file)],
                capture_output=True,
                text=True,
                timeout=timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise LuacError(f"could not run {luac}: {error}") from error
        if done.returncode != 0 or not chunk_file.is_file():
            raise LuacError((done.stderr or done.stdout).strip() or f"luac exited with {done.returncode}")
        return chunk_file.read_bytes()
