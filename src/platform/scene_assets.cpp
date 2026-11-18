// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only renderer data: the meshes and textures the frame description names by id
// (docs/engine/asset-formats.md); the original keeps RenderWare objects by pointer
#include "platform/scene_assets.h"

// Stubs: nothing is held yet.

namespace coney::platform {

std::expected<graphics::TextureId, Error> SceneAssets::addTexture(rw::Texture& /*texture*/) {
    return std::unexpected(Error{ErrorCode::Invalid, "not held yet"});
}

void SceneAssets::removeTexture(graphics::TextureId /*id*/) {}

graphics::TextureId SceneAssets::textureOf(const rw::Texture* /*texture*/) const { return {}; }

std::expected<graphics::MeshId, Error> SceneAssets::addAtomic(rw::Atomic& /*atomic*/) {
    return std::unexpected(Error{ErrorCode::Invalid, "not held yet"});
}

void SceneAssets::removeMesh(graphics::MeshId /*id*/) {}

rw::Atomic* SceneAssets::atomic(graphics::MeshId /*id*/) const { return nullptr; }

std::span<const graphics::TextureId> SceneAssets::materialTextures(graphics::MeshId /*id*/) const { return {}; }

} // namespace coney::platform
