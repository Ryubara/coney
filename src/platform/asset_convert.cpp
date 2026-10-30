// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/asset_convert.h"

namespace coney::platform {

// Stubs so the tests compile and fail first; the next commit implements the conversions.

std::expected<graphics::MeshData, Error> meshFromRw(const rw::Geometry&, const SkinVertexData*) {
    return graphics::MeshData{};
}

std::expected<graphics::TextureData, Error> textureFromRw(rw::Texture&) { return graphics::TextureData{}; }

} // namespace coney::platform
