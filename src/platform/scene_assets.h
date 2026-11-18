// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// @coney-only renderer data: the meshes and textures the frame description names by id
// (docs/engine/asset-formats.md); the original keeps RenderWare objects by pointer

#include <cstdint>
#include <expected>
#include <span>
#include <unordered_map>
#include <vector>

#include "core/error.h"
#include "graphics/asset_store.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Texture;
} // namespace rw

namespace coney::platform {

/// The meshes and textures the frame description's 3D draws name (graphics::Draw3d), converted into Coney's own
/// formats (platform/asset_convert.h) and held by id in an AssetStore, with the librw atomic each mesh came from: the
/// GL3 renderer draws that atomic through librw as before, and the GPU renderer will draw the converted mesh. A
/// texture is known by the librw texture it was converted from, so a mesh's materials find theirs.
///
/// It borrows the atomics and textures: whoever adds one removes it before destroying it (WorldSet does both for the
/// streamed world's sectors and dictionaries). One per RenderEngine (RenderEngine::sceneAssets()). Not thread-safe.
///
/// Design: docs/engine/renderer.md#frame-description
class SceneAssets {
  public:
    /// Whether the assets convert what they are given. With `convert` false (the GL3 renderer, which draws the librw
    /// atomics and reads none of the converted data) a mesh still gets an id that names its atomic, but no mesh or
    /// texture is converted or held: addTexture() returns no id, and the store stays empty.
    explicit SceneAssets(bool convert = true) : m_convert(convert) {}

    /// Whether the assets convert (the constructor's `convert`).
    [[nodiscard]] bool converting() const { return m_convert; }

    /// What the conversion has cost so far (#1422, docs/engine/renderer.md#frame-description): everything converted
    /// since the assets were made, and the bytes of converted data the store holds now and at most. The `--bench`
    /// report carries them.
    struct Totals {
        std::uint64_t textures = 0;     ///< Textures converted, held since or not.
        std::uint64_t meshes = 0;       ///< Meshes converted.
        std::uint64_t textureBytes = 0; ///< The texels the store holds now.
        std::uint64_t meshBytes = 0;    ///< The vertices, indices, sub-meshes and materials the store holds now.
        std::uint64_t peakBytes = 0;    ///< The most textureBytes + meshBytes held at once.
        std::uint64_t nanoseconds = 0;  ///< The time spent converting, while the profiler measures (core/perf.h).
    };
    /// The totals.
    [[nodiscard]] const Totals& totals() const { return m_totals; }

    /// Converts `texture` (graphics::TextureData, every level its raster has) into the store and returns its id; the
    /// same id when the texture is already held, and an invalid id, converting nothing, when the assets do not convert.
    /// Before the texture's raster is converted for drawing: a PS2 raster is
    /// read as loaded. Fails as platform::textureFromRw() or AssetStore::addTexture() does, holding nothing.
    [[nodiscard]] std::expected<graphics::TextureId, Error> addTexture(rw::Texture& texture);
    /// Drops the texture `id` names; nothing when it names none.
    void removeTexture(graphics::TextureId id);
    /// The id of the texture converted from `texture`; invalid when it is not held (or null).
    [[nodiscard]] graphics::TextureId textureOf(const rw::Texture* texture) const;

    /// Converts `atomic`'s plain geometry (graphics::MeshData) into the store, notes the id of each of its materials'
    /// textures (materialTextures(), invalid for a material with no texture or one not held) and keeps `atomic` for
    /// the GL3 renderer (atomic()); when the assets do not convert it only keeps the atomic, under a new id. Fails as
    /// platform::meshFromRw() or AssetStore::addMesh() does, holding nothing. The ids are noted once: one whose texture
    /// is dropped first goes stale. A streamed part's materials take theirs from its own dictionary or a world's on the
    /// disc, never another part's (WorldSet::borrowedTextures() guards it: docs/engine/renderer.md#texture-lifetime).
    [[nodiscard]] std::expected<graphics::MeshId, Error> addAtomic(rw::Atomic& atomic);
    /// Drops the mesh `id` names and forgets its atomic; nothing when it names none.
    void removeMesh(graphics::MeshId id);
    /// The atomic the mesh `id` was converted from; null when it names none.
    [[nodiscard]] rw::Atomic* atomic(graphics::MeshId id) const;
    /// The texture of each of mesh `id`'s materials, in MeshData::materials' order; empty when it names none.
    [[nodiscard]] std::span<const graphics::TextureId> materialTextures(graphics::MeshId id) const;

    /// The converted meshes and textures.
    [[nodiscard]] const graphics::AssetStore& store() const { return m_store; }

  private:
    // What is kept beside a mesh of the store.
    struct MeshEntry {
        rw::Atomic* atomic = nullptr;
        std::vector<graphics::TextureId> textures;
    };

    // The bytes a converted mesh or texture takes in the store, for the totals.
    [[nodiscard]] static std::uint64_t bytesOf(const graphics::MeshData& mesh);
    [[nodiscard]] static std::uint64_t bytesOf(const graphics::TextureData& texture);
    // Counts `bytes` more held in `kind` (the texture or mesh bytes) and lifts the peak.
    void hold(std::uint64_t& kind, std::uint64_t bytes);

    bool m_convert = true;
    Totals m_totals;
    std::uint32_t m_lastBareMesh = 0; // the last mesh id handed out for an atomic alone (no conversion)
    graphics::AssetStore m_store;
    std::unordered_map<std::uint32_t, MeshEntry> m_meshes;                  // by MeshId
    std::unordered_map<const rw::Texture*, graphics::TextureId> m_textures; // the texture each id came from
};

} // namespace coney::platform
