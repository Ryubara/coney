# SPDX-License-Identifier: GPL-3.0-or-later
"""The `data` stage of `coney-tools extract`: the game's lists and the small files, as JSON, PNG and glTF.

* `data/global/<list>.json` from the global resource (`warriors.glr`): the Character List (`0x44`), the Object List
  (`0x46`), the Anim List (`0x4E`), the Dependency List (`0x4F`) and the sprite sheet table (`0x4D`), hashes in hex
  with their names where recovered. The sound tables of the same resource are the `audio` type's. A second, different
  global resource is written as `data/global~2/`.
* `data/objects/<level>.json`: each level's placed objects (`<level>_objs.txt`), one record per line.
* `data/fonts/<name>.json` and `.png`: the unused `METRICS1` font metrics and its bitmap.
* `data/icon/`: the memory card icon as glTF (shape 0, the other shapes as morph targets), its texture as PNG and
  `icon.json` (the animation and header values).

Research: docs/research/formats/global-lists.md, docs/research/objects.md#objs-file,
docs/research/gui.md#the-metrics1-file, docs/research/formats/memory-card-icon.md
"""

from __future__ import annotations

import io
import re
import struct
from collections.abc import Callable
from typing import Any

import numpy as np
from PIL import Image

from coney_tools import gltf, ps2icon, wad_kinds
from coney_tools.extract_output import Output, Report, png_bytes
from coney_tools.extract_wad import SHAPE_GLOBAL, Entry, Item

KIND = "data"
_LINE = re.compile(
    r"^\{(?P<name>\S+)\s*\{(?P<pos>[^}]*)\},\s*\{(?P<rot>[^}]*)\},\s*(?P<a>-?\d+),\s*(?P<zone>-?\d+),\s*"
    r"(?P<flags>-?\d+),\s*(?P<tint>[0-9a-fA-F]+),\s*(?P<flag_name>\S+)\s*\)\s*$"
)
_METRIC = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)")


class DataError(ValueError):
    """A list or file whose layout does not hold together."""


def _records(data: bytes, start: int, size: int, count: int, layout: str) -> list[tuple[int, ...]]:
    """`count` records of `size` bytes from `start`, each unpacked with `layout`."""
    if start + size * count > len(data):
        raise DataError(f"{count} records of {size} bytes do not fit in {len(data)} bytes")
    return [struct.unpack_from(layout, data, start + size * i) for i in range(count)]


def decode_lists(chunks: dict[int, bytes], name: Callable[[int], str | None]) -> dict[str, Any]:
    """The global resource's lists, by file name (formats/global-lists.md)."""

    def ref(value: int) -> dict[str, Any] | None:
        """A hash with its recovered name; None for 0."""
        return {"hash": f"{value:08x}", "name": name(value)} if value else None

    result: dict[str, Any] = {}
    if 0x44 in chunks:
        data = chunks[0x44]
        (count,) = struct.unpack_from("<I", data, 0)
        result["character_list"] = [
            {"model_name": ref(a), "data": ref(b), "model": ref(c), "textures": ref(d), "sizes": [e, f, g, h]}
            for a, b, c, d, e, f, g, h in _records(data, 16, 32, count, "<8I")
        ]
    if 0x46 in chunks:
        data = chunks[0x46]
        (count,) = struct.unpack_from("<I", data, 0)
        result["object_list"] = [
            {
                "type": ref(a),
                "damaged": ref(b),
                "model": ref(c),
                "textures": ref(d),
                "textures_2": ref(e),
                "sizes": [f, g, h, i],
            }
            for a, b, c, d, e, f, g, h, i in _records(data, 16, 36, count, "<9I")
        ]
    if 0x4E in chunks:
        data = chunks[0x4E]
        (count,) = struct.unpack_from("<I", data, 0)
        result["anim_list"] = [{"clip": ref(h), "size": s} for h, s in _records(data, 4, 8, count, "<II")]
    if 0x4F in chunks:
        data = chunks[0x4F]
        (count,) = struct.unpack_from("<I", data, 0)
        result["dependency_list"] = [
            {"group": ref(g), "resource": ref(r), "state": s, "loaded": loaded}
            for g, r, s, loaded in _records(data, 16, 12, count, "<IIHH")
        ]
    if 0x4D in chunks:
        data = chunks[0x4D]
        (count,) = struct.unpack_from("<I", data, 0)
        result["sprite_sheets"] = [{"size": s, "sheet": ref(h)} for s, h in _records(data, 4, 8, count, "<II")]
    return result


def decode_objects(text: str) -> list[dict[str, Any]]:
    """A level's placed objects (`_objs.txt`): a count, then one line per object or particle emitter."""
    lines = [line for line in text.splitlines() if line.strip()]
    if not lines:
        raise DataError("an empty object list")
    count = int(lines[0].split()[0])
    result = []
    for line in lines[1:]:
        match = _LINE.match(line.strip())
        if match is None:
            raise DataError(f"a line that does not parse: {line.strip()[:60]!r}")
        flag_name = match["flag_name"]
        result.append(
            {
                "name": match["name"],
                "emitter": match["name"].startswith("part"),
                "position": [float(v) for v in match["pos"].split(",")],
                "rotation": [float(v) for v in match["rot"].split(",")],
                "value": int(match["a"]),
                "zone": int(match["zone"]),
                "flags": int(match["flags"]),
                "tint": match["tint"].lower(),
                "flag_name": None if flag_name == "nil" else flag_name,
            }
        )
    if len(result) != count:
        raise DataError(f"the count says {count} objects, the file holds {len(result)}")
    return result


def decode_metrics(text: str) -> dict[str, Any]:
    """The `METRICS1` file: its bitmaps, its number and each character's pixel rectangle."""
    lines = text.splitlines()
    if not lines or lines[0].strip() != "METRICS1":
        raise DataError("not a METRICS1 file")
    characters = []
    for line in lines[3:]:
        match = _METRIC.match(line)
        if match:
            code, x0, y0, x1, y1 = (int(v) for v in match.groups())
            characters.append({"code": code, "rectangle": [x0, y0, x1, y1]})
    return {"bitmaps": lines[1].split(), "value": int(lines[2]), "characters": characters}


def icon_document(icon: ps2icon.Icon, texture_uri: str) -> gltf.Document:
    """The icon as glTF: a triangle list of shape 0, the other shapes as morph targets, the texture."""
    document = gltf.Document()
    attributes = {
        "POSITION": document.accessor(icon.shapes[0], gltf.ARRAY_BUFFER, bounds=True),
        "NORMAL": document.accessor(_unit(icon.normals), gltf.ARRAY_BUFFER),
        "TEXCOORD_0": document.accessor(icon.uv, gltf.ARRAY_BUFFER),
        "COLOR_0": document.accessor(np.minimum(icon.colours / 128.0, 1.0).astype(np.float32), gltf.ARRAY_BUFFER),
    }
    targets = [
        {"POSITION": document.accessor(shape - icon.shapes[0], gltf.ARRAY_BUFFER, bounds=True)}
        for shape in icon.shapes[1:]
    ]
    sampler = document.sampler(2, 1, 1)
    material = document.add(
        "materials",
        {
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": document.texture(texture_uri, sampler)},
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "doubleSided": True,
        },
    )
    primitive: dict[str, Any] = {"attributes": attributes, "material": material, "mode": 4}
    mesh_value: dict[str, Any] = {"name": "icon", "primitives": [primitive]}
    if targets:
        primitive["targets"] = targets
        mesh_value["weights"] = [0.0] * len(targets)
    mesh = document.add("meshes", mesh_value)
    document.json["scenes"][0]["nodes"] = [document.add("nodes", {"name": "icon", "mesh": mesh})]
    return document


def _unit(vectors: np.ndarray[Any, Any]) -> np.ndarray[Any, Any]:
    """Vectors scaled to length 1 (zero ones point up)."""
    lengths = np.linalg.norm(vectors, axis=1, keepdims=True)
    return np.where(lengths > 0, vectors / np.where(lengths > 0, lengths, 1), [0.0, 1.0, 0.0]).astype(np.float32)


class DataStage:
    """Writes the global lists, the object lists, the font files and the icon."""

    def __init__(self, output: Output, name: Callable[[int], str | None]) -> None:
        """Start the type; `name` gives a hash's recovered name."""
        self.output = output
        self.name = name
        self.report = Report(KIND)
        output.start(KIND)

    def entry(self, entry: Entry) -> None:
        """An object list, the font files or the icon."""
        label = entry.label()
        try:
            if entry.kind == wad_kinds.OBJECT_LIST:
                objects = decode_objects(entry.data.rstrip(b"\0").decode("latin-1"))
                stem = label.removesuffix(".txt").removesuffix("_objs")
                self.output.write_json(KIND, f"data/objects/{stem}.json", {"source": label, "objects": objects})
                self.report.count("object lists")
            elif entry.kind == wad_kinds.METRICS:
                metrics = decode_metrics(entry.data.rstrip(b"\0").decode("latin-1"))
                stem = metrics["bitmaps"][0].removesuffix(".bmp") if metrics["bitmaps"] else label
                self.output.write_json(KIND, f"data/fonts/{stem}.json", {"source": label, **metrics})
                self.report.count("font metrics")
            elif entry.kind == wad_kinds.BITMAP:
                with Image.open(io.BytesIO(entry.data)) as image:
                    pixels = np.asarray(image.convert("RGBA"))
                self.output.write(KIND, f"data/fonts/{label.removesuffix('.bmp')}.png", png_bytes(pixels))
                self.report.count("bitmaps")
            elif entry.kind == wad_kinds.ICON:
                self._icon(entry)
        except (DataError, ps2icon.IconError, ValueError, struct.error, OSError) as error:
            self.report.problem(f"{label}: {error}")

    def _icon(self, entry: Entry) -> None:
        """The memory card icon's model, texture and animation."""
        icon = ps2icon.parse(entry.data)
        stem = entry.label().removesuffix(".ico")
        texture = self.output.write(KIND, f"data/icon/{stem}.png", png_bytes(icon.texture))
        document = icon_document(icon, texture.rsplit("/", 1)[-1])
        bin_path = self.output.claim(KIND, f"data/icon/{stem}.bin")
        self.output.write_claimed(KIND, bin_path, bytes(document.buffer))
        model = self.output.write(KIND, f"data/icon/{stem}.gltf", document.dumps(bin_path.rsplit("/", 1)[-1]))
        record = {
            "source": entry.label(),
            "texture_type": icon.texture_type,
            "shapes": len(icon.shapes),
            "vertices": len(icon.uv),
            "animation": icon.animation,
            "files": [model, texture],
        }
        self.output.write_json(KIND, f"data/icon/{stem}.json", record)
        self.report.count("icons")

    def item(self, item: Item) -> None:
        """The global resource's lists."""
        if item.shape != SHAPE_GLOBAL:
            return
        chunks = {c.type: item.chunk_bytes(i) for i, c in enumerate(item.resource.chunks)}
        try:
            lists = decode_lists(chunks, self.name)
        except (DataError, struct.error) as error:
            self.report.problem(f"{item.label()}: {error}")
            return
        folder = "global" if item.variant == 1 else f"global~{item.variant}"
        for list_name, records in lists.items():
            self.output.write_json(
                KIND, f"data/{folder}/{list_name}.json", {"source": item.label(), "records": records}
            )
            self.report.count("global lists")

    def finish(self) -> None:
        """Nothing is held back."""
