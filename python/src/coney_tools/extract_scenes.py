# SPDX-License-Identifier: GPL-3.0-or-later
"""The `scenes` stage of `coney-tools extract`: every scene record and the scene list as JSON.

* `scenes/<record>.json` for each `.scn` entry. A header record holds the scene's name, length and its roles,
  objects, camera and lights with their start and end poses; both kinds of record hold tracks: a clip per human role
  (decoded as the `animations` type decodes clips), and keyed tracks for the objects, the camera and the lights
  (position keys `[frame, x, y, z]` in metres, rotation keys `[frame, x, y, z, w]` as unit quaternions, events
  with their 20 argument bytes in hex). A segment record names the next segment of its scene.
* `scenes/list.json`: the Scene List (`scene_list.cnk`): each scene's id, record size and name.

Research: docs/research/scenes.md, docs/research/formats/animation.md
"""

from __future__ import annotations

import struct
from typing import Any

from coney_tools import scenes, wad_kinds
from coney_tools.extract_anims import ClipError, decode_clip, quaternion
from coney_tools.extract_output import Output, Report
from coney_tools.extract_wad import SHAPE_SCENE_LIST, Entry, Item

KIND = "scenes"


def _events(data: bytes, start: int, count: int) -> list[dict[str, Any]]:
    """`count` 24-byte events: frame, type and the argument bytes."""
    result = []
    for i in range(count):
        at = start + scenes.EVENT * i
        frame, kind = struct.unpack_from("<HH", data, at)
        result.append({"frame": frame, "type": kind, "args": data[at + 4 : at + scenes.EVENT].hex()})
    return result


def decode_track(data: bytes, offset: int) -> dict[str, Any]:
    """An object, camera or light track (scenes.md): its length, position and rotation keys, and its events."""
    duration, total, position_bytes, keys = struct.unpack_from("<fIII", data, offset + 4)
    (event_count,) = struct.unpack_from("<H", data, offset + 0x14)
    rotation_bytes = total - position_bytes - event_count * scenes.EVENT
    if rotation_bytes < 0 or keys + total > len(data):
        raise scenes.SceneError(f"a track at {offset:#x} runs past its record")
    positions = []
    for at in range(keys, keys + position_bytes, scenes.POSITION_KEY):
        (frame,) = struct.unpack_from("<H", data, at)
        x, y, z = struct.unpack_from("<3f", data, at + 4)
        positions.append([frame, round(x, 6), round(y, 6), round(z, 6)])
    start = keys + position_bytes
    rotations = []
    for at in range(start, start + rotation_bytes, scenes.ROTATION_KEY):
        frame, x, y, z = struct.unpack_from("<H3h", data, at)
        rotations.append([frame, *quaternion(x, y, z)])
    return {
        "duration": round(duration, 6),
        "positions": positions,
        "rotations": rotations,
        "events": _events(data, start + rotation_bytes, event_count),
    }


def _clip(data: bytes, offset: int) -> dict[str, Any]:
    """A role's clip: its descriptor at `offset`, its keys where the descriptor's `+0x1c` points."""
    (keys,) = struct.unpack_from("<I", data, offset + 0x1C)
    return decode_clip(data[offset : offset + scenes.CLIP_DESCRIPTOR], data[keys:])


def _tracks(data: bytes, tables: list[int], counts: tuple[int, ...]) -> dict[str, Any]:
    """The four track tables: clips per role, then the object, camera and light tracks."""

    def offsets(table: int, count: int) -> list[int]:
        """`count` record offsets from a table."""
        return list(struct.unpack_from(f"<{count}I", data, table)) if count else []

    return {
        "clips": [_clip(data, at) for at in offsets(tables[0], counts[0])],
        "objects": [decode_track(data, at) for at in offsets(tables[1], counts[1])],
        "camera": [decode_track(data, at) for at in offsets(tables[2], counts[2])],
        "lights": [decode_track(data, at) for at in offsets(tables[3], counts[3])],
    }


def _role(role: scenes.Role) -> dict[str, Any]:
    """A role's name, poses and (camera, lights) extra values."""
    value: dict[str, Any] = {
        "name": role.name,
        "start": {"position": list(role.start.position), "rotation": list(role.start.rotation)},
        "end": {"position": list(role.end.position), "rotation": list(role.end.rotation)},
    }
    if role.extra:
        value["extra"] = list(role.extra)
    return value


def decode_record(data: bytes, what: str) -> dict[str, Any]:
    """A scene header or segment record as plain values."""
    if scenes.is_header(data):
        header = scenes.parse_header(data, what)  # checks the layout first
        counts = tuple(data[0x20:0x24])
        tables = list(struct.unpack_from("<4I", data, 0xA0))
        return {
            "record": "header",
            "name": header.name,
            "label": header.label,
            "frames": header.frames,
            "first_segment": header.first_segment or None,
            "roles": [_role(r) for r in header.roles],
            "objects": [_role(r) for r in header.objects],
            "camera": _role(header.camera) if header.camera else None,
            "lights": [_role(r) for r in header.lights],
            "tracks": _tracks(data, tables, counts),
        }
    segment = scenes.parse_segment(data, what)
    counts = tuple(data[0x18:0x1C])
    tables = list(struct.unpack_from("<4I", data, 0x1C))
    return {
        "record": "segment",
        "name": segment.name,
        "next_segment": segment.next_segment or None,
        "tracks": _tracks(data, tables, counts),
    }


class ScenesStage:
    """Writes each scene record as it arrives, and the scene list."""

    def __init__(self, output: Output) -> None:
        """Start the type."""
        self.output = output
        self.report = Report(KIND)
        output.start(KIND)

    def entry(self, entry: Entry) -> None:
        """A `.scn` record."""
        if entry.kind != wad_kinds.SCENE:
            return
        label = entry.label()
        try:
            value = decode_record(entry.data, label)
        except (scenes.SceneError, ClipError, struct.error, ValueError, IndexError) as error:
            self.report.problem(f"{label}: {error}")
            return
        self.output.write_json(KIND, f"scenes/{label.removesuffix('.scn')}.json", value)
        self.report.count("headers" if value["record"] == "header" else "segments")

    def item(self, item: Item) -> None:
        """The scene list resource."""
        if item.shape != SHAPE_SCENE_LIST:
            return
        chunk = next((i for i, c in enumerate(item.resource.chunks) if c.type == scenes.SCENE_LIST_CHUNK), None)
        if chunk is None:
            self.report.problem(f"{item.label()}: no Scene List chunk")
            return
        body = item.chunk_bytes(chunk)
        (count,) = struct.unpack_from("<I", body, 0)
        listing = []
        for index in range(count):
            scene_id, size = struct.unpack_from("<II", body, 4 + 24 * index)
            name = body[12 + 24 * index : 28 + 24 * index].split(b"\0", 1)[0].decode("latin-1")
            listing.append({"id": scene_id, "size": size, "name": name})
        self.output.write_json(KIND, "scenes/list.json", {"scenes": listing})
        self.report.count("scene lists")

    def finish(self) -> None:
        """Nothing is held back."""
