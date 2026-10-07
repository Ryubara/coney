# SPDX-License-Identifier: GPL-3.0-or-later
"""The `animations` stage of `coney-tools extract`: every distinct clip as JSON, and each character's data.

* `animations/<clip>.json`: one per distinct clip (a clip shared by several resources is written once): duration,
  root displacement, the root velocity and root translation channels (metres, m/s), one rotation channel per animated
  pose bone, each key `[frame, x, y, z]` or `[frame, x, y, z, w]` (a unit quaternion; frames at 30 a second), and
  the events;
* `animations/characters/<name>.json`: per character data resource, its clips in load order, the 722 anim slots
  resolved to clip files (`null` where the slot uses the default), and the Anim Range List decoded;
* `animations/index.json`: every clip with its file, length and the resources that hold it.

Research: docs/research/formats/animation.md, docs/research/characters.md#files
"""

from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import dataclass, field
from itertools import pairwise
from typing import Any

from coney_tools.extract_output import Output, Report
from coney_tools.extract_wad import SHAPE_ANIMATION, SHAPE_CHARACTER, Entry, Item

KIND = "animations"
FPS = 30
DESCRIPTOR = 80
KEY = 8
EVENT = 24
ANIM_IDS = 722
UNSET = 0xFFFFFFFF


class ClipError(ValueError):
    """A clip whose sections do not hold together."""


def _position(x: int, y: int, z: int) -> list[float]:
    """A stored position: x and y over 1023, z over 2047 (metres)."""
    return [round(x / 1023, 6), round(y / 1023, 6), round(z / 2047, 6)]


def quaternion(x: int, y: int, z: int) -> list[float]:
    """A stored unit quaternion: x, y, z times 2^-15, w the non-negative root."""
    qx, qy, qz = x / 32768, y / 32768, z / 32768
    rest = 1.0 - qx * qx - qy * qy - qz * qz
    return [round(qx, 6), round(qy, 6), round(qz, 6), round(math.sqrt(rest) if rest > 0 else 0.0, 6)]


def _channels(data: bytes, rotation: bool) -> list[list[list[float]]]:
    """A section's channels: a key with a frame delta of 0 starts one; each key gets its absolute frame."""
    if len(data) % KEY:
        raise ClipError(f"a section of {len(data)} bytes is not whole keys")
    channels: list[list[list[float]]] = []
    frame = 0
    for at in range(0, len(data), KEY):
        delta = data[at]
        x, y, z = struct.unpack_from("<hhh", data, at + 2)
        if delta == 0:
            channels.append([])
            frame = 0
        elif not channels:
            raise ClipError("a section does not start with a channel's first key")
        frame += delta
        value = quaternion(x, y, z) if rotation else _position(x, y, z)
        channels[-1].append([frame, *value])
    return channels


def decode_clip(descriptor: bytes, keys: bytes) -> dict[str, Any]:
    """A clip's descriptor (chunk `0x02`) and keyframes (chunk `0x00`) as plain values."""
    if len(descriptor) < DESCRIPTOR:
        raise ClipError("a descriptor is shorter than 80 bytes")
    dx, dy, duration = struct.unpack_from("<3f", descriptor, 4)
    size_a, size_b, size_c = struct.unpack_from("<HHI", descriptor, 0x10)
    channels, events = struct.unpack_from("<HH", descriptor, 0x18)
    mask = int.from_bytes(descriptor[0x20:0x25], "little")
    name = descriptor[0x25:0x43].split(b"\0", 1)[0].decode("latin-1")
    end = size_a + size_b + size_c
    if end + events * EVENT > len(keys):
        raise ClipError(f"{name}: sections need {end + events * EVENT} bytes, the keyframes hold {len(keys)}")
    bones = [b for b in range(40) if mask >> b & 1]
    if len(bones) != channels:
        raise ClipError(f"{name}: {channels} rotation channels but {len(bones)} mask bits")
    velocity = _channels(keys[:size_a], False)
    translation = _channels(keys[size_a : size_a + size_b], False)
    rotations = _channels(keys[size_a + size_b : end], True)
    if len(rotations) != len(bones) or len(velocity) > 1 or len(translation) > 1:
        raise ClipError(f"{name}: channel counts do not match the descriptor")
    event_list = []
    for i in range(events):
        at = end + i * EVENT
        frame, kind, _, word = struct.unpack_from("<HHHH", keys, at)
        values = struct.unpack_from("<6h", keys, at + 8)
        event_list.append(
            {
                "frame": frame,
                "type": kind,
                "word": word,
                "position": _position(*values[0:3]),
                "rotation": quaternion(*values[3:6]),
            }
        )
    return {
        "name": name,
        "duration": round(duration, 6),
        "frames_per_second": FPS,
        "displacement": [round(dx, 6), round(dy, 6)],
        "root_velocity": velocity[0] if velocity else [],
        "root_translation": translation[0] if translation else [],
        "rotations": [{"bone": b, "keys": channel} for b, channel in zip(bones, rotations, strict=True)],
        "events": event_list,
    }


def decode_range_list(data: bytes) -> list[dict[str, Any] | None]:
    """The Anim Range List (chunk `0x45`): a count, then per anim id a direction, reach, far range, damage, hit code and
    flags; `None` where an id has no range data (reach 0)."""
    (count,) = struct.unpack_from("<I", data, 0)
    records: list[dict[str, Any] | None] = []
    for i in range(count):
        dx, dy, reach, far, damage, hit, flags = struct.unpack_from("<hhfhhhH", data, 4 + 16 * i)
        if reach == 0:
            records.append(None)
            continue
        records.append(
            {
                "direction": [dx / 1000, dy / 1000],
                "reach": round(reach, 6),
                "far": far / 1000 if far else round(reach * 1.25, 6),
                "damage": damage,
                "hit_code": hit,
                "flags": flags,
            }
        )
    return records


@dataclass
class _Clip:
    """A written clip."""

    file: str
    name: str
    duration: float
    resources: list[str] = field(default_factory=list)


class AnimationsStage:
    """Writes clips as they arrive and each character data resource's tables."""

    def __init__(self, output: Output) -> None:
        """Start the type."""
        self.output = output
        self.report = Report(KIND)
        output.start(KIND)
        self.clips: dict[str, _Clip] = {}

    def entry(self, entry: Entry) -> None:
        """Clips come as resources only."""

    def _clip(self, item: Item, keys: int, descriptor: int) -> _Clip | None:
        """Write one clip unless an equal one was written; return it."""
        key_bytes = item.chunk_bytes(keys)
        descriptor_bytes = item.chunk_bytes(descriptor)
        digest = hashlib.sha1(key_bytes + descriptor_bytes[4:]).hexdigest()
        known = self.clips.get(digest)
        if known is None:
            try:
                clip = decode_clip(descriptor_bytes, key_bytes)
            except (ClipError, struct.error) as error:
                self.report.problem(f"{item.label()}: {error}")
                return None
            path = self.output.write_json(KIND, f"animations/{clip['name'] or digest[:8]}.json", clip)
            known = self.clips[digest] = _Clip(path, clip["name"], clip["duration"])
            self.report.count("clips")
        known.resources.append(item.label())
        return known

    def item(self, item: Item) -> None:
        """An animation resource's clip, or a character's clips and tables."""
        if item.shape not in (SHAPE_ANIMATION, SHAPE_CHARACTER):
            return
        chunks = item.resource.chunks
        loaded = []
        for (i, first), (j, second) in pairwise(enumerate(chunks)):
            if first.type == 0x00 and second.type == 0x02:
                clip = self._clip(item, i, j)
                loaded.append(clip.file if clip else None)
        if item.shape != SHAPE_CHARACTER:
            return
        record: dict[str, Any] = {"hash": f"{item.resource.hash:08x}", "name": item.name, "clips": loaded}
        for index, chunk in enumerate(chunks):
            data = item.chunk_bytes(index)
            if chunk.type == 0x08:
                slots = struct.unpack_from(f"<{ANIM_IDS}I", data, 8)
                # Slot n counts the clips from the last one loaded (the chunk stack pops last-in first-out).
                record["slots"] = [
                    None if s == UNSET or s >= len(loaded) else loaded[len(loaded) - 1 - s] for s in slots
                ]
                record["slot_values"] = [None if s == UNSET else s for s in slots]
            elif chunk.type == 0x45:
                record["ranges"] = decode_range_list(data)
        self.output.write_json(KIND, f"animations/characters/{item.label()}.json", record)
        self.report.count("characters")

    def finish(self) -> None:
        """Write the clip index."""
        listing = [
            {"name": c.name, "file": c.file, "duration": c.duration, "resources": c.resources}
            for c in self.clips.values()
        ]
        self.output.write_json(KIND, "animations/index.json", {"clips": listing})
