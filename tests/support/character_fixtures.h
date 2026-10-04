// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic character files, built byte by byte in the tests. Nothing here comes from the game (LEGAL.md, "No game
// data"). The layouts follow docs/research/characters.md and docs/research/formats/animation.md, and RenderWare's
// binary stream format as librw reads it.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "graphics/ps2_world_mesh.h"
#include "support/fixtures.h"
#include "support/rw_fixtures.h"
#include "support/world_fixtures.h"

namespace coney::test {

/// A packed skinned vertex: ps2Vertex()'s attributes, with weight `weight` on bone `bone` and the rest on bone
/// `otherBone` (when `weight` is below 1). Bones are stored as the characters store them: (bone + 1) << 2 in the
/// weight's low bits, 0 for an unused slot.
inline graphics::Ps2PackedVertex skinnedVertex(std::int16_t x, std::int16_t y, std::int16_t z, std::uint32_t bone,
                                               float weight = 1.0F, std::uint32_t otherBone = 0) {
    graphics::Ps2PackedVertex v = ps2Vertex(x, y, z);
    const auto pack = [](float w, std::uint32_t b) {
        return (std::bit_cast<std::uint32_t>(w) & ~0x3FFU) | (b + 1) << 2;
    };
    v.skin[0] = pack(weight, bone);
    if (weight < 1.0F) {
        v.skin[1] = pack(1.0F - weight, otherBone);
    }
    return v;
}

/// A skinned mesh's DMA chain, everything inline after one cnt tag: each batch is STCYCL 5, 1 and five UNPACKs (the
/// world's four slots and the weights as V4_32 in slot 4), unpacked with an even vector count, then ITOP and a
/// microprogram start, as the characters' chains are laid out.
inline Bytes ps2SkinnedMeshChain(std::initializer_list<std::vector<graphics::Ps2PackedVertex>> batches) {
    constexpr std::array<std::uint32_t, 5> kUnpacks{0x6D008000, 0x6D008001, 0x6E00C002, 0x6E008003, 0x6C008004};
    constexpr std::uint32_t kStcycl = 0x01000105; // cycle 5, write 1
    Bytes stream;
    bool first = true;
    for (const auto& batch : batches) {
        const std::size_t count = (batch.size() + 1) / 2 * 2;
        std::vector<Bytes> slots = ps2BatchSlots(batch, count);
        Bytes weights;
        for (std::size_t i = 0; i < count; ++i) {
            const graphics::Ps2PackedVertex v = i < batch.size() ? batch[i] : graphics::Ps2PackedVertex{};
            for (const std::uint32_t word : v.skin) {
                weights.u32(word);
            }
        }
        slots.push_back(weights);
        for (std::size_t s = 0; s < slots.size(); ++s) {
            stream.u32(kStcycl).u32(kUnpacks[s] | static_cast<std::uint32_t>(count) << 16).append(slots[s].span());
        }
        stream.u32(0x04000000 | static_cast<std::uint32_t>(batch.size()));
        stream.u32(first ? 0x15000000 : 0x17000000); // MSCALF, then MSCNT
        first = false;
    }
    stream.padTo((stream.size() + 15) / 16 * 16);
    Bytes chain;
    chain.u32(0x10000000 | static_cast<std::uint32_t>(stream.size() / 16)).u32(0).u32(0).u32(0);
    chain.append(stream.span());
    chain.u32(0x60000000).u32(0).u32(0x11000000).u32(0x11000000); // ret; FLUSH, FLUSH
    return chain;
}

/// One stored key of a clip: frames since the previous key (0 starts a channel) and three stored integers.
struct ClipKey {
    std::uint8_t delta = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

/// The bytes of a run of keys.
inline Bytes clipKeys(std::initializer_list<ClipKey> keys) {
    Bytes bytes;
    for (const ClipKey& key : keys) {
        bytes.u8(key.delta).u8(0).u16(static_cast<std::uint16_t>(key.x)).u16(static_cast<std::uint16_t>(key.y));
        bytes.u16(static_cast<std::uint16_t>(key.z));
    }
    return bytes;
}

/// The fields of a clip descriptor the fixtures vary.
struct ClipFields {
    std::string_view name = "test_clip";
    float displacementX = 0.0F;
    float displacementY = 0.5F;
    float duration = 1.0F;
    std::uint64_t mask = 0;     ///< Bone mask; bits 0-39 are stored.
    std::uint16_t channels = 0; ///< The descriptor's rotation channel count.
    std::uint16_t events = 0;
};

/// An 80-byte clip descriptor (chunk 0x02) for sections of `sizeA`, `sizeB` and `sizeC` bytes.
inline Bytes clipDescriptor(const ClipFields& fields, std::size_t sizeA, std::size_t sizeB, std::size_t sizeC) {
    Bytes d;
    d.u32(0x0045DE40); // the placeholder vtable word the files carry
    f32(f32(f32(d, fields.displacementX), fields.displacementY), fields.duration);
    d.u16(static_cast<std::uint16_t>(sizeA)).u16(static_cast<std::uint16_t>(sizeB));
    d.u32(static_cast<std::uint32_t>(sizeC));
    d.u16(fields.channels).u16(fields.events).u32(0);
    for (int i = 0; i < 5; ++i) {
        d.u8(static_cast<std::uint8_t>((fields.mask >> (8 * i)) & 0xFFU));
    }
    d.text(fields.name.substr(0, 29));
    return d.padTo(80);
}

/// One event of a clip: frame, type, the word at +6 (7), the position (1, 0, 1) and no rotation, as stored.
inline Bytes clipEvent(std::uint16_t frame, std::uint16_t type) {
    Bytes e;
    e.u16(frame).u16(type).u16(0).u16(7).u16(1023).u16(0).u16(2047).u16(0).u16(0).u16(0).u16(0).u16(0);
    return e;
}

/// A chunk of the chunk system: its 16-byte header and `data`, padded to 16 bytes as the files' chunks are.
inline Bytes chunk(std::uint32_t type, const Bytes& data) {
    Bytes padded = data;
    padded.padTo((data.size() + 15) / 16 * 16);
    Bytes c;
    c.header(type, static_cast<std::uint32_t>(padded.size()), 0, 0).append(padded.span());
    return c;
}

/// A flat chunk container holding `chunks` (each made by chunk()), with a count and the sum of their data sizes.
inline Bytes container(std::initializer_list<Bytes> chunks) {
    Bytes body;
    std::uint32_t dataBytes = 0;
    for (const Bytes& c : chunks) {
        body.append(c.span());
        dataBytes += static_cast<std::uint32_t>(c.size() - 16);
    }
    Bytes out;
    out.header(static_cast<std::uint32_t>(chunks.size()), dataBytes, 0, 0).append(body.span());
    return out;
}

/// A 3 x 4 transform as RenderWare streams a frame's: right, up, at, pos (12 floats).
inline Bytes& rwMatrix(Bytes& bytes, std::array<float, 12> m) {
    for (const float v : m) {
        f32(bytes, v);
    }
    return bytes;
}

/// The bone offset chunk's 544 bytes: entry b is (offsets[b], 1), the rest (0, 0, 0, 1).
inline Bytes boneOffsets(std::initializer_list<std::array<float, 3>> offsets) {
    Bytes b;
    for (const auto& o : offsets) {
        f32(f32(f32(f32(b, o[0]), o[1]), o[2]), 1.0F);
    }
    while (b.size() < 544) {
        f32(f32(f32(f32(b, 0.0F), 0.0F), 0.0F), 1.0F);
    }
    return b;
}

/// What the synthetic character clump is made of: three frames (the clump's root, bone 0 at the origin, bone 1 at
/// (1, 0, 0) under it), a two-node HAnim hierarchy on frame 1, one untextured material, a PS2 skin whose inverse
/// bind matrices undo the frames, and one skinned mesh.
struct ClumpFields {
    Bytes meshChain;              ///< The mesh's DMA chain (ps2SkinnedMeshChain()).
    std::uint32_t meshVertices{}; ///< Its vertex count, for the mesh plugin.
    float positionScale = 0.5F;
    float texCoordScale = 0.25F;
    bool withSkin = true;
};

/// A character clump section (0x10) as the model resources hold one.
inline Bytes characterClump(const ClumpFields& fields) {
    // The frame list: struct with three frames, then one extension each; frame 1 carries the hierarchy.
    Bytes frameStruct;
    frameStruct.u32(3);
    rwMatrix(frameStruct, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}).u32(0xFFFFFFFF).u32(0);
    rwMatrix(frameStruct, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}).u32(0).u32(0);
    rwMatrix(frameStruct, {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0}).u32(1).u32(0);
    Bytes hierarchy;
    hierarchy.u32(0x100).u32(0).u32(2).u32(0).u32(36).u32(0).u32(0).u32(0).u32(1).u32(1).u32(0);
    Bytes leaf;
    leaf.u32(0x100).u32(1).u32(0);
    Bytes frameList = rwSection(graphics::kRwStruct, frameStruct);
    frameList.append(rwSection(graphics::kRwExtension, Bytes{}).span());
    frameList.append(rwSection(graphics::kRwExtension, rwSection(0x11E, hierarchy)).span());
    frameList.append(rwSection(graphics::kRwExtension, rwSection(0x11E, leaf)).span());

    // The geometry: native struct, one untextured material, then mesh plugin, native data and skin.
    Bytes geometryStruct;
    geometryStruct.u32(0x010100F3).u32(2).u32(fields.meshVertices).u32(1);
    f32(f32(f32(f32(geometryStruct, 0.0F), 0.0F), 0.0F), 2.0F).u32(0).u32(0);
    Bytes materialStruct;
    materialStruct.u32(0).u32(0xFF969696).u32(0).u32(0);
    f32(f32(f32(materialStruct, 1.0F), 1.0F), 1.0F);
    Bytes material = rwSection(graphics::kRwStruct, materialStruct);
    material.append(rwSection(graphics::kRwExtension, Bytes{}).span());
    Bytes listStruct;
    listStruct.u32(1).u32(0xFFFFFFFF);
    Bytes materialList = rwSection(graphics::kRwStruct, listStruct);
    materialList.append(rwSection(0x07, material).span());
    Bytes binMesh;
    binMesh.u32(1).u32(1).u32(fields.meshVertices).u32(fields.meshVertices).u32(0);
    Bytes nativeStruct;
    nativeStruct.u32(4).u32(static_cast<std::uint32_t>(fields.meshChain.size())).u32(0).append(fields.meshChain.span());
    Bytes skin;
    skin.u32(4).u8(2).u8(2).u8(4).u8(0).u8(0).u8(1);
    // Inverse bind matrices: bone 0 at the origin, bone 1 at (1, 0, 0); 16 floats each.
    for (const float x : {0.0F, -1.0F}) {
        for (const float v : {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}) {
            f32(skin, v);
        }
        f32(f32(f32(f32(skin, x), 0.0F), 0.0F), 1.0F);
    }
    skin.fill(28, 0);
    Bytes geometryExtension = rwSection(0x50E, binMesh);
    geometryExtension.append(rwSection(0x510, rwSection(graphics::kRwStruct, nativeStruct)).span());
    if (fields.withSkin) {
        geometryExtension.append(rwSection(0x116, rwSection(graphics::kRwStruct, skin)).span());
    }
    Bytes geometry = rwSection(graphics::kRwStruct, geometryStruct);
    geometry.append(rwSection(0x08, materialList).span());
    geometry.append(rwSection(graphics::kRwExtension, geometryExtension).span());
    Bytes geometryListStruct;
    geometryListStruct.u32(1);
    Bytes geometryList = rwSection(graphics::kRwStruct, geometryListStruct);
    geometryList.append(rwSection(0x0F, geometry).span());

    // The atomic: frame 1, geometry 0; the right to render and the game's plugin with the scales.
    Bytes atomicStruct;
    atomicStruct.u32(1).u32(0).u32(5).u32(0);
    Bytes rights;
    rights.u32(3).u32(0x30080);
    Bytes scales;
    f32(f32(scales, fields.positionScale), fields.texCoordScale).u32(0);
    Bytes atomicExtension = rwSection(0x1F, rights);
    atomicExtension.append(rwSection(0x3F0, scales).span());
    Bytes atomic = rwSection(graphics::kRwStruct, atomicStruct);
    atomic.append(rwSection(graphics::kRwExtension, atomicExtension).span());

    Bytes clumpStruct;
    clumpStruct.u32(1).u32(0).u32(0);
    Bytes clump = rwSection(graphics::kRwStruct, clumpStruct);
    clump.append(rwSection(0x0E, frameList).span());
    clump.append(rwSection(0x1A, geometryList).span());
    clump.append(rwSection(0x14, atomic).span());
    clump.append(rwSection(graphics::kRwExtension, Bytes{}).span());
    return rwSection(0x10, clump);
}

/// The synthetic character the model and rig tests share: a strip of five vertices (the last repeated, as strips are
/// joined), scaled by 0.5: a at the origin and c at (0, 1, 0) on bone 0, b at (1, 0, 0) on bone 1, d at (1, 1, 0) half
/// on each.
inline ClumpFields testCharacterFields() {
    const auto a = skinnedVertex(0, 0, 0, 0);
    const auto b = skinnedVertex(2, 0, 0, 1);
    const auto c = skinnedVertex(0, 2, 0, 0);
    const auto d = skinnedVertex(2, 2, 0, 0, 0.5F, 1);
    ClumpFields fields;
    fields.meshChain = ps2SkinnedMeshChain({{a, b, c, d, d}});
    fields.meshVertices = 5;
    return fields;
}

/// The bone offsets of the synthetic skeleton: bone 3 (HAnim id 1, under the pelvis) one metre along x.
inline Bytes testBoneOffsets() { return boneOffsets({{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0}}); }

} // namespace coney::test
