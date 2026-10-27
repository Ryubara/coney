// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "characters/character_model.h"
#include "debug/entity_inspect.h"

// The Entity inspector's Mesh group (docs/guides/debug-menu.md#entity-inspector): what a model is made of and what the
// renderer spends on it, counted from the decoded model the way the play mode draws it (one librw atomic per pass, one
// draw call per material). Platform-neutral: the platform hands in the texture's name and size and what it drew last
// frame. Coney's own debug tooling: the original has nothing like it.

namespace coney::debug {

/// One material of a model.
struct MaterialStats {
    std::array<std::uint8_t, 4> colour{255, 255, 255, 255}; ///< RGBA.
    std::size_t triangles = 0;                              ///< Triangles drawn with it (0: an empty material).
    std::string texture;                                    ///< The texture it is drawn with; empty for none.
    int textureWidth = 0;                                   ///< 0 when not known.
    int textureHeight = 0;
};

/// The texture a model is drawn with, as the platform has it loaded.
struct TextureInfo {
    std::string name; ///< Empty: untextured.
    int width = 0;
    int height = 0;
};

/// What a model is made of.
struct MeshStats {
    std::size_t atomics = 0;    ///< librw atomics drawn for one pass.
    std::size_t geometries = 0; ///< Geometries they hold.
    std::size_t triangles = 0;
    std::size_t vertices = 0;
    std::vector<MaterialStats> materials;
    std::size_t textures = 0;        ///< Distinct textures among the materials.
    std::size_t hierarchyBones = 0;  ///< Nodes of the HAnim hierarchy.
    std::size_t skinBones = 0;       ///< Bones the skin gives inverse matrices for.
    std::size_t usedBones = 0;       ///< Bones some vertex is weighted to.
    std::size_t declaredWeights = 0; ///< The skin's most weights a vertex.
    /// Vertices by how many non-zero weights they carry: 0, 1, 2, 3 and 4.
    std::array<std::size_t, 5> verticesByWeights{};
    std::size_t drawsPerPass = 0; ///< Draw calls one pass makes: one per material, an empty one too.
};

/// How the platform drew a model last frame.
struct MeshDrawn {
    std::size_t passes = 0;       ///< Passes drawn: 0 not drawn (faded out, culled), 1 the texture, 2 with the blood.
    float fade = 1.0F;            ///< The distance fade it was drawn at (1: opaque).
    std::string skinning = "CPU"; ///< Where it was skinned: "CPU" or "GPU".
};

/// The statistics of a character model drawn with `texture` on every material, as the play mode draws it
/// (platform::CharacterMesh): one atomic holding one geometry with the model's distinct vertices, its triangles and
/// one material per clump material. librw makes one mesh per material, an empty one included, and draws each
/// (docs/guides/debug-menu.md#mesh-group).
[[nodiscard]] MeshStats characterMeshStats(const characters::CharacterModel& model, const TextureInfo& texture);

/// The draw calls a model with `stats` made when drawn as `drawn` says.
[[nodiscard]] std::size_t drawCallsOf(const MeshStats& stats, const MeshDrawn& drawn);

/// The mean of non-zero weights per vertex; 0 for no vertices.
[[nodiscard]] double meanWeights(const MeshStats& stats);

/// The Mesh group: atomics, geometries, triangles, vertices, materials (one line each with its triangles, colour and
/// texture), textures, bones, weights per vertex, draw calls last frame, the level of detail and the skinning.
[[nodiscard]] WatchGroup describeMesh(const MeshStats& stats, const MeshDrawn& drawn);

} // namespace coney::debug
