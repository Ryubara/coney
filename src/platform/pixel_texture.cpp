// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/pixel_texture.h"

#include <cstddef>

#include "platform/movie_screen.h"
#include "platform/render_engine.h"

namespace coney::platform {

bool PixelTexture::write(std::span<const std::uint8_t> rgba, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (rgba.size() < bytes) {
        return false;
    }
    m_pixels.assign(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(bytes));
    m_width = width;
    m_height = height;
    ++m_revision;
    return true;
}

const graphics::Texture* PixelMovieScreen::upload(std::span<const std::uint8_t> rgba, int width, int height) {
    return m_texture.write(rgba, width, height) ? &m_texture : nullptr;
}

std::unique_ptr<movies::MovieScreen> makeMovieScreen(RenderBackend backend) {
    switch (backend) {
    case RenderBackend::OpenGl:
        return std::make_unique<RasterMovieScreen>();
    case RenderBackend::Gpu:
        return std::make_unique<PixelMovieScreen>();
    case RenderBackend::Null:
        break;
    }
    return nullptr;
}

} // namespace coney::platform
