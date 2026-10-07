# SPDX-License-Identifier: GPL-3.0-or-later
"""Where `coney-tools extract` writes: safe file names, deterministic files, and the manifest of what was written.

Every file goes through `Output`, which picks a path that is safe on Windows, Linux and macOS (no reserved names, no
two paths that differ only in case), writes the bytes, and records the file's SHA-256 under its asset type. The
manifest (`manifest.json` in the output folder) lists every file with its hash, and per type the file count, the
byte count and a digest over the type's sorted `path  sha256` lines, so two extractions can be compared with the
manifest alone. Nothing written here carries a time stamp: the same disc gives the same bytes.

Research: docs/guides/coney-tools.md#extract (the folder layout)
"""

from __future__ import annotations

import hashlib
import io
import json
import re
import wave
from collections.abc import Iterable
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt
from PIL import Image

from coney_tools.config import ConfigError

MANIFEST = "manifest.json"
#: Bumped when the folder layout or a file format changes in a way a reader must know about.
FORMAT_VERSION = 1

_UNSAFE = re.compile(r"[^A-Za-z0-9._-]+")
_RESERVED = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(10)), *(f"lpt{i}" for i in range(10))}


def safe_part(text: str) -> str:
    """One path component made safe: unusual characters become `_`, and a Windows device name gets a `_` prefix."""
    part = _UNSAFE.sub("_", text).strip(". ") or "_"
    if part.split(".", 1)[0].lower() in _RESERVED:
        part = "_" + part
    return part[:120]


def safe_path(text: str) -> str:
    """A relative path made of safe components (`/` separated; `\\` counts as a separator too)."""
    return "/".join(safe_part(p) for p in text.replace("\\", "/").split("/") if p not in ("", ".", ".."))


def _scalar(value: Any) -> bool:
    """Whether a JSON value holds no list or object."""
    return not isinstance(value, (list, tuple, dict))


def _encode(value: Any, depth: int, out: list[str]) -> None:
    """Append `value`'s JSON: a list or object of plain values on one line (a key, a vertex, a record), anything
    deeper indented two spaces a level, so large tables stay readable and small."""
    if isinstance(value, dict):
        entries: list[tuple[str | None, Any]] = list(value.items())
        opening, closing = "{", "}"
    elif isinstance(value, (list, tuple)):
        entries = [(None, v) for v in value]
        opening, closing = "[", "]"
    else:
        entries = []
    if not entries or all(_scalar(v) for _, v in entries):
        out.append(json.dumps(value, ensure_ascii=False))
        return
    pad = "  " * (depth + 1)
    out.append(opening + "\n")
    for index, (key, item) in enumerate(entries):
        out.append(pad)
        if key is not None:
            out.append(json.dumps(key, ensure_ascii=False) + ": ")
        _encode(item, depth + 1, out)
        out.append(",\n" if index < len(entries) - 1 else "\n")
    out.append("  " * depth + closing)


def dumps(value: Any) -> bytes:
    """JSON as the extractor writes it: UTF-8, keys in the order given, a final newline; a list or object of plain
    values on one line, anything deeper indented two spaces a level."""
    out: list[str] = []
    _encode(value, 0, out)
    return ("".join(out) + "\n").encode("utf-8")


def png_bytes(rgba: npt.NDArray[np.uint8]) -> bytes:
    """A (height, width, 4) array as PNG bytes, with no metadata, so the bytes depend on the pixels alone."""
    buffer = io.BytesIO()
    Image.fromarray(np.ascontiguousarray(rgba), "RGBA").save(buffer, format="PNG", compress_level=6)
    return buffer.getvalue()


def wav_bytes(samples: npt.NDArray[np.int16], rate: int) -> bytes:
    """A (frames, channels) array of 16-bit samples as a PCM WAV file."""
    frames = np.ascontiguousarray(samples.astype("<i2"))
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as out:
        out.setnchannels(frames.shape[1] if frames.ndim == 2 else 1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(frames.tobytes())
    return buffer.getvalue()


class Output:
    """The output folder of one extraction: writes files and keeps the record of what each type wrote."""

    def __init__(self, root: Path) -> None:
        """Use `root` (created when missing). The manifest of an earlier run there is loaded, so a run limited to
        some types keeps the other types' records."""
        self.root = root
        root.mkdir(parents=True, exist_ok=True)
        self.files: dict[str, dict[str, str]] = {}
        self.sizes: dict[str, dict[str, int]] = {}
        self._taken: dict[str, str] = {}  # lower-cased path -> asset type, to keep paths unique on any file system
        self.disc: dict[str, str] = {}
        earlier = root / MANIFEST
        if earlier.is_file():
            try:
                data = json.loads(earlier.read_text(encoding="utf-8"))
            except (OSError, ValueError) as error:
                raise ConfigError(f"{earlier}: cannot be read ({error})") from error
            if data.get("format") == FORMAT_VERSION:
                for kind, record in (data.get("types") or {}).items():
                    self.files[kind] = dict(record.get("files") or {})
                    self.sizes[kind] = dict(record.get("sizes") or {})

    def start(self, kind: str) -> None:
        """Begin (or redo) a type: delete the files an earlier run recorded for it (only those the manifest lists)
        and forget them."""
        folders: set[Path] = set()
        for path in self.files.get(kind, {}):
            (self.root / path).unlink(missing_ok=True)
            folders.update((self.root / path).parents)
        # Folders the deleted files leave empty go too (deepest first; a folder still in use stays).
        for folder in sorted(folders, key=lambda f: len(f.parts), reverse=True):
            if folder != self.root and self.root in folder.parents and folder.is_dir() and not any(folder.iterdir()):
                folder.rmdir()
        self.files[kind] = {}
        self.sizes[kind] = {}
        self._taken = {path: k for path, k in self._taken.items() if k != kind}
        for other, record in self.files.items():
            if other != kind:
                for path in record:
                    self._taken.setdefault(path.lower(), other)

    def claim(self, kind: str, wanted: str) -> str:
        """A free, safe relative path for `wanted`: when it is taken (in any letter case), `~2`, `~3` ... is put
        before the extension."""
        path = safe_path(wanted)
        stem, dot, ext = path.rpartition(".")
        if not dot or "/" in ext:
            stem, dot, ext = path, "", ""
        candidate, number = path, 1
        while candidate.lower() in self._taken:
            number += 1
            candidate = f"{stem}~{number}{dot}{ext}"
        self._taken[candidate.lower()] = kind
        return candidate

    def write(self, kind: str, wanted: str, data: bytes) -> str:
        """Write `data` at a path claimed for `wanted`; returns the path used (relative, `/` separated)."""
        return self.write_claimed(kind, self.claim(kind, wanted), data)

    def write_claimed(self, kind: str, path: str, data: bytes) -> str:
        """Write `data` at a path `claim` already gave out (when another file must name it before it is written)."""
        target = self.root / path
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        except OSError as error:
            raise ConfigError(f"{target}: cannot be written ({error})") from error
        self.files.setdefault(kind, {})[path] = hashlib.sha256(data).hexdigest()
        self.sizes.setdefault(kind, {})[path] = len(data)
        return path

    def replace(self, kind: str, path: str, data: bytes) -> str:
        """Overwrite a file this type already wrote (the Xbox version of an asset taking the PS2 one's place).
        Raises ConfigError when `path` is not one of the type's files."""
        if path not in self.files.get(kind, {}):
            raise ConfigError(f"{path}: not a {kind} file of this extraction, so it cannot be replaced")
        return self.write_claimed(kind, path, data)

    def write_stream(self, kind: str, wanted: str, pieces: Iterable[bytes]) -> str:
        """Write a large file piece by piece (a movie, a copied disc file), hashing as it goes."""
        path = self.claim(kind, wanted)
        target = self.root / path
        digest = hashlib.sha256()
        size = 0
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("wb") as out:
                for piece in pieces:
                    digest.update(piece)
                    size += len(piece)
                    out.write(piece)
        except OSError as error:
            raise ConfigError(f"{target}: cannot be written ({error})") from error
        self.files.setdefault(kind, {})[path] = digest.hexdigest()
        self.sizes.setdefault(kind, {})[path] = size
        return path

    def write_json(self, kind: str, wanted: str, value: Any) -> str:
        """Write a JSON file."""
        return self.write(kind, wanted, dumps(value))

    def type_digest(self, kind: str) -> str:
        """SHA-256 over the type's sorted `path  sha256` lines: one value that changes when any file does."""
        lines = "".join(f"{path}  {sha}\n" for path, sha in sorted(self.files.get(kind, {}).items()))
        return hashlib.sha256(lines.encode("utf-8")).hexdigest()

    def save_manifest(self, disc: dict[str, str]) -> None:
        """Write `manifest.json`: the disc's identity, then per type its counts, digest and files."""
        types = {}
        for kind in sorted(self.files):
            files = self.files[kind]
            types[kind] = {
                "count": len(files),
                "bytes": sum(self.sizes.get(kind, {}).values()),
                "digest": self.type_digest(kind),
                "files": dict(sorted(files.items())),
                "sizes": dict(sorted(self.sizes.get(kind, {}).items())),
            }
        manifest = {"format": FORMAT_VERSION, "tool": "coney-tools extract", "disc": disc, "types": types}
        (self.root / MANIFEST).write_bytes(dumps(manifest))


class Report:
    """What one asset type's stage did: counts by what was written, and the items it could not decode."""

    def __init__(self, kind: str) -> None:
        """An empty report for `kind`."""
        self.kind = kind
        self.counts: dict[str, int] = {}
        self.problems: list[str] = []

    def count(self, what: str, number: int = 1) -> None:
        """Add `number` to the count of `what`."""
        self.counts[what] = self.counts.get(what, 0) + number

    def problem(self, text: str) -> None:
        """Note an item that could not be decoded (it is then written raw, or skipped, as the stage says)."""
        self.problems.append(text)
        self.count("problems")
