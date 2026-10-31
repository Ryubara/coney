// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/asset_store.h"

namespace coney::graphics {

// Stubs so the tests compile and fail first; the next commit implements the store.

std::expected<MeshId, Error> AssetStore::addMesh(MeshData) { return MeshId{}; }

std::expected<TextureId, Error> AssetStore::addTexture(TextureData) { return TextureId{}; }

const MeshData* AssetStore::mesh(MeshId) const { return nullptr; }

const TextureData* AssetStore::texture(TextureId) const { return nullptr; }

bool AssetStore::removeMesh(MeshId) { return false; }

bool AssetStore::removeTexture(TextureId) { return false; }

} // namespace coney::graphics
