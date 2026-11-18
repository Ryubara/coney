// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/pixel_texture.h"

#include "platform/movie_screen.h"
#include "platform/render_engine.h"

namespace coney::platform {

// Stubs until the GPU renderer's textures land.
bool PixelTexture::write(std::span<const std::uint8_t> /*rgba*/, int /*width*/, int /*height*/) { return false; }

const graphics::Texture* PixelMovieScreen::upload(std::span<const std::uint8_t> rgba, int width, int height) {
    return m_texture.write(rgba, width, height) ? &m_texture : nullptr;
}

std::unique_ptr<movies::MovieScreen> makeMovieScreen(RenderBackend backend) {
    if (backend == RenderBackend::OpenGl) {
        return std::make_unique<RasterMovieScreen>();
    }
    return nullptr;
}

} // namespace coney::platform
