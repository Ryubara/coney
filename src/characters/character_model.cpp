// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_model.h"

#include <bit>
#include <format>
#include <map>
#include <utility>

#include "core/chunk_system.h"
#include "fileio/reader.h"
#include "gamemodes/load_entry_mode.h"
#include "graphics/ps2_world_mesh.h"
#include "graphics/rw_stream.h"

namespace coney::characters {

namespace {

// A weight word: the weight is a float whose low 10 bits are replaced by (bone + 1) << 2, 0 for an unused slot (the
// layout librw's PS2 skin code uses; confirmed on the disc data, characters.md).
constexpr std::uint32_t kBoneBits = 0x3FF;

// The bones of the pose that have no HAnim node: the root and the pelvis. Node id n is pose bone n + 2.
constexpr std::int32_t kFirstNodeBone = 2;

// The key under which identical packed vertices share one index.
using VertexKey = std::array<std::uint32_t, 12>;

// The key of a packed vertex: every attribute, packed into words.
VertexKey keyOf(const graphics::Ps2PackedVertex& v) {
    VertexKey key{};
    const auto pair16 = [](std::int16_t a, std::int16_t b) {
        return static_cast<std::uint32_t>(static_cast<std::uint16_t>(a)) |
               static_cast<std::uint32_t>(static_cast<std::uint16_t>(b)) << 16U;
    };
    key[0] = pair16(v.position[0], v.position[1]);
    key[1] = pair16(v.position[2], v.position[3]);
    key[2] = pair16(v.texCoords[0], v.texCoords[1]);
    key[3] = pair16(v.texCoords[2], v.texCoords[3]);
    key[4] = std::bit_cast<std::uint32_t>(v.colour);
    key[5] = std::bit_cast<std::uint32_t>(v.normal);
    for (std::size_t i = 0; i < kVertexWeights; ++i) {
        key[6 + i] = v.skin[i];
    }
    return key;
}

// Turns a packed vertex into a model vertex: scales, normal, and the weights split from their bones.
std::expected<SkinVertex, Error> unpackVertex(const graphics::Ps2PackedVertex& v, const world::AtomicPluginData& scales,
                                              std::size_t nodeCount) {
    SkinVertex out;
    out.position = anim::Vec3{static_cast<float>(v.position[0]) * scales.positionScale,
                              static_cast<float>(v.position[1]) * scales.positionScale,
                              static_cast<float>(v.position[2]) * scales.positionScale};
    out.normal = anim::Vec3{static_cast<float>(v.normal[0]) / 128.0F, static_cast<float>(v.normal[1]) / 128.0F,
                            static_cast<float>(v.normal[2]) / 128.0F};
    out.texCoords = {static_cast<float>(v.texCoords[0]) * scales.secondScale,
                     static_cast<float>(v.texCoords[1]) * scales.secondScale};
    out.secondTexCoords = {static_cast<float>(v.texCoords[2]) * scales.secondScale,
                           static_cast<float>(v.texCoords[3]) * scales.secondScale};
    for (std::size_t i = 0; i < kVertexWeights; ++i) {
        const std::uint32_t word = v.skin[i];
        const std::uint32_t tag = (word & kBoneBits) >> 2U;
        if (tag == 0) {
            continue; // an unused slot
        }
        if (tag > nodeCount) {
            return fail(ErrorCode::Invalid,
                        std::format("a vertex is weighted to bone {} of a hierarchy of {}", tag - 1, nodeCount));
        }
        out.bones[i] = static_cast<std::uint8_t>(tag - 1);
        out.weights[i] = std::bit_cast<float>(word & ~kBoneBits);
    }
    return out;
}

// Appends the triangles of a strip (or list) of vertex indices: in a strip every second triangle is turned round to
// keep one winding, and triangles with a repeated index (the joins between strips) are dropped.
void appendTriangles(const std::vector<std::uint16_t>& indices, bool strip, std::uint16_t material,
                     std::vector<ModelTriangle>& triangles) {
    const auto add = [&](std::uint16_t a, std::uint16_t b, std::uint16_t c) {
        if (a != b && b != c && a != c) {
            triangles.push_back(ModelTriangle{{a, b, c}, material});
        }
    };
    if (!strip) {
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            add(indices[i], indices[i + 1], indices[i + 2]);
        }
        return;
    }
    for (std::size_t i = 2; i < indices.size(); ++i) {
        if (i % 2 == 0) {
            add(indices[i - 2], indices[i - 1], indices[i]);
        } else {
            add(indices[i - 1], indices[i - 2], indices[i]);
        }
    }
}

// Reads the bone offsets: 34 entries of x, y, z and a fourth float (1 on the disc).
std::expected<void, Error> readBoneOffsets(std::span<const std::byte> chunk, CharacterModel& model) {
    if (chunk.size() != kBoneOffsetsBytes) {
        return fail(ErrorCode::Invalid,
                    std::format("a bone offset chunk holds {} bytes, not {}", chunk.size(), kBoneOffsetsBytes));
    }
    const auto floatAt = [chunk](std::size_t at) { return std::bit_cast<float>(io::loadU32Le(chunk.subspan(at, 4))); };
    for (std::size_t b = 0; b < anim::kPoseBones; ++b) {
        model.boneOffsets[b] = anim::Vec3{floatAt(b * 16), floatAt(b * 16 + 4), floatAt(b * 16 + 8)};
    }
    model.rootHeightOffset = floatAt(4);
    return {};
}

} // namespace

std::expected<CharacterModel, Error> decodeCharacterModel(std::span<const std::byte> clumpChunk,
                                                          std::span<const std::byte> boneOffsetChunk) {
    CharacterModel model;
    if (auto offsets = readBoneOffsets(boneOffsetChunk, model); !offsets) {
        return std::unexpected(std::move(offsets.error()));
    }
    auto start = graphics::findRwSection(clumpChunk, kRwClump);
    if (!start) {
        return std::unexpected(std::move(start.error()));
    }
    auto clump = readCharacterClump(clumpChunk.subspan(*start));
    if (!clump) {
        return std::unexpected(std::move(clump.error()));
    }
    for (const HAnimNode& node : clump->hierarchy) {
        if (node.id < 0 || node.id + kFirstNodeBone >= static_cast<std::int32_t>(anim::kPoseBones)) {
            return fail(ErrorCode::Invalid, std::format("HAnim bone id {} has no place in the 34-bone pose", node.id));
        }
    }

    // Decode every mesh, merging identical vertices, and build its triangles.
    std::map<VertexKey, std::uint16_t> indexOf;
    for (std::size_t m = 0; m < clump->meshes.size(); ++m) {
        const ClumpMesh& mesh = clump->meshes[m];
        auto decoded = graphics::decodePs2WorldMesh(mesh.nativeData, clump->triangleStrips);
        if (!decoded) {
            return fail(decoded.error().code, std::format("mesh {}: {}", m, decoded.error().message));
        }
        if (decoded->vertices.size() != mesh.indexCount || !decoded->attributes.skin) {
            return fail(ErrorCode::Invalid,
                        std::format("mesh {} decodes to {} vertices{}; its mesh record says {}", m,
                                    decoded->vertices.size(), decoded->attributes.skin ? "" : " without weights",
                                    mesh.indexCount));
        }
        std::vector<std::uint16_t> indices;
        indices.reserve(decoded->vertices.size());
        for (const graphics::Ps2PackedVertex& packed : decoded->vertices) {
            const auto [it, inserted] = indexOf.try_emplace(keyOf(packed), static_cast<std::uint16_t>(0));
            if (inserted) {
                if (model.vertices.size() > 0xFFFF) {
                    return fail(ErrorCode::Invalid, "the model has more than 65,536 distinct vertices");
                }
                auto vertex = unpackVertex(packed, clump->scales, clump->hierarchy.size());
                if (!vertex) {
                    return std::unexpected(std::move(vertex.error()));
                }
                it->second = static_cast<std::uint16_t>(model.vertices.size());
                model.vertices.push_back(*vertex);
            }
            indices.push_back(it->second);
        }
        model.stripVertices += static_cast<std::uint32_t>(indices.size());
        appendTriangles(indices, clump->triangleStrips, static_cast<std::uint16_t>(mesh.materialIndex),
                        model.triangles);
    }

    // The spans point into the caller's buffer; keep only the values.
    for (ClumpMesh& mesh : clump->meshes) {
        mesh.nativeData = {};
    }
    model.clump = std::move(*clump);
    return model;
}

std::expected<CharacterModel, Error> loadCharacterModel(const io::Wad& wad, const io::WadEntry& entry) {
    // The model's chunks are read raw: Coney's model reader runs on the bytes afterwards.
    const chunk::ChunkHandlerTable table;
    auto load = loadWadEntry(wad, entry, table);
    if (!load) {
        return std::unexpected(std::move(load.error()));
    }
    std::vector<chunk::ChunkData> clumps = load->stacks.takeChunks(kModelClumpChunk);
    std::vector<chunk::ChunkData> offsets = load->stacks.takeChunks(kBoneOffsetsChunk);
    if (clumps.empty() || offsets.empty()) {
        return fail(ErrorCode::NotFound, "the entry lacks the model's clump (0x47) or bone offsets (0x28)");
    }
    return decodeCharacterModel(clumps.front().bytes, offsets.front().bytes);
}

} // namespace coney::characters
