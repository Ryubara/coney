// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's textures held as pixels, and the movie screen each renderer gets
// (docs/engine/gpu-renderer.md#textures). Coney's own: the expected values are the classes' contracts
// (src/platform/pixel_texture.h); nothing here needs a window or a GPU.
#include "platform/pixel_texture.h"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "platform/movie_screen.h"
#include "platform/render_engine.h"

using coney::platform::PixelTexture;
using coney::platform::RenderBackend;

TEST_CASE("a pixel texture keeps its frame and counts its revisions", "[render][gpu-renderer]") {
    PixelTexture texture(coney::graphics::TextureFilter::Nearest);
    CHECK(texture.width() == 0);
    CHECK(texture.pixels().empty());
    CHECK(texture.revision() == 0);
    CHECK(texture.filter() == coney::graphics::TextureFilter::Nearest);

    const std::vector<std::uint8_t> frame{1, 2, 3, 4, 5, 6, 7, 8};
    REQUIRE(texture.write(frame, 2, 1));
    CHECK(texture.width() == 2);
    CHECK(texture.height() == 1);
    CHECK(std::vector<std::uint8_t>(texture.pixels().begin(), texture.pixels().end()) == frame);
    CHECK(texture.revision() == 1);

    // Too few bytes for the size, or no size: refused, the texture as it was.
    CHECK_FALSE(texture.write(frame, 2, 2));
    CHECK_FALSE(texture.write(frame, 0, 1));
    CHECK(texture.width() == 2);
    CHECK(texture.revision() == 1);

    REQUIRE(texture.write(std::vector<std::uint8_t>{9, 9, 9, 9}, 1, 1));
    CHECK(texture.width() == 1);
    CHECK(texture.pixels().size() == 4);
    CHECK(texture.revision() == 2);
}

TEST_CASE("each renderer gets the movie screen it draws from", "[render][gpu-renderer][movies]") {
    // NULL draws nothing, so it gets none.
    CHECK(coney::platform::makeMovieScreen(RenderBackend::Null) == nullptr);
    // OpenGL: a librw raster (only made by the first upload, which needs the GL3 device, so not made here).
    CHECK(dynamic_cast<coney::platform::RasterMovieScreen*>(
              coney::platform::makeMovieScreen(RenderBackend::OpenGl).get()) != nullptr);
    // The GPU renderer: the frame kept as pixels, handed back as the texture to draw.
    auto screen = coney::platform::makeMovieScreen(RenderBackend::Gpu);
    REQUIRE(screen != nullptr);
    const std::vector<std::uint8_t> frame{10, 20, 30, 255};
    const auto* texture = dynamic_cast<const PixelTexture*>(screen->upload(frame, 1, 1));
    REQUIRE(texture != nullptr);
    CHECK(texture->pixels()[1] == 20);
    CHECK(texture->filter() == coney::graphics::TextureFilter::Linear);
}
