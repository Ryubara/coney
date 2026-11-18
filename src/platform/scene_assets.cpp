// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only renderer data: the meshes and textures the frame description names by id
// (docs/engine/asset-formats.md); the original keeps RenderWare objects by pointer
#include "platform/scene_assets.h"

#include <algorithm>
#include <utility>

#include <rw.h>

#include "platform/asset_convert.h"

namespace coney::platform {

std::expected<graphics::TextureId, Error> SceneAssets::addTexture(rw::Texture& texture) {
    if (const graphics::TextureId held = textureOf(&texture); held.valid()) {
        return held;
    }
    auto converted = textureFromRw(texture);
    if (!converted) {
        return std::unexpected(std::move(converted.error()));
    }
    auto id = m_store.addTexture(std::move(*converted));
    if (!id) {
        return std::unexpected(std::move(id.error()));
    }
    m_textures.emplace(&texture, *id);
    return *id;
}

void SceneAssets::removeTexture(graphics::TextureId id) {
    if (!m_store.removeTexture(id)) {
        return;
    }
    // The librw texture it came from is forgotten too, so one made later at the same address is a new texture.
    const auto found =
        std::find_if(m_textures.begin(), m_textures.end(), [id](const auto& entry) { return entry.second == id; });
    if (found != m_textures.end()) {
        m_textures.erase(found);
    }
}

graphics::TextureId SceneAssets::textureOf(const rw::Texture* texture) const {
    const auto found = m_textures.find(texture);
    return found != m_textures.end() ? found->second : graphics::TextureId{};
}

std::expected<graphics::MeshId, Error> SceneAssets::addAtomic(rw::Atomic& atomic) {
    if (atomic.geometry == nullptr) {
        return std::unexpected(Error{ErrorCode::Invalid, "the atomic has no geometry"});
    }
    auto converted = meshFromRw(*atomic.geometry);
    if (!converted) {
        return std::unexpected(std::move(converted.error()));
    }
    auto id = m_store.addMesh(std::move(*converted));
    if (!id) {
        return std::unexpected(std::move(id.error()));
    }
    // Each material's texture by id, in the geometry's material list order, as meshFromRw() lists the materials.
    MeshEntry entry{.atomic = &atomic, .textures = {}};
    const rw::Geometry& geometry = *atomic.geometry;
    for (rw::int32 m = 0; m < geometry.matList.numMaterials; ++m) {
        entry.textures.push_back(textureOf(geometry.matList.materials[m]->texture));
    }
    m_meshes.emplace(id->value, std::move(entry));
    return *id;
}

void SceneAssets::removeMesh(graphics::MeshId id) {
    m_store.removeMesh(id);
    m_meshes.erase(id.value);
}

rw::Atomic* SceneAssets::atomic(graphics::MeshId id) const {
    const auto found = m_meshes.find(id.value);
    return found != m_meshes.end() ? found->second.atomic : nullptr;
}

std::span<const graphics::TextureId> SceneAssets::materialTextures(graphics::MeshId id) const {
    const auto found = m_meshes.find(id.value);
    if (found == m_meshes.end()) {
        return {};
    }
    return found->second.textures;
}

} // namespace coney::platform
