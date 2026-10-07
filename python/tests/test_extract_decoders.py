# SPDX-License-Identifier: GPL-3.0-or-later
"""The decoders behind `coney-tools extract`'s models, animations, levels, scenes and data types, on made-up bytes."""

from __future__ import annotations

import json
import struct

import numpy as np
import pytest

from coney_tools import extract_anims, extract_data, extract_levels, extract_scenes, gltf, ps2icon, ps2mesh
from coney_tools.extract_output import dumps


def vif(command: int, number: int = 0, immediate: int = 0) -> bytes:
    """One VIF code."""
    return struct.pack("<I", command << 24 | number << 16 | immediate)


def chain(vertices: list[tuple[int, int, int]]) -> bytes:
    """A DMA chain of one batch: a `cnt` tag carrying an UNPACK of V4_16 positions, ITOP and MSCAL, then `ret`."""
    body = vif(0x01, 1, 1) + vif(0x60 | 0x0D, len(vertices), 0)
    body += b"".join(struct.pack("<4h", x, y, z, 0) for x, y, z in vertices)
    body += vif(0x04, 0, len(vertices)) + vif(0x14)
    body += b"\0" * (-len(body) % 16)
    return (
        struct.pack("<II", 1 << 28 | len(body) // 16, 0) + bytes(8) + body + struct.pack("<II", 6 << 28, 0) + bytes(8)
    )


def test_json_keeps_plain_lists_on_one_line() -> None:
    text = dumps({"keys": [[0, 1.5], [2, 3.0]], "name": "a"}).decode()
    assert text == '{\n  "keys": [\n    [0, 1.5],\n    [2, 3.0]\n  ],\n  "name": "a"\n}\n'
    assert json.loads(text) == {"keys": [[0, 1.5], [2, 3.0]], "name": "a"}


def test_a_dma_chain_unpacks_to_vertices() -> None:
    mesh = ps2mesh.decode(chain([(1, 2, 3), (4, 5, 6), (7, 8, 9)]), strip=True)
    assert mesh.count == 3 and mesh.batches == 1
    assert mesh.slots[0][:, :3].tolist() == [[1, 2, 3], [4, 5, 6], [7, 8, 9]]


def test_a_chain_without_a_draw_is_refused() -> None:
    body = vif(0x60 | 0x0D, 1, 0) + bytes(8) + bytes(4)
    data = struct.pack("<II", 1 << 28 | 1, 0) + bytes(8) + body + struct.pack("<II", 6 << 28, 0) + bytes(8)
    with pytest.raises(ps2mesh.MeshError):
        ps2mesh.decode(data, strip=True)


def test_strips_alternate_their_winding() -> None:
    assert ps2mesh.strip_triangles(5).tolist() == [[0, 1, 2], [2, 1, 3], [2, 3, 4]]


def test_skin_weights_carry_the_bone_in_their_low_bits() -> None:
    words = np.array([[0.75, 0.25, 0.0, 0.0]], dtype=np.float32).view(np.uint32)
    words[0, 0] = (words[0, 0] & 0xFFFFFC00) | (3 + 1) << 2
    words[0, 1] = (words[0, 1] & 0xFFFFFC00) | (0 + 1) << 2
    nodes, weights = ps2mesh.skin_weights(words)
    assert nodes.tolist() == [[3, 0, 0, 0]]
    assert weights[0, :2] == pytest.approx([0.75, 0.25], abs=1e-3)


def descriptor(name: bytes, sizes: tuple[int, int, int], channels: int, events: int, mask: int) -> bytes:
    """An 80-byte clip descriptor."""
    data = bytearray(80)
    struct.pack_into("<3f", data, 4, 1.0, 2.0, 0.5)
    struct.pack_into("<HHI", data, 0x10, *sizes)
    struct.pack_into("<HH", data, 0x18, channels, events)
    data[0x20:0x25] = mask.to_bytes(5, "little")
    data[0x25 : 0x25 + len(name)] = name
    return bytes(data)


def key(delta: int, x: int, y: int, z: int) -> bytes:
    """One 8-byte key."""
    return struct.pack("<BBhhh", delta, 0, x, y, z)


def test_a_clip_decodes_to_channels_of_keys() -> None:
    keys = key(0, 1023, 0, 2047) + key(0, 0, 0, 0) + key(15, 16384, 0, 0)
    keys += struct.pack("<HHHH6h", 5, 2, 0, 9, 0, 0, 0, 0, 0, 0) + bytes(4)
    clip = extract_anims.decode_clip(descriptor(b"walk", (8, 0, 16), 1, 1, 1 << 4), keys)
    assert clip["name"] == "walk" and clip["duration"] == 0.5
    assert clip["root_velocity"] == [[0, 1.0, 0.0, 1.0]]
    rotation = clip["rotations"][0]
    assert rotation["bone"] == 4
    assert rotation["keys"][1][0] == 15 and rotation["keys"][1][1:] == pytest.approx([0.5, 0, 0, 0.866025], abs=1e-5)
    assert clip["events"][0]["frame"] == 5 and clip["events"][0]["type"] == 2


def test_a_clip_whose_mask_disagrees_is_refused() -> None:
    with pytest.raises(extract_anims.ClipError):
        extract_anims.decode_clip(descriptor(b"x", (0, 0, 0), 2, 0, 1), b"")


def test_the_range_list_skips_ids_without_reach() -> None:
    data = struct.pack("<I", 2) + struct.pack("<hhfhhhH", 0, 1000, 1.5, 0, 7, 3, 1) + bytes(16)
    ranges = extract_anims.decode_range_list(data)
    assert ranges[1] is None
    assert ranges[0] == {"direction": [0.0, 1.0], "reach": 1.5, "far": 1.875, "damage": 7, "hit_code": 3, "flags": 1}


def collision_chunks() -> tuple[bytes, bytes, bytes, bytes, bytes]:
    """A two-cell collision mesh of one triangle listed in the second cell."""
    header = bytearray(160)
    struct.pack_into("<3H", header, 0x70, 2, 1, 1)
    struct.pack_into("<I", header, 0x7C, 3)
    struct.pack_into("<H", header, 0x84, 1)
    struct.pack_into("<I", header, 0x90, 3)
    vertices = b"".join(struct.pack("<4f", *v, 1.0) for v in [(0, 0, 0), (1, 0, 0), (0, 1, 0)])
    triangles = struct.pack("<3HHBB", 0, 1, 2, 1, 35, 4)
    grid = struct.pack("<2I", 0, 1)
    lists = struct.pack("<3H", 0, 1, 0)
    return bytes(header), vertices, triangles, grid, lists


def test_the_collision_mesh_decodes_with_its_grid() -> None:
    collision = extract_levels.decode_collision(*collision_chunks())
    assert collision["grid_size"] == [2, 1, 1]
    assert collision["triangles"] == [{"vertices": [0, 1, 2], "flags": 1, "material": 35, "area": 4}]
    assert collision["cells"] == [[1, 0, 0, [0]]]
    document = extract_levels.collision_document(collision)
    assert document.json["materials"][0]["name"] == "material_35"


def test_path_data_resolves_areas_nodes_and_edges() -> None:
    header = struct.pack("<IIhhI", 1, 3, 2, 1, 1) + bytes(16)
    a_record = struct.pack("<II", 2, 0) + bytes(8)
    points = b"".join(struct.pack("<2f", x, y) + bytes(8) for x, y in [(0, 0), (4, 0), (0, 4)])
    path = bytearray(0x50)
    struct.pack_into("<hh", path, 0, 3, 0)
    struct.pack_into("<4f", path, 8, 0, 4, 0, 4)
    struct.pack_into("<16h", path, 0x28, 0, *([-1] * 15))
    struct.pack_into("<HHI", path, 0x48, 1, 2, 1)
    nodes = struct.pack("<4f", 1, 1, 0, 1) + bytes(4) + struct.pack("<h", 1) + bytes(10)
    nodes += struct.pack("<4f", 2, 1, 0, 1) + bytes(4) + struct.pack("<h", 0) + bytes(10)
    edges = struct.pack("<II", 1, 0x80000004)
    lists = struct.pack("<3h", 0, 2, -1)
    paths = extract_levels.decode_paths(header + a_record + points + bytes(path) + nodes + edges + lists)
    assert paths["areas"] == [[0]]
    assert paths["paths"][0]["nodes"] == [0, 1]
    assert paths["paths"][0]["slabs"][0] == [0, 2]
    assert paths["nodes"][0]["edges"] == [{"to": 1, "flags": 4, "avoid": True}]


def test_occluders_and_subtitles_decode() -> None:
    occluders = struct.pack("<I", 1) + bytes(12) + bytes(0x30) + struct.pack("<16f", *range(16)) + bytes(0x10)
    assert extract_levels.decode_occluders(occluders)[0]["points"][1] == [4.0, 5.0, 6.0]
    records = struct.pack("<I", 0) + b"ENGLISH\0" + struct.pack("<I", 1) + b"l1\0" + struct.pack("<I", 3) + b"Hi\0"
    text = struct.pack("<H", len(records) + 3) + records
    assert extract_levels.decode_subtitles(text) == {"ENGLISH": {"l1": [{"kind": 3, "text": "Hi"}]}}


def test_a_scene_track_decodes_its_keys_and_events() -> None:
    track = bytearray(0x18)
    struct.pack_into("<fIII", track, 4, 2.0, 16 + 8 + 24, 16, 0x18)
    struct.pack_into("<H", track, 0x14, 1)
    keys = struct.pack("<HH3f", 3, 0, 1.0, 2.0, 3.0) + struct.pack("<H3h", 3, 0, 0, 0)
    event = struct.pack("<HH", 4, 41) + bytes(20)
    value = extract_scenes.decode_track(bytes(track) + keys + event, 0)
    assert value["positions"] == [[3, 1.0, 2.0, 3.0]]
    assert value["rotations"] == [[3, 0.0, 0.0, 0.0, 1.0]]
    assert value["events"][0]["type"] == 41


def test_object_lists_and_metrics_parse() -> None:
    text = "1\r\n{dyn_bat {1.0, 2.0, 3.0}, {0.0,0.0,0.0,1.0}, -1, 26, 2, ffffffff, nil )\r\n"
    objects = extract_data.decode_objects(text)
    assert objects[0]["name"] == "dyn_bat" and objects[0]["zone"] == 26 and objects[0]["flag_name"] is None
    with pytest.raises(extract_data.DataError):
        extract_data.decode_objects("2\r\n" + text.split("\r\n", 1)[1])
    metrics = extract_data.decode_metrics("METRICS1\na.bmp b.bmp\n5\n 32   0   0  10  18 # ' '\n")
    assert metrics["characters"] == [{"code": 32, "rectangle": [0, 0, 10, 18]}]


def test_the_global_lists_name_their_hashes() -> None:
    dependency = struct.pack("<I", 1) + bytes(12) + struct.pack("<IIHH", 7, 8, 1, 0)
    anims = struct.pack("<III", 1, 9, 100)
    lists = extract_data.decode_lists({0x4F: dependency, 0x4E: anims}, lambda h: {7: "always"}.get(h))
    assert lists["dependency_list"][0]["group"] == {"hash": "00000007", "name": "always"}
    assert lists["anim_list"] == [{"clip": {"hash": "00000009", "name": None}, "size": 100}]


def test_a_memory_card_icon_decodes() -> None:
    vertex = (
        struct.pack("<4h", 4096, 0, 0, 0)
        + struct.pack("<4h", 0, 4096, 0, 0)
        + struct.pack("<2h4B", 0, 4096, *[128] * 4)
    )
    data = struct.pack("<5I", 0x10000, 1, 7, 0, 3) + vertex * 3
    data += struct.pack("<IIfII", 1, 100, 0.5, 0, 1) + struct.pack("<II2f", 0, 1, 0.0, 1.0)
    data += struct.pack("<H", 31) * (128 * 128)
    icon = ps2icon.parse(data)
    assert icon.shapes[0][0].tolist() == [1.0, 0.0, 0.0]
    assert icon.animation["frames"] == [{"shape": 0, "keys": [[0.0, 1.0]]}]
    assert icon.texture[0, 0].tolist() == [255, 0, 0, 255]
    document = extract_data.icon_document(icon, "icon.png")
    assert document.json["images"] == [{"uri": "icon.png"}]


def test_gltf_accessors_are_aligned_and_bounded() -> None:
    document = gltf.Document()
    document.accessor(np.array([1, 2, 3], dtype=np.uint16))
    index = document.accessor(np.array([[0, 1, 2], [3, 4, 5]], dtype=np.float32), bounds=True)
    assert document.json["bufferViews"][1]["byteOffset"] == 8
    assert document.json["accessors"][index]["max"] == [3.0, 4.0, 5.0]
