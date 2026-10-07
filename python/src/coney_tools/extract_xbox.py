# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools extract ... --xbox <iso>`: the Xbox disc as the preferred source, the PS2 disc filling the gaps.

The PS2 stages write every asset first; then, per asset, the Xbox version replaces the file at the same path when the
rule for its kind says it is better, and the type's index records it (`"source": "xbox"`). So the folder has the same
files either way and nothing that reads it needs to know which disc an asset came from. The rules, kind by kind
(docs/research/xbox-assets.md#asset-kinds):

- **textures**: the matching Xbox texture when it is larger (xbox_match.replaces); the PS2 one otherwise.
- **movies**: `bik/<name>_hd.bik` when it is larger than the PS2 movie and as long (to half a second), since the
  subtitles are timed against the PS2 movie; the PS2 movie otherwise.
- **everything else**: PS2. The Xbox models, animations, levels and sound are the same data or the same
  recordings at no higher quality, and scripts, scenes, levels and tables are behaviour, for which the PS2 disc is
  the reference.

Research: docs/research/xbox-assets.md
"""

from __future__ import annotations

import io
import sys
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image

from coney_tools import movies, wad, xbox_match, xbox_texture_map
from coney_tools.extract_output import Output, Report, png_bytes
from coney_tools.xbox_source import XboxSource, ps2_world_level

#: How much longer or shorter (seconds) an Xbox movie may be than the PS2 one and still replace it.
MOVIE_SLACK = 0.5
#: The Xbox movie variant preferred: `_hd` is 1280 x 720 (16:9); `_w` and the plain name are 640 x 480.
MOVIE_VARIANT = "_hd"


def open_source(path: Path, index_path: Path | None = None) -> XboxSource:
    """Open the Xbox disc (an image, an XISO or a folder) and its resource index (see XboxSource)."""
    return XboxSource(path, index_path)


def upgrade_textures(output: Output, dictionaries: list[Any], xbox: XboxSource, report: Report) -> None:
    """Replace each PS2 texture the Xbox has a larger version of, in place, and note it in its index record.

    `dictionaries` are the textures stage's (`folder`, `source`, `resource_hash`, `textures`: records with `file`,
    `width`, `height`). A replaced record gets `"source": "xbox"`, `"xbox"` (where on the Xbox disc), the Xbox size
    as its `width` and `height`, and the PS2 size as `"ps2_size"`. Each replacement is also noted in
    `xbox.replacements`, for the texture map.
    """
    # World dictionaries come level by level so each level's Xbox textures are decoded once.
    ordered = sorted(dictionaries, key=lambda d: (_world_level(d) or "", d.source))
    for number, dictionary in enumerate(ordered):
        if number % 500 == 0:
            print(f"coney-tools: xbox textures: {number}/{len(ordered)} dictionaries", file=sys.stderr, flush=True)
        level = _world_level(dictionary)
        if level is not None:
            candidates = xbox.world_textures(level)
        elif dictionary.resource_hash is not None:
            candidates = xbox.resource_textures(dictionary.resource_hash)
        else:
            candidates = []
        positional = len(candidates) == len(dictionary.textures) and level is None
        for position, record in enumerate(dictionary.textures):
            if not record.get("file"):
                continue
            report.count("ps2 textures")
            if not candidates:
                report.count("ps2 kept: no xbox counterpart")
                continue
            ps2 = _read_png(output.root / record["file"])
            match = xbox_match.best_match(ps2, candidates, position if positional else None)
            if match is None or not match.matches:
                report.count("ps2 kept: no matching xbox texture")
                continue
            if not xbox_match.replaces(record["width"], record["height"], match):
                report.count("ps2 kept: xbox not larger")
                continue
            xbox_texture = match.candidate
            output.replace("textures", record["file"], png_bytes(xbox_texture.rgba))
            record["source"] = "xbox"
            record["xbox"] = xbox_texture.source
            record["ps2_size"] = [record["width"], record["height"]]
            record["width"], record["height"] = xbox_texture.width, xbox_texture.height
            report.count("xbox textures (larger)")
            noted = _replacement(dictionary, record["name"], xbox_texture)
            if noted is not None:
                xbox.replacements.append(noted)


def _replacement(dictionary: Any, name: str, xbox_texture: xbox_match.Candidate) -> xbox_texture_map.Replacement | None:
    """The texture map's record for one replaced texture; None when the dictionary or the Xbox texture has no key."""
    where = xbox_texture.location
    if where is None:
        return None
    if dictionary.resource_hash is not None:
        kind, key = xbox_texture_map.RESOURCE, dictionary.resource_hash
    elif getattr(dictionary, "entry_hash", None) is not None:
        kind, key = xbox_texture_map.WORLD, dictionary.entry_hash
    else:
        return None
    return xbox_texture_map.Replacement(
        key,
        xbox_texture_map.texture_name_hash(name),
        kind,
        where.number,
        where.volume,
        where.offset,
        where.size,
        xbox_texture.width,
        xbox_texture.height,
    )


def _world_level(dictionary: Any) -> str | None:
    """The level of a streamed-world dictionary (`level99s_ms3.sec` -> `level99`), None for a resource's."""
    return ps2_world_level(dictionary.source) if dictionary.resource_hash is None else None


def _read_png(path: Path) -> np.ndarray[Any, np.dtype[np.uint8]]:
    """A written PNG as RGBA."""
    with Image.open(io.BytesIO(path.read_bytes())) as image:
        return np.asarray(image.convert("RGBA"), dtype=np.uint8)


def movie_choice(name: str, ps2: movies.BinkHeader, xbox: XboxSource) -> tuple[str, movies.BinkHeader] | None:
    """The Xbox movie to use for the PS2 movie `name` (`L9_IN`), as (path on the Xbox disc, its header), or None.

    The rule: `<name>_hd.bik` when it has more pixels than the PS2 movie and lasts as long (to MOVIE_SLACK).
    """
    path = xbox.movies().get(name.lower() + MOVIE_VARIANT)
    if path is None:
        return None
    size = xbox.disc.size(path)
    with xbox.disc.open(path) as handle:
        head = handle.read(64)
        handle.seek(0)
        head = handle.read(movies.header_size(head))
    try:
        header = movies.parse_header(head, size, path)
    except movies.MovieError:
        return None
    if header.width * header.height <= ps2.width * ps2.height:
        return None
    if abs(header.seconds - ps2.seconds) > MOVIE_SLACK:
        return None
    return path, header


def copy_movie(xbox: XboxSource, path: str, output: Output, wanted: str) -> str:
    """Copy one Xbox movie into the folder at `wanted`; returns the path written."""
    with xbox.disc.open(path) as handle:
        pieces = wad.read_chunks(handle, xbox.disc.size(path), 8 << 20)
        return output.write_stream("movies", wanted, pieces)
