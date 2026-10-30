// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// @coney-only renderer data: the GPU renderer's own mesh format (docs/engine/asset-formats.md); the
// original draws its meshes as PS2 DMA chains, which have no counterpart here

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "core/error.h"
#include "graphics/render_device.h"

namespace coney::graphics {

/// The most texture-coordinate sets a vertex keeps: the world's base layer and its dual layer.
inline constexpr std::size_t kMeshTexCoordSets = 2;

/// The most bones that move one vertex: the skinning shader's four slots (platform/character_skin.h).
inline constexpr std::size_t kSkinSlots = 4;

/// One vertex in Coney's format, the same for every mesh so that one vertex layout draws them all. What a mesh does not
/// carry (MeshAttributes) holds its neutral value: a zero normal, white opaque prelighting, zero texture coordinates
/// and no bones (every weight 0).
struct MeshVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    Rgba prelit{255, 255, 255, 255};                                 ///< The baked vertex colour, RGBA.
    std::array<std::array<float, 2>, kMeshTexCoordSets> texCoords{}; ///< u, v of each set.
    std::array<std::uint8_t, kSkinSlots> bones{};                    ///< Indices into the bone palette.
    std::array<float, kSkinSlots> weights{};                         ///< 0 for an unused slot.

    friend bool operator==(const MeshVertex&, const MeshVertex&) = default;
};

/// Which vertex attributes a mesh carries; the position always.
struct MeshAttributes {
    bool normals = false;
    bool prelit = false;
    std::uint8_t texCoordSets = 0; ///< 0 to kMeshTexCoordSets.
    bool skinned = false;          ///< bones and weights are set.

    friend bool operator==(const MeshAttributes&, const MeshAttributes&) = default;
};

/// How a sub-mesh's indices make triangles.
enum class MeshTopology : std::uint8_t {
    TriangleList,  ///< Three indices per triangle.
    TriangleStrip, ///< Each index after the second makes a triangle with the two before it.
};

/// What a sub-mesh is drawn with, as the mesh's source names it. The texture is a name, as RenderWare's material holds
/// it; the frame description resolves it to a TextureId when it draws.
struct MeshMaterial {
    Rgba colour{255, 255, 255, 255};
    std::string texture; ///< Empty for an untextured material.

    friend bool operator==(const MeshMaterial&, const MeshMaterial&) = default;
};

/// A run of the mesh's indices drawn with one material: one draw.
struct SubMesh {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t material = 0; ///< Into MeshData::materials.

    friend bool operator==(const SubMesh&, const SubMesh&) = default;
};

/// A sphere holding the mesh, in the mesh's own space.
struct MeshBounds {
    std::array<float, 3> centre{};
    float radius = 0.0F;

    friend bool operator==(const MeshBounds&, const MeshBounds&) = default;
};

/// A mesh in Coney's format: converted once from the data librw reads, then drawn by either renderer and uploaded by
/// the GPU renderer as it is. Indices are 16 bits, as RenderWare's are.
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<MeshMaterial> materials;
    MeshAttributes attributes;
    MeshTopology topology = MeshTopology::TriangleList;
    MeshBounds bounds;

    friend bool operator==(const MeshData&, const MeshData&) = default;
};

/// Checks that `mesh` holds together: every sub-mesh's indices lie in `indices`, name a vertex that exists and a
/// material that exists, and the attributes are in range. ErrorCode::Invalid naming the first fault.
[[nodiscard]] std::expected<void, Error> validateMesh(const MeshData& mesh);

/// A 64-bit FNV-1a hash of everything `mesh` holds, floats by their bits: equal meshes hash equal on every platform.
/// For checks that print a hash instead of the data (the disc checks, LEGAL.md) and to tell two conversions apart.
[[nodiscard]] std::uint64_t hashMesh(const MeshData& mesh);

} // namespace coney::graphics
