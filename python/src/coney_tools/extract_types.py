# SPDX-License-Identifier: GPL-3.0-or-later
"""The asset types of `coney-tools extract`, kept apart from the extractor so the command line can list them without
loading NumPy.

Research: docs/research/formats/inventory.md
"""

from __future__ import annotations

#: Every asset type, in the order the summary prints them, with what it writes.
TYPES: dict[str, str] = {
    "disc": "the disc's other files (executable, IOP modules and image, SYSTEM.CNF), copied, with disc/files.json",
    "movies": "PSS/*.BIK, copied as .bik, with movies/index.json (header values)",
    "audio": "streamed sounds, bank samples and music as WAV, with the sound, music and class tables as JSON",
    "scripts": "compiled Lua 4.0 chunks as stored, with scripts/index.json",
    "textures": "every texture of every dictionary as PNG, with textures/index.json",
    "index": "index/wad.json: every WAD entry's kind and name, every resource's chunks",
    "raw": "every WAD entry or resource no decoder takes yet, as stored, with a JSON description",
}
