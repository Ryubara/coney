# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the scene record reader and `wad scenes`, on made-up records built here byte by byte."""

import struct
import zlib
from pathlib import Path

import pytest

from coney_tools import scenes
from coney_tools.cli import main
from coney_tools.disc import SECTOR


def clip(name: bytes, duration: float, keys_at: int, events: list[tuple[int, int]]) -> tuple[bytes, bytes]:
    """An 80-byte clip descriptor with one root-velocity key, no other sections, one bone channel, and its keys."""
    descriptor = bytearray(scenes.CLIP_DESCRIPTOR)
    struct.pack_into("<3f", descriptor, 4, 0.0, 0.0, duration)
    struct.pack_into("<HHIHHI", descriptor, 0x10, 8, 0, 8, 1, len(events), keys_at)
    descriptor[0x20] = 0x02  # bone 1 has a channel
    descriptor[0x25 : 0x25 + len(name)] = name
    keys = struct.pack("<BBhhh", 0, 0, 0, 0, 0) * 2
    keys += b"".join(struct.pack("<HH20x", frame, kind) for frame, kind in events)
    return bytes(descriptor), keys


def track(duration: float, keys_at: int, frames: list[int], events: list[tuple[int, int]]) -> tuple[bytes, bytes]:
    """An object/camera track header and its keys: a position and a rotation key per frame, then the events."""
    positions = b"".join(struct.pack("<HH3f", f, 0, 1.0, 2.0, 3.0) for f in frames)
    rotations = b"".join(struct.pack("<H3h", f, 0, 0, 16384) for f in frames)
    tail = b"".join(struct.pack("<HH20x", frame, kind) for frame, kind in events)
    header = bytearray(scenes.TRACK_HEADER)
    struct.pack_into(
        "<fIIIH", header, 4, duration, len(positions + rotations + tail), len(positions), keys_at, len(events)
    )
    return bytes(header), positions + rotations + tail


def role(name: bytes, size: int = scenes.ROLE_SIZE) -> bytes:
    """A role-like definition: name, start pose, end pose, zeros (or lens values for a camera)."""
    body = bytearray(size)
    body[: len(name)] = name
    struct.pack_into("<8f", body, 0x10, 1, 2, 3, 1, 0, 0, 0, 1)
    struct.pack_into("<8f", body, 0x30, 4, 5, 6, 1, 0, 0, 0, 1)
    if size == scenes.CAMERA_SIZE:
        struct.pack_into("<7f", body, 0x50, 0, 0.5, 75, 0.5, 500, 65, 0)
    return bytes(body)


def header_record(name: bytes, first_segment: bytes, frames: int, duration: float) -> bytes:
    """A header with one role, one object and a camera, each with a track."""
    body = bytearray(scenes.HEADER_SIZE)
    body[8 : 8 + len(name)] = name
    body[0x18 : 0x18 + len(first_segment)] = first_segment
    body[0x20:0x24] = bytes([1, 1, 1, 0])
    struct.pack_into("<I", body, 0x84, frames)
    data = bytes(body) + role(b"hero") + role(b"crate") + role(b"camera01", scenes.CAMERA_SIZE)
    defs = (0xC0, 0xC0 + 0x60, 0xC0 + 0xC0, len(data))
    tables_at = len(data)
    data += bytes(16)  # four one-entry tables: role clip, object track, camera track, (no lights)
    clip_at = len(data)
    descriptor, clip_keys = clip(b"hero_clip", duration, clip_at + scenes.CLIP_DESCRIPTOR, [(0, 21), (0, 22)])
    data += descriptor + clip_keys
    object_at = len(data)
    head, keys = track(duration, object_at + scenes.TRACK_HEADER, [0, 30], [])
    data += head + keys
    camera_at = len(data)
    head, keys = track(duration, camera_at + scenes.TRACK_HEADER, [0, 15, 30], [(0, 28), (10, 26)])
    data += head + keys
    out = bytearray(data)
    struct.pack_into("<I", out, 0, len(out))
    struct.pack_into("<4I", out, 0x90, *defs)
    struct.pack_into("<4I", out, 0xA0, tables_at, tables_at + 4, tables_at + 8, tables_at + 12)
    struct.pack_into("<I", out, 0xB0, defs[0])
    struct.pack_into("<3I", out, tables_at, clip_at, object_at, camera_at)
    return bytes(out)


def segment_record(name: bytes, next_segment: bytes, duration: float) -> bytes:
    """A segment with one role clip and one camera track."""
    body = bytearray(scenes.SEGMENT_HEADER + 4)  # header, padding
    body[4 : 4 + len(name)] = name
    body[0x14 : 0x14 + len(next_segment)] = next_segment
    body[0x18:0x1C] = bytes([1, 0, 1, 0])
    tables_at = len(body)
    data = bytes(body) + bytes(16)
    clip_at = len(data)
    descriptor, clip_keys = clip(b"hero_clip", duration, clip_at + scenes.CLIP_DESCRIPTOR, [])
    data += descriptor + clip_keys
    camera_at = len(data)
    head, keys = track(duration, camera_at + scenes.TRACK_HEADER, [0, 30], [])
    data += head + keys
    out = bytearray(data)
    struct.pack_into("<I", out, 0, len(out))
    struct.pack_into("<4I", out, 0x1C, tables_at, tables_at + 4, tables_at + 8, tables_at + 12)
    struct.pack_into("<2I", out, tables_at, clip_at, camera_at)
    struct.pack_into("<I", out, tables_at + 8, camera_at)
    return bytes(out)


def scene_list(entries: list[tuple[int, int, bytes]]) -> bytes:
    """`scene_list.cnk`: one resource holding a Scene List chunk of `{id, size, name}` records."""
    body = struct.pack("<I", len(entries)) + b"".join(struct.pack("<II16s", i, s, n) for i, s, n in entries)
    body += bytes(-len(body) % 16)
    chunk = struct.pack("<IIII", scenes.SCENE_LIST_CHUNK, len(body), 0, 0x1234) + body
    return struct.pack("<IIII", 1, len(body), 0, 0x1234) + chunk


def made_up_scene() -> dict[str, bytes]:
    """A scene `demo` (header + `aa` segment) of two 1-second parts, 60 frames in all."""
    return {"demo": header_record(b"demo", b"aa", 60, 1.0), "demoaa": segment_record(b"demoaa", b"", 1.0)}


def test_header_layout() -> None:
    """The header's names, counts, poses, lens values and tracks come out where the layout puts them."""
    header = scenes.parse_header(made_up_scene()["demo"])
    assert (header.name, header.first_segment, header.frames) == ("demo", "aa", 60)
    assert [r.name for r in header.roles] == ["hero"] and [o.name for o in header.objects] == ["crate"]
    assert header.roles[0].start.position == (1.0, 2.0, 3.0) and header.roles[0].end.position == (4.0, 5.0, 6.0)
    assert header.camera is not None and header.camera.extra[5] == 65.0
    assert header.tracks.clips[0].name == "hero_clip" and header.tracks.clips[0].events == ((0, 21), (0, 22))
    camera = header.tracks.camera[0]
    assert [k[0] for k in camera.positions] == [0, 15, 30] and camera.rotations[0] == (0, 0, 0, 16384)
    assert camera.events == ((0, 28), (10, 26)) and header.tracks.duration() == 1.0


def test_segment_layout_and_name() -> None:
    """A segment holds only tracks; its name is the scene's cut to 15 characters plus 3 of the suffix."""
    segment = scenes.parse_segment(made_up_scene()["demoaa"])
    assert (segment.name, segment.next_segment, len(segment.tracks.clips)) == ("demoaa", "", 1)
    assert scenes.segment_name("a_sixteen_chars_", "aab") == "a_sixteen_charsaab"
    assert scenes.is_header(made_up_scene()["demo"]) and not scenes.is_header(made_up_scene()["demoaa"])


def test_bad_records_raise() -> None:
    """A wrong size word or an offset past the end is a SceneError, not a crash."""
    record = bytearray(made_up_scene()["demo"])
    with pytest.raises(scenes.SceneError):
        scenes.parse_header(bytes(record[:-1]))
    struct.pack_into("<I", record, 0xA0, len(record))
    with pytest.raises(scenes.SceneError):
        scenes.parse_header(bytes(record))


def test_survey_follows_chains() -> None:
    """The survey counts parts and roles and checks the frame count against the summed parts."""
    records = made_up_scene()
    listed = [scenes.ListEntry(i, len(data), name) for i, (name, data) in enumerate(records.items())]
    listed.append(scenes.ListEntry(2, 10, "missing"))
    result = scenes.survey(listed, lambda entry: records.get(entry.name))
    assert (result.headers, result.segments, result.roles, result.cameras) == (1, 1, 1, 1)
    assert (result.broken_chains, result.frame_mismatches) == (0, 0)
    assert result.track_events == {28: 1, 26: 1} and result.errors == ["2 missing: not found"]


def test_scene_list_round_trip() -> None:
    """The Scene List chunk's records are read back in order."""
    entries = scenes.parse_scene_list(scene_list([(0, 100, b"demo"), (1, 50, b"demoaa")]))
    assert entries == [scenes.ListEntry(0, 100, "demo"), scenes.ListEntry(1, 50, "demoaa")]
    with pytest.raises(scenes.SceneError):
        scenes.parse_scene_list(b"not a container at all")


def test_wad_scenes_command(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """`wad scenes` finds records by name, and a cut name by content, then prints counts only."""
    records = made_up_scene()
    long_name = "sixteen_chars_ab"  # the list holds 16 characters; the file's real name is longer
    records[long_name] = header_record(long_name.encode(), b"", 30, 1.0)
    listing = scene_list([(i, len(d), n.encode()) for i, (n, d) in enumerate(records.items())])
    files = [("scene_list.cnk", listing), ("demo.scn", records["demo"]), ("demoaa.scn", records["demoaa"])]
    files.append((long_name + "x.scn", records[long_name]))
    directory, wad_bytes = b"", b""
    for name, data in files:
        directory += struct.pack("<III", len(wad_bytes), len(data), zlib.crc32(f"./ee_files/{name}".encode()))
        wad_bytes += data + bytes(-len(data) % SECTOR)
    disc = tmp_path / "disc"
    disc.mkdir()
    (disc / "WARRIORS.DIR").write_bytes(struct.pack("<I12x", len(files)) + directory)
    (disc / "WARRIORS.WAD").write_bytes(wad_bytes)
    assert main(["wad", "scenes", str(disc)]) == 0
    out = capsys.readouterr().out
    assert "3 records" in out and "parsed: 2 headers, 1 segments, 0 failed" in out
    assert "track event types: 26:2, 28:2" in out
