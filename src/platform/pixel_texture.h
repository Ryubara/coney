// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "graphics/render_device.h"
#include "graphics/texture_data.h"
#include "movies/movie_mode.h"

namespace coney::platform {

enum class RenderBackend : std::uint8_t;

/// A texture held as RGBA pixels in memory, for the GPU renderer, which uploads it when it draws it
/// (docs/engine/gpu-renderer.md#textures): a movie's frame, Coney's own test cards. The GL3 renderer's twin is
/// MovieTexture (movie_screen.h), which writes its frames into a librw raster instead.
///
/// Coney's own: nothing here ports the original.
class PixelTexture final : public graphics::Texture {
  public:
    /// An empty texture, sampled with `filter` once written (Nearest and MipNearest take the nearest texel, the rest
    /// blend the four nearest, as the GL3 renderer samples a texture of that filter with no mip levels).
    explicit PixelTexture(graphics::TextureFilter filter = graphics::TextureFilter::Linear) : m_filter(filter) {}

    [[nodiscard]] int width() const override { return m_width; }
    [[nodiscard]] int height() const override { return m_height; }

    /// Copies a `width` × `height` RGBA frame (rows top first) into the texture and counts a new revision. False, with
    /// the texture unchanged, when the size is not positive or `rgba` holds fewer than its bytes.
    bool write(std::span<const std::uint8_t> rgba, int width, int height);

    /// The pixels, RGBA, rows top first; empty before the first write().
    [[nodiscard]] std::span<const std::uint8_t> pixels() const { return m_pixels; }
    /// How many times write() has changed the pixels: a renderer holding a copy of revision N uploads again past it.
    [[nodiscard]] std::uint64_t revision() const { return m_revision; }
    /// How it is sampled.
    [[nodiscard]] graphics::TextureFilter filter() const { return m_filter; }

  private:
    graphics::TextureFilter m_filter;
    std::vector<std::uint8_t> m_pixels;
    int m_width = 0;
    int m_height = 0;
    std::uint64_t m_revision = 0;
};

/// The movie player's screen on the GPU renderer: frames go into a PixelTexture.
class PixelMovieScreen final : public movies::MovieScreen {
  public:
    const graphics::Texture* upload(std::span<const std::uint8_t> rgba, int width, int height) override;

  private:
    PixelTexture m_texture;
};

/// The movie player's screen for a renderer of `backend`: a RasterMovieScreen for OpenGL (movie_screen.h), a
/// PixelMovieScreen for the GPU renderer, none for NULL, which draws nothing.
[[nodiscard]] std::unique_ptr<movies::MovieScreen> makeMovieScreen(RenderBackend backend);

} // namespace coney::platform
