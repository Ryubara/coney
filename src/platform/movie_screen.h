// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "graphics/render_device.h"
#include "movies/movie_mode.h"

namespace rw {
struct Raster;
struct Texture;
} // namespace rw

namespace coney::platform {

/// The texture a movie's frames are shown through: one 32-bit RGBA librw raster the size of the movie, rewritten
/// with every frame shown. RenderEngine::drawQuads() draws it like a sprite sheet's texture.
class MovieTexture final : public graphics::Texture {
  public:
    MovieTexture() = default;
    ~MovieTexture() override;
    MovieTexture(const MovieTexture&) = delete;
    MovieTexture& operator=(const MovieTexture&) = delete;
    MovieTexture(MovieTexture&&) = delete;
    MovieTexture& operator=(MovieTexture&&) = delete;

    [[nodiscard]] int width() const override { return m_width; }
    [[nodiscard]] int height() const override { return m_height; }

    /// Writes a `width` × `height` RGBA frame, making the raster first or again when the size changes. False when
    /// librw cannot make it.
    bool write(std::span<const std::uint8_t> rgba, int width, int height);

    /// The librw texture, for the renderer; null before the first write().
    [[nodiscard]] rw::Texture* rwTexture() const { return m_texture; }

  private:
    /// Frees the texture and its raster.
    void release();

    rw::Texture* m_texture = nullptr;
    int m_width = 0;
    int m_height = 0;
};

/// The movie player's screen on the OpenGL renderer: frames go into a MovieTexture.
class RasterMovieScreen final : public movies::MovieScreen {
  public:
    const graphics::Texture* upload(std::span<const std::uint8_t> rgba, int width, int height) override;

  private:
    MovieTexture m_texture;
};

} // namespace coney::platform
