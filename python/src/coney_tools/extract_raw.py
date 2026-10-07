# SPDX-License-Identifier: GPL-3.0-or-later
"""The `raw` stage of `coney-tools extract`: what no decoder takes yet, as stored, each with a JSON description.

* `raw/<kind>/<name>` and `<name>.json` for each WAD entry of a kind with no decoder: the five old RenderWare files
  no code reads, and any entry of no known kind. The JSON holds the entry's index, hash, name, size and first words.
* `raw/resources/<shape>/<name>.res` and `.json` for each distinct resource of a shape with no decoder: the
  resource exactly as stored (its header and chunks), and its chunk list with each type's name.

As decoders arrive, kinds and shapes leave the RAW sets below; nothing is ever both decoded and raw.

Research: docs/research/formats/inventory.md (which formats are raw), docs/research/formats/wad-contents.md
"""

from __future__ import annotations

import struct
from typing import Any

from coney_tools import chunk_names, rw, scenes, wad_kinds
from coney_tools.extract_output import Output, Report
from coney_tools.extract_wad import (
    SHAPE_OTHER,
    Entry,
    Item,
)

KIND = "raw"
#: Entry kinds written raw (packs are lists of resources and come out as their resources).
RAW_KINDS = {
    wad_kinds.LEGACY_RW: "rws",
    wad_kinds.UNKNOWN: "bin",
}
#: Resource shapes written raw.
RAW_SHAPES = {SHAPE_OTHER}


def _words(data: bytes, count: int) -> list[int]:
    """The first `count` little-endian words (fewer when the data is short)."""
    count = min(count, len(data) // 4)
    return list(struct.unpack_from(f"<{count}I", data, 0))


def describe(entry: Entry) -> dict[str, Any]:
    """The first words of a raw entry (formats/renderware.md#older-streams has what is known of the old files)."""
    return {"words": [f"{w:08x}" for w in _words(entry.data, 4)]}


class RawStage:
    """Writes the raw kinds and shapes."""

    def __init__(self, output: Output) -> None:
        """Start the type."""
        self.output = output
        self.report = Report(KIND)
        output.start(KIND)

    def entry(self, entry: Entry) -> None:
        """Write an entry of a raw kind with its description."""
        extension = RAW_KINDS.get(entry.kind)
        if extension is None:
            return
        name = entry.name or f"{entry.record.hash:08x}.{extension}"
        path = self.output.write(KIND, f"raw/{entry.kind}/{name}", entry.data)
        record: dict[str, Any] = {
            "file": path,
            "index": entry.record.index,
            "hash": f"{entry.record.hash:08x}",
            "name": entry.name,
            "kind": entry.kind,
            "size": entry.record.size,
        }
        try:
            record["header"] = describe(entry)
        except (ValueError, struct.error, rw.RwError, scenes.SceneError) as error:
            self.report.problem(f"{entry.label()}: {error}")
        self.output.write_json(KIND, f"{path}.json", record)
        self.report.count(f"entries: {entry.kind}")

    def item(self, item: Item) -> None:
        """Write a resource of a raw shape with its chunk list."""
        if item.shape not in RAW_SHAPES:
            return
        path = self.output.write(KIND, f"raw/resources/{item.shape}/{item.label()}.res", item.resource_bytes())
        record = {
            "file": path,
            "hash": f"{item.resource.hash:08x}",
            "name": item.name,
            "shape": item.shape,
            "chunks": [
                {"type": c.type, "name": chunk_names.name(c.type), "size": c.size, "hash": f"{c.hash:08x}"}
                for c in item.resource.chunks
            ],
        }
        self.output.write_json(KIND, f"{path}.json", record)
        self.report.count(f"resources: {item.shape}")

    def finish(self) -> None:
        """Nothing to add."""
