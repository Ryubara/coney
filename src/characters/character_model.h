// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "characters/clump_reader.h"
#include "core/error.h"
#include "fileio/wad.h"

// A character's model, decoded into plain skinned vertices and triangles: what the model resource's clump (chunk
// 0x47) and bone offsets (chunk 0x28) hold. Format: docs/research/characters.md#files and #character-geometry.

namespace coney::characters {

/// The chunk types of a model resource: the clump (Preinstance Object) and the bones' offsets.
inline constexpr std::uint32_t kModelClumpChunk = 0x47;
inline constexpr std::uint32_t kBoneOffsetsChunk = 0x28;

/// Bytes of the bone offset chunk: 34 entries of four floats.
inline constexpr std::size_t kBoneOffsetsBytes = anim::kPoseBones * 16;

/// Weights a vertex can carry.
inline constexpr std::size_t kVertexWeights = 4;

/// One vertex of a character, in the clump's model space (RenderWare's axes), unskinned.
struct SkinVertex {
    anim::Vec3 position;                              ///< Packed integers times the atomic's position scale.
    anim::Vec3 normal;                                ///< Packed signed bytes over 128.
    std::array<float, 2> texCoords{};                 ///< The first set, times the atomic's texture-coordinate scale.
    std::array<std::uint8_t, kVertexWeights> bones{}; ///< Node indices of the HAnim hierarchy (and of the skin).
    std::array<float, kVertexWeights> weights{};      ///< 0 for an unused slot.
};

/// One triangle: three indices into the vertices, and its material.
struct ModelTriangle {
    std::array<std::uint16_t, 3> vertices{};
    std::uint16_t material = 0;
};

/// A character's model, decoded.
struct CharacterModel {
    ClumpData clump;                      ///< Frames, hierarchy, materials, skin; no native data spans.
    std::vector<SkinVertex> vertices;     ///< Distinct vertices: those the strips share are merged.
    std::vector<ModelTriangle> triangles; ///< From the strips, degenerate ones dropped.
    std::array<anim::Vec3, anim::kPoseBones> boneOffsets{}; ///< Chunk 0x28: each pose bone's offset from its parent.
    float rootHeightOffset = 0.0F;                          ///< Chunk 0x28, the float at byte 4 (anim::Skeleton).
    std::uint32_t stripVertices = 0;                        ///< Vertices of all strips, as the mesh plugin counts them.
};

/// Decodes a model from the RenderWare stream inside its clump chunk (it is searched for the clump section) and its
/// bone offset chunk (exactly 544 bytes). Every mesh's DMA chain is decoded with graphics::decodePs2WorldMesh() and
/// must give the mesh plugin's vertex count, five slots a vertex (with bone weights). Fails as readCharacterClump()
/// and the decoder do, and with ErrorCode::Invalid for a bone offset chunk of another size, a vertex naming a bone
/// beyond the hierarchy, a hierarchy bone id outside 0-31 (the pose has 34 bones) or more than 65,536 distinct
/// vertices.
[[nodiscard]] std::expected<CharacterModel, Error> decodeCharacterModel(std::span<const std::byte> clumpChunk,
                                                                        std::span<const std::byte> boneOffsetChunk);

/// Loads the model resource `entry` (its chunks read raw) and decodes its clump and bone offsets. Fails with
/// ErrorCode::NotFound when either chunk is missing, and as the load and decodeCharacterModel() do.
/// @orig 0x001783d0 ResourceManager_LoadCharacterModel (unknown)
[[nodiscard]] std::expected<CharacterModel, Error> loadCharacterModel(const io::Wad& wad, const io::WadEntry& entry);

} // namespace coney::characters
