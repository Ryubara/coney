# SPDX-License-Identifier: GPL-3.0-or-later
"""Patched copies of PCSX2 save states: named edits to a state's EE memory, each checked against the bytes it
replaces.

A PCSX2 save state (`.p2s`) is a zip whose `eeMemory.bin` is the Emotion Engine's 32 MB of RAM, so an address below
`0x02000000` is its offset in that file. A code edit has to be in RAM before the recompiler first compiles the block,
which is why it is made in a copy of the state and PCSX2 is started on that copy
(docs/guides/research-workflow.md#driving-pcsx2). The edits are declared in a TOML file
(`research/traces/patches.toml`):

```toml
[patch.scripted-pad]
description = "what the patch does"
research = "docs/guides/research-workflow.md#driving-pcsx2"
edits = [
  { address = 0x00149dd4, original = [0x92220002], replacement = [0x92220022] },
  { address = 0x005de3aa, bytes = "ff ff 80 80 80 80" },
]
```

`original` and `replacement` are little-endian 32-bit words; a copy is refused when the state does not hold
`original` there, which catches a state of another version or a patch at the wrong address. `bytes` writes data
without a check (spare memory with no known content).

A repacked copy (`repack`) has the same contents with plain deflate instead of zstd, for readers that cannot read
zstd, such as Ghidra's.

The source state is only read: copies go wherever the caller says, never over the source and never into PCSX2's
`sstates/` folder, where the quick-save slots live.
"""

from __future__ import annotations

import tomllib
import zipfile
from dataclasses import dataclass
from pathlib import Path

EE_MEMORY = "eeMemory.bin"
EE_MEMORY_SIZE = 32 * 1024 * 1024
#: The zip method PCSX2 compresses its entries with; Python reads it from 3.14.
ZIP_ZSTANDARD = 93
#: The PCSX2 serial and CRC of the NTSC-U disc, which name its quick-save slots.
SLOT_NAME = "SLUS-21215 (B99A75DE).{slot:02d}.p2s"


class StateError(Exception):
    """A patch file or a state that cannot be used; the message names the file and the problem."""


@dataclass(frozen=True)
class Edit:
    """One edit: `data` written at `address`, after checking that `expected` is there (when given)."""

    address: int
    data: bytes
    expected: bytes | None


@dataclass(frozen=True)
class Patch:
    """A named group of edits, with what it does and the research page it comes from."""

    name: str
    description: str
    research: str
    edits: tuple[Edit, ...]


def _words(value: object, where: str) -> bytes:
    """A TOML list of 32-bit words as little-endian bytes. Raises StateError for anything else."""
    if not isinstance(value, list) or not value or not all(isinstance(w, int) and 0 <= w <= 0xFFFFFFFF for w in value):
        raise StateError(f"{where}: must be a non-empty list of 32-bit words")
    return b"".join(word.to_bytes(4, "little") for word in value)


def _edit(raw: object, where: str) -> Edit:
    """One edit from its TOML table."""
    if not isinstance(raw, dict) or not isinstance(raw.get("address"), int):
        raise StateError(f"{where}: an edit needs an integer address")
    unknown = set(raw) - {"address", "original", "replacement", "bytes"}
    if unknown:
        raise StateError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    address = raw["address"]
    if "bytes" in raw:
        if "original" in raw or "replacement" in raw:
            raise StateError(f"{where}: give either bytes or original and replacement, not both")
        try:
            data = bytes.fromhex(str(raw["bytes"]))
        except ValueError as error:
            raise StateError(f"{where}: bytes must be hex ({error})") from error
        edit = Edit(address, data, None)
    else:
        expected = _words(raw.get("original"), f"{where} original")
        data = _words(raw.get("replacement"), f"{where} replacement")
        if len(expected) != len(data):
            raise StateError(f"{where}: original and replacement must have as many words")
        edit = Edit(address, data, expected)
    if not edit.data or address < 0 or address + len(edit.data) > EE_MEMORY_SIZE:
        raise StateError(f"{where}: {address:#010x} is outside the EE's 32 MB of RAM")
    return edit


def load_patches(path: Path) -> dict[str, Patch]:
    """Every patch of the TOML file at `path`, by name. Raises StateError for a missing file or a bad entry."""
    try:
        with path.open("rb") as handle:
            data = tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError, UnicodeDecodeError) as error:
        raise StateError(f"{path}: {error}") from error
    patches: dict[str, Patch] = {}
    for name, raw in data.get("patch", {}).items():
        where = f"{path}: patch {name}"
        if not isinstance(raw, dict) or not isinstance(raw.get("edits"), list) or not raw["edits"]:
            raise StateError(f"{where}: needs a non-empty list of edits")
        edits = tuple(_edit(edit, f"{where} edit {i + 1}") for i, edit in enumerate(raw["edits"]))
        patches[name] = Patch(name, str(raw.get("description", "")), str(raw.get("research", "")), edits)
    return patches


def pick(patches: dict[str, Patch], names: list[str]) -> list[Patch]:
    """The patches called `names`, in that order. Raises StateError naming the known ones for an unknown name."""
    unknown = [name for name in names if name not in patches]
    if unknown:
        raise StateError(f"unknown patch(es) {', '.join(unknown)}; known: {', '.join(sorted(patches))}")
    return [patches[name] for name in names]


def apply(memory: bytearray, patches: list[Patch]) -> int:
    """Apply `patches` to EE memory in place; returns the number of edits. Raises StateError, changing nothing, when
    a checked edit does not find its original bytes."""
    for patch in patches:
        for edit in patch.edits:
            if edit.expected is None:
                continue
            found = bytes(memory[edit.address : edit.address + len(edit.expected)])
            if found != edit.expected:
                raise StateError(
                    f"patch {patch.name}: at {edit.address:#010x} the state holds {found.hex(' ', 4)}, "
                    f"not the expected {edit.expected.hex(' ', 4)}; is this a state of NTSC-U SLUS_212.15?"
                )
    count = 0
    for patch in patches:
        for edit in patch.edits:
            memory[edit.address : edit.address + len(edit.data)] = edit.data
            count += 1
    return count


def slot_path(pcsx2_dir: Path, slot: int) -> Path:
    """Where PCSX2 keeps quick-save slot `slot` of the NTSC-U disc."""
    return pcsx2_dir / "sstates" / SLOT_NAME.format(slot=slot)


def _check_destination(source: Path, destination: Path, forbidden: list[Path]) -> Path:
    """The resolved `destination` of a copy of `source`. Raises StateError when it is the source or lies in one of
    the `forbidden` folders (PCSX2's `sstates/`)."""
    target = destination.resolve()
    if target == source.resolve():
        raise StateError(f"{destination}: is the source state; a patched or repacked state is always a copy")
    for folder in forbidden:
        if target.is_relative_to(folder.resolve()):
            raise StateError(
                f"{destination}: is in {folder}, where the quick-save slots live; write the copy elsewhere"
            )
    return target


def read_state(source: Path) -> list[tuple[zipfile.ZipInfo, bytes]]:
    """Every entry of the state `source`, header and contents, in order. Raises StateError when it cannot be read or
    has no EE memory."""
    try:
        with zipfile.ZipFile(source) as state:
            entries = [(info, state.read(info.filename)) for info in state.infolist()]
    except NotImplementedError as error:
        raise StateError(f"{source}: {error}; PCSX2 states use zstd, which Python reads from 3.14") from error
    except (OSError, zipfile.BadZipFile) as error:
        raise StateError(f"{source}: not a readable PCSX2 save state ({error})") from error
    if not any(info.filename == EE_MEMORY for info, _ in entries):
        raise StateError(f"{source}: has no {EE_MEMORY}; not a PCSX2 save state")
    return entries


def _write_state(target: Path, entries: list[tuple[zipfile.ZipInfo, bytes]]) -> None:
    """Write `entries` as a zip at `target`, through a `.partial` file so that a failed write leaves nothing there."""
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".partial")
    with zipfile.ZipFile(partial, "w") as copy:
        for info, data in entries:
            copy.writestr(info, data)
    partial.replace(target)


def prepare(source: Path, destination: Path, patches: list[Patch], forbidden: list[Path]) -> int:
    """Copy the state `source` to `destination` with `patches` applied to its EE memory; returns the edit count.

    The source is only read. Raises StateError when the destination is the source or lies in one of the `forbidden`
    folders (PCSX2's `sstates/`), when a state cannot be read, or when an edit's original bytes are not there.
    """
    target = _check_destination(source, destination, forbidden)
    entries = read_state(source)
    found = next(i for i, (info, _) in enumerate(entries) if info.filename == EE_MEMORY)
    # Patch first, so a refused edit leaves no file behind.
    memory = bytearray(entries[found][1])
    if len(memory) != EE_MEMORY_SIZE:
        raise StateError(f"{source}: its {EE_MEMORY} is {len(memory)} bytes, not the EE's 32 MB")
    count = apply(memory, patches)
    entries[found] = (entries[found][0], bytes(memory))
    # The entry's own header (name, date, method) is kept, so PCSX2 reads the copy as it reads the original.
    _write_state(target, entries)
    return count


def repack(source: Path, destination: Path, forbidden: list[Path]) -> int:
    """Copy the state `source` to `destination` with every compressed entry rewritten with plain deflate; returns the
    number of entries rewritten.

    Ghidra's zip reader (which the EE extension's `PCSX2SaveStateImporter` script uses) cannot read zstd, PCSX2's
    default, and Python cannot write Deflate64, the other method PCSX2 offers; plain deflate is read by Ghidra, PCSX2
    and Python alike. Stored entries stay stored. The source is only read; the same refusals as `prepare` apply.
    """
    target = _check_destination(source, destination, forbidden)
    entries = read_state(source)
    rewritten: list[tuple[zipfile.ZipInfo, bytes]] = []
    count = 0
    for info, data in entries:
        # A fresh header, so nothing of the zstd entry (its method's version number, flags) carries over.
        header = zipfile.ZipInfo(info.filename, info.date_time)
        header.external_attr = info.external_attr
        if info.compress_type != zipfile.ZIP_STORED:
            header.compress_type = zipfile.ZIP_DEFLATED
            count += 1
        rewritten.append((header, data))
    _write_state(target, rewritten)
    return count
