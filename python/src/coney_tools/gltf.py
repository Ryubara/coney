# SPDX-License-Identifier: GPL-3.0-or-later
"""A small glTF 2.0 writer: nodes, meshes, skins, materials, textures and animations into a `.gltf` and a `.bin`.

Only what the extractor needs, and deterministic: the JSON keys and the buffer come out in the order things were
added. Accessors are tightly packed little-endian arrays, each 4-byte aligned in the buffer.
"""

from __future__ import annotations

import json
from typing import Any

import numpy as np
import numpy.typing as npt

FLOAT = 5126
UNSIGNED_SHORT = 5123
UNSIGNED_INT = 5125
UNSIGNED_BYTE = 5121
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963
_TYPES = {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4", 16: "MAT4"}
_COMPONENTS = {np.dtype("float32"): FLOAT, np.dtype("uint16"): UNSIGNED_SHORT, np.dtype("uint32"): UNSIGNED_INT,
               np.dtype("uint8"): UNSIGNED_BYTE}  # fmt: skip
#: RenderWare filter modes to glTF (mag, min) filters: nearest, linear and the mipmapped modes.
_FILTERS = {1: (9728, 9728), 2: (9729, 9729), 3: (9728, 9984), 4: (9729, 9985), 5: (9728, 9986), 6: (9729, 9987)}
#: RenderWare addressing to glTF wrap modes: wrap, mirror, clamp (border has no glTF equivalent: clamp).
_WRAPS = {1: 10497, 2: 33648, 3: 33071, 4: 33071}


class Document:
    """One glTF asset under construction."""

    def __init__(self, generator: str = "coney-tools extract") -> None:
        """An empty asset with one scene."""
        self.buffer = bytearray()
        self.json: dict[str, Any] = {
            "asset": {"version": "2.0", "generator": generator},
            "scene": 0,
            "scenes": [{"nodes": []}],
        }

    def _list(self, key: str) -> list[Any]:
        """The top-level array `key`, created when first used."""
        return self.json.setdefault(key, [])  # type: ignore[no-any-return]

    def add(self, key: str, value: dict[str, Any]) -> int:
        """Append `value` to the top-level array `key`; returns its index."""
        items = self._list(key)
        items.append(value)
        return len(items) - 1

    def accessor(self, values: npt.NDArray[Any], target: int | None = None, bounds: bool = False,
                 normalized: bool = False) -> int:  # fmt: skip
        """Put an array (rows of 1-4 or 16 components) in the buffer and describe it; returns the accessor index."""
        array = np.ascontiguousarray(values)
        if array.dtype == np.float64:
            array = array.astype(np.float32)
        components = 1 if array.ndim == 1 else array.shape[1]
        while len(self.buffer) % 4:
            self.buffer.append(0)
        view: dict[str, Any] = {"buffer": 0, "byteOffset": len(self.buffer), "byteLength": array.nbytes}
        if target is not None:
            view["target"] = target
        self.buffer += array.astype(array.dtype.newbyteorder("<")).tobytes()
        accessor: dict[str, Any] = {
            "bufferView": self.add("bufferViews", view),
            "componentType": _COMPONENTS[array.dtype],
            "count": len(array),
            "type": _TYPES[components],
        }
        if normalized:
            accessor["normalized"] = True
        if bounds and len(array):
            flat = array.reshape(len(array), components)
            accessor["min"] = [float(v) for v in flat.min(axis=0)]
            accessor["max"] = [float(v) for v in flat.max(axis=0)]
        return self.add("accessors", accessor)

    def sampler(self, filter_mode: int, wrap_u: int, wrap_v: int) -> int:
        """A sampler from RenderWare's filter mode and addressing; equal samplers are shared."""
        mag, min_ = _FILTERS.get(filter_mode, (9729, 9987))
        value = {"magFilter": mag, "minFilter": min_, "wrapS": _WRAPS.get(wrap_u, 10497),
                 "wrapT": _WRAPS.get(wrap_v or wrap_u, 10497)}  # fmt: skip
        samplers = self._list("samplers")
        if value in samplers:
            return samplers.index(value)
        return self.add("samplers", value)

    def texture(self, uri: str, sampler: int) -> int:
        """A texture of an image file (relative URI); equal ones are shared."""
        images = self._list("images")
        image = {"uri": uri}
        source = images.index(image) if image in images else self.add("images", image)
        value = {"sampler": sampler, "source": source}
        textures = self._list("textures")
        return textures.index(value) if value in textures else self.add("textures", value)

    def dumps(self, bin_name: str) -> bytes:
        """The `.gltf` JSON, its buffer pointing at `bin_name` beside it."""
        while len(self.buffer) % 4:
            self.buffer.append(0)
        if self.buffer:
            self.json["buffers"] = [{"uri": bin_name, "byteLength": len(self.buffer)}]
        return (json.dumps(self.json, indent=1, ensure_ascii=False) + "\n").encode("utf-8")


def matrix(
    right: tuple[float, ...], up: tuple[float, ...], at: tuple[float, ...], position: tuple[float, ...]
) -> list[float]:
    """A RenderWare matrix (rows right, up, at, position) as glTF's column-major 16 numbers."""
    return [*right[:3], 0.0, *up[:3], 0.0, *at[:3], 0.0, *position[:3], 1.0]
