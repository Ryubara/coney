// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic streamed-world files, built byte by byte in the tests. Nothing here comes from the game (LEGAL.md, "No game
// data"). The layouts follow docs/research/world.md and RenderWare's binary stream format as librw reads it; the PS2
// DMA chains follow the vertex layout described in src/graphics/ps2_world_mesh.h.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "graphics/ps2_world_mesh.h"
#include "support/fixtures.h"
#include "support/rw_fixtures.h"

namespace coney::test {

/// A packed vertex whose attributes are derived from its position, so tests can check every field: texture
/// coordinates twice the position, colour (10, 20, 30, 0x80), normal (0, 0, 0x7E).
inline graphics::Ps2PackedVertex ps2Vertex(std::int16_t x, std::int16_t y, std::int16_t z) {
    graphics::Ps2PackedVertex v;
    v.position = {x, y, z, 0};
    v.texCoords = {static_cast<std::int16_t>(x * 2), static_cast<std::int16_t>(y * 2), static_cast<std::int16_t>(z * 2),
                   0};
    v.colour = {10, 20, 30, 0x80};
    v.normal = {0, 0, 0x7E, 0};
    return v;
}

/// Appends a float's bits.
inline Bytes& f32(Bytes& bytes, float value) { return bytes.u32(std::bit_cast<std::uint32_t>(value)); }

/// The four unpacks' data of one batch, each padded to whole 16-byte units: positions and texture coordinates
/// (V4_16), colours (V4_8 unsigned) and normals (V4_8). `count` vectors are written: the batch, then zero vertices.
inline std::vector<Bytes> ps2BatchSlots(const std::vector<graphics::Ps2PackedVertex>& batch, std::size_t count) {
    std::vector<Bytes> slots(4);
    for (std::size_t i = 0; i < count; ++i) {
        const graphics::Ps2PackedVertex v = i < batch.size() ? batch[i] : graphics::Ps2PackedVertex{};
        for (std::size_t c = 0; c < 4; ++c) {
            slots[0].u16(static_cast<std::uint16_t>(v.position[c]));
            slots[1].u16(static_cast<std::uint16_t>(v.texCoords[c]));
            slots[2].u8(v.colour[c]);
            slots[3].u8(static_cast<std::uint8_t>(v.normal[c]));
        }
    }
    for (Bytes& slot : slots) {
        slot.padTo((slot.size() + 15) / 16 * 16);
    }
    return slots;
}

/// A mesh's DMA chain with one batch per entry of `batches`. Each batch is unpacked with an even vector count (a
/// padding vertex after an odd batch) and ITOP says the real count. `withReferences`: as the game stores them, a
/// reference tag per unpack pointing at data after the chain's ret tag; otherwise everything inline after one cnt tag.
inline Bytes ps2MeshChain(std::initializer_list<std::vector<graphics::Ps2PackedVertex>> batches, bool withReferences) {
    constexpr std::array<std::uint32_t, 4> kUnpacks{0x6D008000, 0x6D008001, 0x6E00C002, 0x6E008003};
    constexpr std::uint32_t kStcycl = 0x01000104; // cycle 4, write 1
    Bytes chain;
    Bytes data; // the referenced data, placed after the chain
    Bytes inlineStream;
    std::vector<std::size_t> refTags; // offsets of reference tags, patched once the chain's length is known
    std::vector<std::size_t> refData; // the matching offsets into `data`
    bool first = true;
    for (const auto& batch : batches) {
        const std::size_t count = (batch.size() + 1) / 2 * 2;
        const std::vector<Bytes> slots = ps2BatchSlots(batch, count);
        for (std::size_t s = 0; s < 4; ++s) {
            const std::uint32_t unpack = kUnpacks[s] | static_cast<std::uint32_t>(count) << 16;
            if (withReferences) {
                refTags.push_back(chain.size());
                refData.push_back(data.size());
                chain.u32(0x30000000 | static_cast<std::uint32_t>(slots[s].size() / 16))
                    .u32(0)
                    .u32(kStcycl)
                    .u32(unpack);
                data.append(slots[s].span());
            } else {
                inlineStream.u32(kStcycl).u32(unpack).append(slots[s].span().first(slots[s].size()));
            }
        }
        const std::uint32_t itop = 0x04000000 | static_cast<std::uint32_t>(batch.size());
        const std::uint32_t start = first ? 0x15000000 : 0x17000000; // MSCALF, then MSCNT
        if (withReferences) {
            chain.u32(0x10000000).u32(0).u32(itop).u32(start); // cnt with no data, the commands in the tag
        } else {
            inlineStream.u32(itop).u32(start);
        }
        first = false;
    }
    if (!withReferences) {
        inlineStream.padTo((inlineStream.size() + 15) / 16 * 16);
        chain.u32(0x10000000 | static_cast<std::uint32_t>(inlineStream.size() / 16)).u32(0).u32(0).u32(0);
        chain.append(inlineStream.span());
    }
    chain.u32(0x60000000).u32(0).u32(0x11000000).u32(0x11000000); // ret; FLUSH, FLUSH
    const std::size_t dataStart = chain.size();
    for (std::size_t i = 0; i < refTags.size(); ++i) {
        chain.patchU32(refTags[i] + 4, static_cast<std::uint32_t>((dataStart + refData[i]) / 16));
    }
    chain.append(data.span());
    return chain;
}

/// An empty extension section.
inline Bytes rwEmptyExtension() { return rwSection(graphics::kRwExtension, Bytes{}); }

/// What a synthetic native atomic is made of.
struct AtomicFields {
    std::vector<Bytes> meshChains;         ///< One DMA chain per mesh.
    std::vector<std::uint32_t> meshCounts; ///< Each mesh's vertex count, for the mesh plugin.
    std::uint32_t triangles = 0;           ///< For the geometry header.
    std::uint32_t pipeline = 0x30083;      ///< Right-to-render data under plugin 3.
    float positionScale = 0.5F;
    float secondScale = 0.25F;
    std::uint32_t materialIndex = 0;          ///< Material of every mesh (the list holds one).
    std::uint32_t geometryFlags = 0x0100001D; ///< Native, triangle strips, one texture set, prelit, normals.
};

/// A standalone atomic section (0x14) with PS2 native geometry, as a part file holds one.
inline Bytes nativeAtomic(const AtomicFields& fields) {
    // Geometry: struct (header and one morph target with a bounding sphere and no arrays).
    Bytes geometryStruct;
    std::uint32_t total = 0;
    for (const std::uint32_t count : fields.meshCounts) {
        total += count;
    }
    geometryStruct.u32(fields.geometryFlags).u32(fields.triangles).u32(total).u32(1);
    f32(f32(f32(f32(geometryStruct, 0.0F), 0.0F), 0.0F), 10.0F).u32(0).u32(0);
    // Material list: one material, no texture.
    Bytes materialStruct;
    materialStruct.u32(0).u32(0xFFFFFFFF).u32(0).u32(0);
    f32(f32(f32(materialStruct, 1.0F), 1.0F), 1.0F);
    Bytes material = rwSection(graphics::kRwStruct, materialStruct);
    material.append(rwEmptyExtension().span());
    Bytes listStruct;
    listStruct.u32(1).u32(0xFFFFFFFF);
    Bytes list = rwSection(graphics::kRwStruct, listStruct);
    list.append(rwSection(0x07, material).span());
    // Extension: mesh plugin, then native data.
    Bytes meshes;
    meshes.u32(1).u32(static_cast<std::uint32_t>(fields.meshCounts.size())).u32(total);
    for (const std::uint32_t count : fields.meshCounts) {
        meshes.u32(count).u32(fields.materialIndex);
    }
    Bytes native;
    native.u32(4);
    for (const Bytes& chain : fields.meshChains) {
        native.u32(static_cast<std::uint32_t>(chain.size())).u32(0).append(chain.span());
    }
    Bytes geometryExtension = rwSection(0x50E, meshes);
    geometryExtension.append(rwSection(0x510, rwSection(graphics::kRwStruct, native)).span());
    Bytes geometry = rwSection(graphics::kRwStruct, geometryStruct);
    geometry.append(rwSection(0x08, list).span()).append(rwSection(graphics::kRwExtension, geometryExtension).span());

    // Atomic: struct (an arbitrary frame index, as the game's files have), geometry, extension.
    Bytes atomicStruct;
    atomicStruct.u32(0x1234).u32(0).u32(5).u32(0);
    Bytes rights;
    rights.u32(3).u32(fields.pipeline);
    Bytes plugin;
    f32(f32(plugin, fields.positionScale), fields.secondScale).u32(0);
    Bytes atomicExtension = rwSection(0x1F, rights);
    atomicExtension.append(rwSection(0x3F0, plugin).span());
    Bytes atomic = rwSection(graphics::kRwStruct, atomicStruct);
    atomic.append(rwSection(0x0F, geometry).span()).append(rwSection(graphics::kRwExtension, atomicExtension).span());
    return rwSection(0x14, atomic);
}

/// An atomic sector of a streamed world: an empty box from `lo` to `hi` and, when `index` >= 0, sector plugin data.
inline Bytes worldSector(std::array<float, 3> lo, std::array<float, 3> hi, std::int32_t index, std::uint32_t part,
                         std::array<float, 3> origin) {
    Bytes info;
    info.u32(0).u32(0).u32(0);
    for (const float v : lo) {
        f32(info, v);
    }
    for (const float v : hi) {
        f32(info, v);
    }
    info.u32(0).u32(0);
    Bytes plugin;
    plugin.u32(static_cast<std::uint32_t>(index)).u32(part);
    for (const float v : origin) {
        f32(plugin, v);
    }
    Bytes extension = rwSection(0x510, Bytes{}.u32(4).u32(0));
    extension.append(rwSection(0x3F1, plugin).span());
    Bytes body = rwSection(graphics::kRwStruct, info);
    body.append(rwSection(graphics::kRwExtension, extension).span());
    return rwSection(0x09, body);
}

/// A world stream: the part count, an empty texture dictionary, and a world whose BSP is one plane sector over the
/// two sectors given (or the one sector alone). `planes` and `sectors` go into the world header.
inline Bytes worldStream(std::uint32_t partCount, const std::vector<Bytes>& leaves, std::uint32_t planes,
                         std::uint32_t sectors) {
    Bytes info;
    info.u32(0);
    f32(f32(f32(info, 0.0F), 0.0F), 0.0F);
    info.u32(0).u32(0).u32(planes).u32(sectors).u32(0).u32(0x4101003D);
    f32(f32(f32(info, 100.0F), 100.0F), 100.0F);
    f32(f32(f32(info, -100.0F), -100.0F), -100.0F);
    Bytes world = rwSection(graphics::kRwStruct, info);
    world.append(rwSection(0x08, rwSection(graphics::kRwStruct, Bytes{}.u32(0))).span());
    if (leaves.size() == 1) {
        world.append(leaves[0].span());
    } else {
        Bytes plane;
        plane.u32(0);
        f32(plane, 0.0F).u32(1).u32(1);
        f32(f32(plane, 0.0F), 0.0F);
        Bytes body = rwSection(graphics::kRwStruct, plane);
        for (const Bytes& leaf : leaves) {
            body.append(leaf.span());
        }
        world.append(rwSection(0x0A, body).span());
    }
    Bytes stream;
    stream.u32(partCount).append(texDictionary({}).span()).append(rwSection(0x0B, world).span());
    return stream;
}

/// A part file: the header with `hash`, an empty texture dictionary, then the `{index, atomic}` records.
inline Bytes partFile(std::uint32_t hash, const std::vector<std::pair<std::uint32_t, Bytes>>& records) {
    Bytes file;
    file.header(1, 0, 0, hash).append(texDictionary({}).span()).u32(static_cast<std::uint32_t>(records.size()));
    for (const auto& [index, atomic] : records) {
        file.u32(index).append(atomic.span());
    }
    return file;
}

} // namespace coney::test
