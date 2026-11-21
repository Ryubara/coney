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

struct SkinVertexData; // platform/character_skin.h

/// The meshes and textures the frame description's 3D draws name (graphics::Draw3d), converted into Coney's own
/// formats (platform/asset_convert.h) and held by id in an AssetStore, with the librw atomic each mesh came from: the
/// GL3 renderer draws that atomic through librw as before, and the GPU renderer will draw the converted mesh. A
/// texture is known by the librw texture it was converted from, so a mesh's materials find theirs.
///
/// It borrows the atomics and textures: whoever adds one removes it before destroying it (WorldSet does both for the
/// streamed world's sectors and dictionaries). A converted texture is held while anything uses it: each addTexture()
/// and each mesh material that names it, as RenderWare keeps a texture while a material references it, so a mesh
/// whose material found its texture in another dictionary (a streamed part's) keeps it after that dictionary goes
/// (docs/engine/renderer.md#texture-lifetime). One per RenderEngine (RenderEngine::sceneAssets()). Not thread-safe.
///
/// Design: docs/engine/renderer.md#frame-description
class SceneAssets {
  public:
    /// Whether the assets convert what they are given. With `convert` false (the GL3 renderer, which draws the librw
    /// atomics and reads none of the converted data) a mesh still gets an id that names its atomic, but no mesh or
    /// texture is converted or held: addTexture() returns no id, and the store stays empty.
    /// `skinsInShaders` says whether the renderer they are for skins posed meshes in its shaders from the converted
    /// data (the GPU renderer: RenderEngine::skinsInShaders()), so a character's mesh need not be skinned on the CPU.
    explicit SceneAssets(bool convert = true, bool skinsInShaders = false)
        : m_convert(convert), m_skinsInShaders(skinsInShaders) {}

    /// Whether the assets convert (the constructor's `convert`).
    [[nodiscard]] bool converting() const { return m_convert; }
    /// Whether the renderer skins posed meshes in its shaders from the converted data (the constructor's
    /// `skinsInShaders`): a CharacterMesh is then posed by its bone palette alone
    /// (docs/engine/gpu-renderer.md#skinning).
    [[nodiscard]] bool skinsInShaders() const { return m_skinsInShaders; }

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

    /// Converts `texture` (graphics::TextureData, every level its raster has) into the store, holds it once and returns
    /// its id; the same id, held once more, when the texture is already held, and an invalid id, converting nothing,
    /// when the assets do not convert.
    /// Before the texture's raster is converted for drawing: a PS2 raster is read as loaded. Fails as
    /// platform::textureFromRw() or AssetStore::addTexture() does, holding nothing.
    [[nodiscard]] std::expected<graphics::TextureId, Error> addTexture(rw::Texture& texture);
    /// Takes `data` as the converted texture of `texture`, a texture Coney made itself (the test cards'), whose raster
    /// need not be readable: held as addTexture() holds one, under the same rules (an invalid id when the assets do
    /// not convert; the same id, held once more, when `texture` is already held, `data` then unused). Fails as
    /// AssetStore::addTexture() does, holding nothing.
    [[nodiscard]] std::expected<graphics::TextureId, Error> addTexture(rw::Texture& texture,
                                                                       graphics::TextureData data);
    /// Lets go of one hold of the texture `id` names (an addTexture()'s), and drops it from the store when no hold and
    /// no held mesh's material is left; nothing when it names none.
    void removeTexture(graphics::TextureId id);
    /// The id of the texture converted from `texture`; invalid when it is not held (or null).
    [[nodiscard]] graphics::TextureId textureOf(const rw::Texture* texture) const;

    /// Converts `atomic`'s plain geometry (graphics::MeshData) into the store, notes the id of each of its materials'
    /// textures (materialTextures(), invalid for a material with no texture or one not held), holding each of those
    /// textures until the mesh is removed, and keeps `atomic` for the GL3 renderer (atomic()); when the assets do not
    /// convert it only keeps the atomic, under a new id. Fails as platform::meshFromRw() or AssetStore::addMesh() does,
    /// holding nothing. The ids are noted once, and stay valid while the mesh is held, whoever added the textures
    /// (docs/engine/renderer.md#texture-lifetime). With `skin` (one entry per vertex), each converted vertex also takes
    /// its four bones and weights, for a renderer that skins the mesh (platform::meshFromRw()).
    [[nodiscard]] std::expected<graphics::MeshId, Error> addAtomic(rw::Atomic& atomic,
                                                                   const SkinVertexData* skin = nullptr);
    /// Drops the mesh `id` names, lets go of its materials' textures (removeTexture()) and forgets its atomic; nothing
    /// when it names none.
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
    // Lets go of one use of the texture `id` names, dropping it from the store at the last.
    void release(graphics::TextureId id);

    // A converted texture in the store: the librw texture it came from and how many uses hold it (addTexture() calls
    // and held meshes' materials).
    struct TextureEntry {
        const rw::Texture* source = nullptr;
        std::uint32_t uses = 0;
    };

    bool m_convert = true;
    bool m_skinsInShaders = false;
    Totals m_totals;
    std::uint32_t m_lastBareMesh = 0; // the last mesh id handed out for an atomic alone (no conversion)
    graphics::AssetStore m_store;
    std::unordered_map<std::uint32_t, MeshEntry> m_meshes;                  // by MeshId
    std::unordered_map<const rw::Texture*, graphics::TextureId> m_textures; // the id of each texture held
    std::unordered_map<std::uint32_t, TextureEntry> m_uses;                 // by TextureId
};

} // namespace coney::platform
