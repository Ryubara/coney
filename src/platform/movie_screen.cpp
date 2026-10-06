// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/movie_screen.h"

#include <algorithm>
#include <cstddef>

#include <rw.h>

namespace coney::platform {

namespace {

// A texture raster of 32-bit RGBA without mip levels: a movie frame is drawn at its own size.
constexpr rw::int32 kRasterFormat =
    static_cast<rw::int32>(rw::Raster::TEXTURE) | static_cast<rw::int32>(rw::Raster::C8888);

} // namespace

MovieTexture::~MovieTexture() { release(); }

void MovieTexture::release() {
    if (m_texture != nullptr) {
        m_texture->destroy(); // destroys its raster too
        m_texture = nullptr;
    }
    m_width = 0;
    m_height = 0;
}

bool MovieTexture::write(std::span<const std::uint8_t> rgba, int width, int height) {
    if (rgba.size() < static_cast<std::size_t>(width) * height * 4) {
        return false;
    }
    if (m_texture == nullptr || width != m_width || height != m_height) {
        release();
        rw::Raster* raster = rw::Raster::create(width, height, 32, kRasterFormat);
        if (raster == nullptr) {
            return false;
        }
        m_texture = rw::Texture::create(raster);
        m_texture->setFilter(rw::Texture::LINEAR);
        m_width = width;
        m_height = height;
    }
    rw::Raster* raster = m_texture->raster;
    rw::uint8* pixels = raster->lock(0, rw::Raster::LOCKWRITE | rw::Raster::LOCKNOFETCH);
    if (pixels == nullptr) {
        return false;
    }
    const auto row = static_cast<std::ptrdiff_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        // The 2D pass reads this raster's rows bottom first (a frame written top first showed upside down), so the
        // frame goes in upside down to be drawn the right way up.
        std::copy_n(rgba.begin() + (y * row), row,
                    pixels + (static_cast<std::ptrdiff_t>(height - 1 - y) * raster->stride));
    }
    raster->unlock(0);
    return true;
}

const graphics::Texture* RasterMovieScreen::upload(std::span<const std::uint8_t> rgba, int width, int height) {
    return m_texture.write(rgba, width, height) ? &m_texture : nullptr;
}

} // namespace coney::platform
