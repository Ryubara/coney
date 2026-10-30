// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/asset_store.h"

#include <utility>

namespace coney::graphics {

std::expected<MeshId, Error> AssetStore::addMesh(MeshData mesh) {
    if (auto valid = validateMesh(mesh); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    // Ids count up from 1 and are never handed out again, so a stale id finds nothing.
    const MeshId id{++m_lastMesh};
    m_meshes.emplace(id.value, std::move(mesh));
    return id;
}

std::expected<TextureId, Error> AssetStore::addTexture(TextureData texture) {
    if (auto valid = validateTexture(texture); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    const TextureId id{++m_lastTexture};
    m_textures.emplace(id.value, std::move(texture));
    return id;
}

const MeshData* AssetStore::mesh(MeshId id) const {
    const auto found = m_meshes.find(id.value);
    return found == m_meshes.end() ? nullptr : &found->second;
}

const TextureData* AssetStore::texture(TextureId id) const {
    const auto found = m_textures.find(id.value);
    return found == m_textures.end() ? nullptr : &found->second;
}

bool AssetStore::removeMesh(MeshId id) { return m_meshes.erase(id.value) != 0; }

bool AssetStore::removeTexture(TextureId id) { return m_textures.erase(id.value) != 0; }

} // namespace coney::graphics
