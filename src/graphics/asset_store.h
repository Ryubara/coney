// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// @coney-only renderer data: the store both renderers draw meshes and textures from
// (docs/engine/asset-formats.md); the original keeps RenderWare objects by pointer

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <unordered_map>

#include "core/error.h"
#include "graphics/mesh_data.h"
#include "graphics/texture_data.h"

namespace coney::graphics {

/// A mesh in an AssetStore. 0 is no mesh; a store never hands out the same id twice, so a stale id finds nothing rather
/// than another mesh.
struct MeshId {
    std::uint32_t value = 0;

    /// Whether the id names a mesh at all (it may still have been removed).
    [[nodiscard]] bool valid() const { return value != 0; }

    friend auto operator<=>(const MeshId&, const MeshId&) = default;
};

/// A texture in an AssetStore, as MeshId is for meshes.
struct TextureId {
    std::uint32_t value = 0;

    /// Whether the id names a texture at all (it may still have been removed).
    [[nodiscard]] bool valid() const { return value != 0; }

    friend auto operator<=>(const TextureId&, const TextureId&) = default;
};

/// Converted meshes and textures held by id, so the frame description names what it draws by id and both renderers
/// (and later a loader thread) share the data. Only data that holds together (validateMesh(), validateTexture()) is
/// taken, so a renderer can upload whatever it finds here without checking it again. Not thread-safe.
class AssetStore {
  public:
    /// Takes `mesh` and returns its new id; ErrorCode::Invalid (and nothing stored) when validateMesh() refuses it. A
    /// store hands out at most 2^32 - 1 mesh ids in its life; one more is a broken invariant (CONEY_ASSERT).
    [[nodiscard]] std::expected<MeshId, Error> addMesh(MeshData mesh);

    /// Takes `texture` and returns its new id; ErrorCode::Invalid (and nothing stored) when validateTexture() refuses
    /// it. At most 2^32 - 1 texture ids, as for meshes.
    [[nodiscard]] std::expected<TextureId, Error> addTexture(TextureData texture);

    /// The mesh `id` names, or null when it names none (0, removed, or never handed out). Valid until it is removed.
    [[nodiscard]] const MeshData* mesh(MeshId id) const;

    /// The texture `id` names, or null when it names none. Valid until it is removed.
    [[nodiscard]] const TextureData* texture(TextureId id) const;

    /// Drops the mesh `id` names; false when it names none.
    bool removeMesh(MeshId id);

    /// Drops the texture `id` names; false when it names none.
    bool removeTexture(TextureId id);

    /// How many meshes are held.
    [[nodiscard]] std::size_t meshCount() const { return m_meshes.size(); }

    /// How many textures are held.
    [[nodiscard]] std::size_t textureCount() const { return m_textures.size(); }

  private:
    std::unordered_map<std::uint32_t, MeshData> m_meshes;
    std::unordered_map<std::uint32_t, TextureData> m_textures;
    std::uint32_t m_lastMesh = 0;    // the last mesh id handed out
    std::uint32_t m_lastTexture = 0; // the last texture id handed out
};

} // namespace coney::graphics
