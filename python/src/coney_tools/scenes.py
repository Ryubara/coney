# SPDX-License-Identifier: GPL-3.0-or-later
"""In-engine scenes: the scene list (`scene_list.cnk`) and the `.scn` records it indexes.

A scene is a header record (`<name>.scn`) and, for a long scene, a chain of segment records (`<name>aa.scn`,
`<name>ab.scn`, ...). Both hold tracks: one animation clip per human role (the 80-byte descriptor of chunk `0x02`
followed by its keys), and keyed tracks for the scene's objects, its camera and its lights. Every offset in a record
counts from the record's start; the game adds the record's address to them when it loads it.

This module parses and checks the records; it never writes game data anywhere.

Research: docs/research/scenes.md (the layout, with the addresses that read it)
"""

from __future__ import annotations

import struct
from collections import Counter
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

from coney_tools.chunks import parse_container

#: Chunk type of the Scene List inside `scene_list.cnk`.
SCENE_LIST_CHUNK = 0x43
SCENE_LIST_FILE = "scene_list.cnk"
_LIST_RECORD = 24  # {u32 id, u32 size, char name[16]}
_NAME = 16
HEADER_SIZE = 0xC0  # the header record's fixed part; the role definitions start here
ROLE_SIZE = 0x60  # a human role or an object: name, start pose, end pose, runtime binding
CAMERA_SIZE = 0x70  # the camera, and each light: a role plus lens or light values
LIGHT_SIZE = 0x70
SEGMENT_HEADER = 0x2C
CLIP_DESCRIPTOR = 0x50  # the animation descriptor of chunk 0x02 (docs/research/formats/animation.md)
TRACK_HEADER = 0x38  # an object, camera or light track's header before its keys (when the keys follow it)
POSITION_KEY = 16  # {u16 frame, u16 unused, f32 x, y, z}
ROTATION_KEY = 8  # {u16 frame, s16 x, y, z} of a unit quaternion, w rebuilt
EVENT = 24  # {u16 frame, u16 type, 20 bytes of arguments}
FPS = 30


class SceneError(Exception):
    """A scene record or the scene list does not have the layout the game reads."""


@dataclass(frozen=True)
class ListEntry:
    """One Scene List record: the scene id (its index), the record's size and its name (cut to 16 characters)."""

    id: int
    size: int
    name: str


@dataclass(frozen=True)
class Pose:
    """A position (metres) and an orientation quaternion `(x, y, z, w)`."""

    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]


@dataclass(frozen=True)
class Role:
    """A human role, an object, the camera or a light: its name and its poses at the scene's start and end."""

    name: str
    start: Pose
    end: Pose
    extra: tuple[float, ...] = ()  # camera: +0x50-+0x68; light: +0x50-+0x68 (the kind word read as a float)


@dataclass(frozen=True)
class Clip:
    """A human role's track: an animation clip (descriptor and keys) as in a character's chunk 0x02 / 0x00 pair."""

    name: str
    duration: float
    channels: int
    events: tuple[tuple[int, int], ...]  # (frame, type)


@dataclass(frozen=True)
class Track:
    """An object, camera or light track: position keys, rotation keys and events."""

    duration: float
    positions: tuple[tuple[int, float, float, float], ...]  # (frame, x, y, z)
    rotations: tuple[tuple[int, int, int, int], ...]  # (frame, x, y, z) as stored, x 2**-15
    events: tuple[tuple[int, int], ...]  # (frame, type)


@dataclass
class Tracks:
    """The tracks one record holds: a clip per role, then the object, camera and light tracks."""

    clips: list[Clip] = field(default_factory=list)
    objects: list[Track] = field(default_factory=list)
    camera: list[Track] = field(default_factory=list)
    lights: list[Track] = field(default_factory=list)

    def duration(self) -> float:
        """The part's length in seconds, taken from the first track as `SceneLength` does (0 with none)."""
        for group in (self.clips, self.camera, self.objects, self.lights):
            if group:
                return group[0].duration
        return 0.0


@dataclass
class Header:
    """A scene's header record."""

    size: int
    name: str
    first_segment: str  # suffix of the first segment ("aa"), or "" for a scene in one record
    label: str  # the name at +0x30 (not read by the code traced)
    frames: int  # the whole scene's length at 30 frames a second, segments included
    roles: list[Role]
    objects: list[Role]
    camera: Role | None
    lights: list[Role]
    tracks: Tracks


@dataclass
class Segment:
    """A segment record: the next part of a long scene's tracks."""

    size: int
    name: str
    next_segment: str  # suffix of the next segment, or "" for the last
    tracks: Tracks


def _text(data: bytes, offset: int, size: int) -> str:
    """A NUL-terminated (or full-width) Latin-1 name."""
    return data[offset : offset + size].split(b"\0", 1)[0].decode("latin-1")


class _Reader:
    """Bounds-checked little-endian reads from one record."""

    def __init__(self, data: bytes, what: str) -> None:
        """Wrap `data`; `what` names the record in error messages."""
        self.data = data
        self.what = what

    def need(self, offset: int, size: int) -> None:
        """Raise SceneError unless `size` bytes at `offset` lie inside the record."""
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise SceneError(f"{self.what}: {size} bytes at {offset:#x} reach past the end ({len(self.data):#x})")

    def u8(self, offset: int) -> int:
        """An unsigned byte."""
        self.need(offset, 1)
        return self.data[offset]

    def u16(self, offset: int) -> int:
        """An unsigned 16-bit word."""
        self.need(offset, 2)
        return int(struct.unpack_from("<H", self.data, offset)[0])

    def u32(self, offset: int) -> int:
        """An unsigned 32-bit word."""
        self.need(offset, 4)
        return int(struct.unpack_from("<I", self.data, offset)[0])

    def floats(self, offset: int, count: int) -> tuple[float, ...]:
        """`count` 32-bit floats."""
        self.need(offset, 4 * count)
        return tuple(struct.unpack_from(f"<{count}f", self.data, offset))

    def table(self, offset: int, count: int) -> list[int]:
        """`count` record offsets from a table at `offset`."""
        return [self.u32(offset + 4 * index) for index in range(count)]


def _pose(reader: _Reader, offset: int) -> Pose:
    """A position vec4 (w = 1) followed by a quaternion."""
    x, y, z, _ = reader.floats(offset, 4)
    return Pose((x, y, z), tuple(reader.floats(offset + 16, 4)))  # type: ignore[arg-type]


def _role(reader: _Reader, offset: int, size: int) -> Role:
    """A role-like definition of `size` bytes: name, start pose, end pose, then (for 0x70) lens or light values."""
    reader.need(offset, size)
    extra = reader.floats(offset + 0x50, 7) if size == CAMERA_SIZE else ()
    return Role(_text(reader.data, offset, _NAME), _pose(reader, offset + 0x10), _pose(reader, offset + 0x30), extra)


def _events(reader: _Reader, start: int, count: int) -> tuple[tuple[int, int], ...]:
    """`count` 24-byte events at `start`: (frame, type) of each."""
    reader.need(start, count * EVENT)
    return tuple((reader.u16(start + EVENT * i), reader.u16(start + EVENT * i + 2)) for i in range(count))


def parse_clip(reader: _Reader, offset: int) -> Clip:
    """A role's clip: the 80-byte descriptor, its keys (sections A, B, C) and its events.

    The layout is that of formats/animation.md: sizes at +0x10 (u16 A), +0x12 (u16 B), +0x14 (u32 C), the channel
    count at +0x18 and the event count at +0x1a, the keys' offset at +0x1c and the bone mask at +0x20.
    """
    reader.need(offset, CLIP_DESCRIPTOR)
    duration = reader.floats(offset + 0x0C, 1)[0]
    size_a, size_b = reader.u16(offset + 0x10), reader.u16(offset + 0x12)
    size_c = reader.u32(offset + 0x14)
    channels, event_count = reader.u16(offset + 0x18), reader.u16(offset + 0x1A)
    keys = reader.u32(offset + 0x1C)
    mask = int.from_bytes(reader.data[offset + 0x20 : offset + 0x25], "little")
    if bin(mask).count("1") != channels:
        raise SceneError(f"{reader.what}: clip at {offset:#x} has {channels} channels but its mask sets {mask:#x}")
    if (size_a | size_b | size_c) % 8:
        raise SceneError(f"{reader.what}: clip at {offset:#x} has a section size that is not whole 8-byte keys")
    reader.need(keys, size_a + size_b + size_c)
    events = _events(reader, keys + size_a + size_b + size_c, event_count)
    return Clip(_text(reader.data, offset + 0x25, 30), duration, channels, events)


def parse_track(reader: _Reader, offset: int) -> Track:
    """An object, camera or light track: header, then position keys, rotation keys and events.

    Header: +0x04 f32 duration, +0x08 u32 bytes of keys and events, +0x0c u32 bytes of position keys, +0x10 u32 the
    keys' offset, +0x14 u16 the event count.
    """
    reader.need(offset, 0x16)
    duration = reader.floats(offset + 4, 1)[0]
    total, position_bytes = reader.u32(offset + 8), reader.u32(offset + 0x0C)
    keys, event_count = reader.u32(offset + 0x10), reader.u16(offset + 0x14)
    rotation_bytes = total - position_bytes - event_count * EVENT
    if position_bytes % POSITION_KEY or rotation_bytes < 0 or rotation_bytes % ROTATION_KEY:
        raise SceneError(f"{reader.what}: track at {offset:#x} has sizes {total}, {position_bytes}, {event_count}")
    reader.need(keys, total)
    positions = tuple(
        (reader.u16(at), *reader.floats(at + 4, 3)) for at in range(keys, keys + position_bytes, POSITION_KEY)
    )
    start = keys + position_bytes
    rotations = tuple(
        (reader.u16(at), *struct.unpack_from("<3h", reader.data, at + 2))
        for at in range(start, start + rotation_bytes, ROTATION_KEY)
    )
    for kind, frames in (("position", [k[0] for k in positions]), ("rotation", [k[0] for k in rotations])):
        if frames != sorted(frames):
            raise SceneError(f"{reader.what}: track at {offset:#x} has {kind} keys out of frame order")
    events = _events(reader, start + rotation_bytes, event_count)
    return Track(duration, positions, rotations, events)  # type: ignore[arg-type]


def _tracks(reader: _Reader, tables: list[int], counts: tuple[int, int, int, int]) -> Tracks:
    """Every track a record's four tables name: clips per role, then object, camera and light tracks."""
    roles, objects, camera, lights = counts
    return Tracks(
        [parse_clip(reader, at) for at in reader.table(tables[0], roles)],
        [parse_track(reader, at) for at in reader.table(tables[1], objects)],
        [parse_track(reader, at) for at in reader.table(tables[2], camera)],
        [parse_track(reader, at) for at in reader.table(tables[3], lights)],
    )


def is_header(data: bytes) -> bool:
    """Whether a record is a scene header (`+0x04` is 0 and a name follows) rather than a segment (name at +0x04)."""
    return len(data) >= 9 and data[4:8] == b"\0\0\0\0" and data[8] != 0


def parse_header(data: bytes, what: str = "scene") -> Header:
    """Parse a header record; raises SceneError when a field, count or offset is out of place."""
    reader = _Reader(data, what)
    reader.need(0, HEADER_SIZE)
    if reader.u32(0) != len(data):
        raise SceneError(f"{what}: size word {reader.u32(0)} is not the record's size {len(data)}")
    counts = (reader.u8(0x20), reader.u8(0x21), reader.u8(0x22), reader.u8(0x23))
    if counts[2] > 1:
        raise SceneError(f"{what}: camera count {counts[2]}")
    defs = reader.table(0x90, 4)
    tables = reader.table(0xA0, 4)
    roles = [_role(reader, defs[0] + ROLE_SIZE * i, ROLE_SIZE) for i in range(counts[0])]
    objects = [_role(reader, defs[1] + ROLE_SIZE * i, ROLE_SIZE) for i in range(counts[1])]
    camera = _role(reader, defs[2], CAMERA_SIZE) if counts[2] else None
    lights = [_role(reader, defs[3] + LIGHT_SIZE * i, LIGHT_SIZE) for i in range(counts[3])]
    return Header(
        size=len(data),
        name=_text(data, 8, _NAME),
        first_segment=_text(data, 0x18, 4),
        label=_text(data, 0x30, 0x54),
        frames=reader.u32(0x84),
        roles=roles,
        objects=objects,
        camera=camera,
        lights=lights,
        tracks=_tracks(reader, tables, counts),  # type: ignore[arg-type]
    )


def parse_segment(data: bytes, what: str = "segment") -> Segment:
    """Parse a segment record: size, name, the next segment's suffix, four counts and four track tables."""
    reader = _Reader(data, what)
    reader.need(0, SEGMENT_HEADER)
    if reader.u32(0) != len(data):
        raise SceneError(f"{what}: size word {reader.u32(0)} is not the record's size {len(data)}")
    counts = (reader.u8(0x18), reader.u8(0x19), reader.u8(0x1A), reader.u8(0x1B))
    return Segment(
        size=len(data),
        name=_text(data, 4, _NAME),
        next_segment=_text(data, 0x14, 4),
        tracks=_tracks(reader, reader.table(0x1C, 4), counts),  # type: ignore[arg-type]
    )


def parse_scene_list(data: bytes) -> list[ListEntry]:
    """Parse `scene_list.cnk`: its Scene List chunk is a u32 count, then 24-byte `{id, size, name[16]}` records."""
    container = parse_container(data)
    chunk = None
    if container is not None:
        chunk = next((c for r in container.resources for c in r.chunks if c.type == SCENE_LIST_CHUNK), None)
    if chunk is None:
        raise SceneError(f"{SCENE_LIST_FILE}: no Scene List chunk (type {SCENE_LIST_CHUNK:#x})")
    body = data[chunk.offset : chunk.offset + chunk.size]
    (count,) = struct.unpack_from("<I", body, 0)
    if 4 + count * _LIST_RECORD > len(body):
        raise SceneError(f"{SCENE_LIST_FILE}: {count} records do not fit in {len(body)} bytes")
    entries = []
    for index in range(count):
        at = 4 + index * _LIST_RECORD
        scene_id, size = struct.unpack_from("<II", body, at)
        entries.append(ListEntry(scene_id, size, _text(body, at + 8, _NAME)))
    return entries


def segment_name(scene: str, suffix: str) -> str:
    """The name the game looks a segment up by: the scene's name cut to 15 characters, then up to 3 of the suffix."""
    return scene[:15] + suffix[:3]


@dataclass
class Survey:
    """Counts over every record of the scene list."""

    headers: int = 0
    segments: int = 0
    roles: int = 0
    objects: int = 0
    cameras: int = 0
    lights: int = 0
    frames: int = 0
    broken_chains: int = 0
    frame_mismatches: int = 0
    clip_events: Counter[int] = field(default_factory=Counter)
    track_events: Counter[int] = field(default_factory=Counter)
    errors: list[str] = field(default_factory=list)


def survey(entries: Iterable[ListEntry], read: Callable[[ListEntry], bytes | None]) -> Survey:
    """Parse every listed record with `read` (None for a record that cannot be found) and count what they hold.

    Also follows each header's segment chain the way the game names segments and checks that the header's frame
    count equals its parts' summed durations at 30 frames a second.
    """
    result = Survey()
    entries = list(entries)
    by_name = {entry.name: entry for entry in entries}
    parsed: dict[str, Header | Segment] = {}
    for entry in entries:
        data = read(entry)
        if data is None:
            result.errors.append(f"{entry.id} {entry.name}: not found")
            continue
        try:
            record: Header | Segment = parse_header(data, entry.name) if is_header(data) else parse_segment(data)
        except SceneError as error:
            result.errors.append(f"{entry.id}: {error}")
            continue
        parsed[entry.name] = record
        if isinstance(record, Header):
            result.headers += 1
            result.roles += len(record.roles)
            result.objects += len(record.objects)
            result.cameras += record.camera is not None
            result.lights += len(record.lights)
            result.frames += record.frames
        else:
            result.segments += 1
        tracks = record.tracks
        for clip in tracks.clips:
            result.clip_events.update(kind for _, kind in clip.events)
        for track in tracks.objects + tracks.camera + tracks.lights:
            result.track_events.update(kind for _, kind in track.events)
    for name, record in parsed.items():
        if not isinstance(record, Header):
            continue
        total, suffix, seen = round(record.tracks.duration() * FPS), record.first_segment, 0
        while suffix and seen < len(entries):
            part = parsed.get(segment_name(name, suffix)) if segment_name(name, suffix) in by_name else None
            if not isinstance(part, Segment):
                result.broken_chains += 1
                break
            total += round(part.tracks.duration() * FPS)
            suffix, seen = part.next_segment, seen + 1
        else:
            if abs(total - record.frames) > 1 + seen:
                result.frame_mismatches += 1
    return result
