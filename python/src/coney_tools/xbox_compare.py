# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools xbox compare PS2_DISC XBOX_DISC`: every chunk and entry of the PS2 archive looked up on the Xbox disc.

Made to answer whether an Xbox disc alone holds the data the game's simulation reads (collision, levels, objects,
paths, scenes, scripts, tables, animations, text) or only something like it. Both archives use the same chunk
container ([Formats, kind by kind](docs/research/xbox-assets.md#formats-kind-by-kind)) and the same resource hashes,
so a PS2 chunk is found on the Xbox by its resource's hash and its chunk type, and compared by SHA-1:

* **same**: the Xbox holds a chunk of that resource and type with the same bytes;
* **same size** / **other size**: it holds the resource and type, but no chunk with these bytes;
* **no such chunk**: it holds the resource, without a chunk of this type;
* **no such resource**: no resource with this hash.

Packs repeat shared resources, so each distinct (resource, type, bytes) counts once. Entries that are no container
(scripts, scene records, object lists, streamed world, sound) are compared whole, by SHA-1, per kind. The output is
counts only.

Research: docs/research/xbox-assets.md#either-disc. Guide: docs/guides/coney-tools-disc.md#xbox.
"""

from __future__ import annotations

import hashlib
import sys
from collections import Counter, defaultdict
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import chunk_names, wad, xbox
from coney_tools.disc import Disc
from coney_tools.xdvdfs import XboxDisc

#: The PS2's entries that the Xbox's tests leave unclassified (RenderWare streams, sound banks) are counted as this.
OTHER = "other (RenderWare streams, sound banks)"
#: The columns of a chunk row, in order.
COLUMNS = ("same", "same size", "other size", "no such chunk", "no such resource")


@dataclass
class Side:
    """What one archive holds: every chunk's (size, SHA-1) by (resource hash, chunk type), and the SHA-1s of the
    entries that are no container, by kind."""

    chunks: dict[tuple[int, int], set[tuple[int, bytes]]] = field(default_factory=lambda: defaultdict(set))
    resources: set[int] = field(default_factory=set)
    entries: dict[str, set[bytes]] = field(default_factory=lambda: defaultdict(set))
    entry_count: Counter[str] = field(default_factory=Counter)

    def add(self, data: bytes) -> None:
        """Add one archive entry: its chunks when it is a pack or a resource, else its SHA-1 under its kind."""
        head = data[:64]
        kind, container, _ = xbox.classify(head, len(data), lambda: data)
        if container is not None:
            view = memoryview(data)
            for resource in container.resources:
                self.resources.add(resource.hash)
                for chunk in resource.chunks:
                    digest = hashlib.sha1(view[chunk.offset : chunk.offset + chunk.size]).digest()
                    self.chunks[(resource.hash, chunk.type)].add((chunk.size, digest))
            return
        kind = OTHER if kind == xbox.UNKNOWN else kind
        self.entry_count[kind] += 1
        self.entries[kind].add(hashlib.sha1(data).digest())


def read_side(entries: Iterable[bytes]) -> Side:
    """A Side from every entry's bytes."""
    side = Side()
    for data in entries:
        side.add(data)
    return side


@dataclass
class Comparison:
    """Per chunk type, the PS2's distinct chunks in each column; per entry kind, the PS2's distinct entries and how
    many of them the Xbox holds; the resources each side alone has."""

    chunk_rows: dict[int, Counter[str]]
    entry_rows: dict[str, tuple[int, int, int]]  # kind -> (PS2 entries, PS2 distinct, of those on the Xbox)
    ps2_only_resources: int
    xbox_only_resources: int


def compare(ps2: Side, other: Side) -> Comparison:
    """Look each PS2 chunk and entry up on the Xbox side `other`."""
    rows: dict[int, Counter[str]] = defaultdict(Counter)
    for (resource, chunk_type), found in ps2.chunks.items():
        theirs = other.chunks.get((resource, chunk_type))
        for size, digest in found:
            if theirs is not None and (size, digest) in theirs:
                column = "same"
            elif theirs is not None:
                column = "same size" if any(their_size == size for their_size, _ in theirs) else "other size"
            elif resource in other.resources:
                column = "no such chunk"
            else:
                column = "no such resource"
            rows[chunk_type][column] += 1
    entry_rows = {
        kind: (ps2.entry_count[kind], len(digests), len(digests & other.entries.get(kind, set())))
        for kind, digests in ps2.entries.items()
    }
    return Comparison(
        dict(rows), entry_rows, len(ps2.resources - other.resources), len(other.resources - ps2.resources)
    )


def report(result: Comparison) -> list[str]:
    """The comparison as lines of counts."""
    lines = ["chunks by type: the PS2's distinct chunks, then where each is on the Xbox"]
    lines.append(f"  {'type':<30} {'PS2':>7} " + " ".join(f"{column:>16}" for column in COLUMNS))
    totals: Counter[str] = Counter()
    for chunk_type in sorted(result.chunk_rows):
        row = result.chunk_rows[chunk_type]
        totals.update(row)
        label = f"{chunk_type:#04x} {chunk_names.name(chunk_type)}"
        cells = " ".join(f"{row[column]:16d}" for column in COLUMNS)
        lines.append(f"  {label:<30} {sum(row.values()):7d} {cells}")
    cells = " ".join(f"{totals[column]:16d}" for column in COLUMNS)
    lines.append(f"  {'all':<30} {sum(totals.values()):7d} {cells}")
    lines.append("entries that are no container, by kind: PS2 entries, distinct, of those on the Xbox byte for byte")
    for kind in sorted(result.entry_rows):
        count, distinct, same = result.entry_rows[kind]
        lines.append(f"  {kind:<40} {count:7d} {distinct:7d} {same:7d}")
    lines.append(
        f"resources only on the PS2: {result.ps2_only_resources}; only on the Xbox: {result.xbox_only_resources}"
    )
    return lines


def ps2_entries(disc: Disc, progress: bool = False) -> Iterator[bytes]:
    """Every WARRIORS.WAD entry's bytes, in index order."""
    entries = wad.load_entries(disc)
    with disc.open(wad.WAD_FILE) as handle:
        for entry in entries:
            handle.seek(entry.offset)
            yield handle.read(entry.size)
            if progress and entry.index % 1000 == 999:
                print(f"  read {entry.index + 1}/{len(entries)} PS2 entries", file=sys.stderr)


def xbox_entries(disc: XboxDisc, progress: bool = False) -> Iterator[bytes]:
    """Every XBoxWad.idx entry's bytes, in index order."""
    index = xbox.load_index(disc)
    for entry, handle in xbox.iter_entries(disc, index):
        yield handle.read(entry.size)
        if progress and entry.index % 1000 == 999:
            print(f"  read {entry.index + 1}/{len(index.entries)} Xbox entries", file=sys.stderr)


def run(ps2_disc: str, xbox_disc: str) -> int:
    """`xbox compare PS2_DISC XBOX_DISC`: print where each PS2 chunk and entry is on the Xbox."""
    ps2 = read_side(ps2_entries(Disc(Path(ps2_disc)), progress=True))
    other = read_side(xbox_entries(XboxDisc(Path(xbox_disc)), progress=True))
    for line in report(compare(ps2, other)):
        print(line)
    return 0
