// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "world/world_streams.h"

// A RenderWare 3.7 clump as the characters store it, read without RenderWare: its frames with their HAnim data, its
// one geometry (PS2 native, with the mesh plugin, the native data and a PS2 skin), its materials and its atomic.
// librw reads clumps, but neither the game's packed PS2 vertex layout nor the skin's per-vertex weights inside it;
// reading the stream here keeps the format checks and the model platform-neutral, as the world's streams are
// (docs/research/world.md#coneys-implementation). The stream format is RenderWare's, as librw reads it; what the
// characters hold is in docs/research/characters.md#files.

namespace coney::characters {

/// RenderWare section ids of a clump.
inline constexpr std::uint32_t kRwClump = 0x10;
inline constexpr std::uint32_t kRwFrameList = 0x0E;
inline constexpr std::uint32_t kRwGeometryList = 0x1A;
inline constexpr std::uint32_t kRwGeometry = 0x0F;
inline constexpr std::uint32_t kRwMaterialList = 0x08;
inline constexpr std::uint32_t kRwMaterial = 0x07;
inline constexpr std::uint32_t kRwTexture = 0x06;
inline constexpr std::uint32_t kRwAtomic = 0x14;
inline constexpr std::uint32_t kRwRightToRender = 0x1F;
inline constexpr std::uint32_t kRwHAnim = 0x11E;
inline constexpr std::uint32_t kRwSkin = 0x116;
inline constexpr std::uint32_t kRwBinMesh = 0x50E;
inline constexpr std::uint32_t kRwNativeData = 0x510;

/// One frame of the clump: its matrix relative to its parent, and its HAnim data when it has some.
struct ClumpFrame {
    anim::Mat34 matrix;                  ///< RenderWare's right, up, at and pos, relative to the parent.
    std::int32_t parent = -1;            ///< Index of the parent frame, -1 for the root.
    std::optional<std::int32_t> hanimId; ///< The bone id of its HAnim extension (0x11E), when it has one.
};

/// One node of the HAnim hierarchy, in the hierarchy's order.
struct HAnimNode {
    std::int32_t id = 0;     ///< The bone id, as the frames' HAnim extensions name it.
    std::int32_t index = 0;  ///< The node's index (its place in the hierarchy).
    std::uint32_t flags = 0; ///< RenderWare's push (2) and pop (1) flags, which give the tree's shape.
};

/// A material: its colour and its texture's name.
struct ClumpMaterial {
    std::array<std::uint8_t, 4> colour{255, 255, 255, 255}; ///< RGBA.
    std::string texture;                                    ///< Empty for an untextured material.
    std::uint32_t textureFilter = 0; ///< The texture's filter mode and addressing, packed as RenderWare does.
};

/// One mesh of the geometry: its vertex count, its material and its PS2 DMA chain.
struct ClumpMesh {
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = 0;
    std::span<const std::byte> nativeData; ///< Points into the stream the clump was read from.
};

/// The PS2 skin plugin (0x116) of the geometry.
struct ClumpSkin {
    std::uint32_t boneCount = 0;          ///< Bones the inverse matrices are given for: the hierarchy's nodes.
    std::uint32_t maxWeights = 0;         ///< At most this many weights a vertex.
    std::vector<std::uint8_t> usedBones;  ///< The bones any vertex uses.
    std::vector<anim::Mat34> inverseBind; ///< Per node index: the inverse of the bone's rest transform in model space.
};

/// What a character's clump holds, as far as Coney uses it.
struct ClumpData {
    std::vector<ClumpFrame> frames;
    std::vector<HAnimNode> hierarchy; ///< From the frame whose HAnim extension carries the node list.
    std::uint32_t geometryFlags = 0;  ///< The geometry's format word: flags, texture-coordinate sets, native bit.
    std::uint32_t triangleCount = 0;  ///< From the geometry header.
    std::uint32_t vertexCount = 0;    ///< From the geometry header.
    bool triangleStrips = false;      ///< The mesh plugin's flag: the meshes are strips.
    std::vector<ClumpMaterial> materials;
    std::vector<ClumpMesh> meshes;
    ClumpSkin skin;
    std::uint32_t atomicFrame = 0;               ///< The frame the atomic hangs from.
    world::AtomicPluginData scales;              ///< The game's atomic plugin (0x3F0): the vertex scales.
    std::optional<std::uint32_t> atomicPipeline; ///< Right-to-render data of the atomic.
};

/// Reads a clump from `stream`, which starts with the clump section (0x10). The clump must hold exactly one atomic
/// with PS2 native geometry, a mesh plugin, native data and a PS2 skin, and every count and size is checked against
/// its parent section before it is used. Fails with ErrorCode::Truncated when a section runs past its parent, and
/// ErrorCode::Invalid naming what is missing or out of place. The returned spans point into `stream`.
[[nodiscard]] std::expected<ClumpData, Error> readCharacterClump(std::span<const std::byte> stream);

} // namespace coney::characters
