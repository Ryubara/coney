// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/texture_grid.h"

#include <array>
#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::graphics::Extent;
using coney::graphics::layoutGrid;
using coney::graphics::ScreenRect;

namespace {
// Whether two rectangles share any pixel.
bool overlap(const ScreenRect& a, const ScreenRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
} // namespace

TEST_CASE("no textures give an empty layout", "[texture_grid]") {
    auto layout = layoutGrid({}, Extent{640, 480}, 8);
    CHECK(layout.quads.empty());
    CHECK(layout.columns == 0);
}

TEST_CASE("one texture fills the screen inside the margin, keeping its shape", "[texture_grid]") {
    const std::array textures{Extent{512, 256}};
    auto layout = layoutGrid(textures, Extent{1000, 1000}, 10);
    REQUIRE(layout.quads.size() == 1);
    // The cell is 980 x 980; a 2:1 texture is width-limited, 980 x 490, centred vertically.
    CHECK(layout.quads[0] == ScreenRect{10, 255, 980, 490});
}

TEST_CASE("four square textures on a square screen make a 2 x 2 grid", "[texture_grid]") {
    const std::vector<Extent> textures(4, Extent{64, 64});
    auto layout = layoutGrid(textures, Extent{400, 400}, 0);
    CHECK(layout.columns == 2);
    CHECK(layout.rows == 2);
    CHECK(layout.quads[0] == ScreenRect{0, 0, 200, 200});
    CHECK(layout.quads[1] == ScreenRect{200, 0, 200, 200});
    CHECK(layout.quads[2] == ScreenRect{0, 200, 200, 200});
    CHECK(layout.quads[3] == ScreenRect{200, 200, 200, 200});
}

TEST_CASE("a wide screen puts three textures in one row", "[texture_grid]") {
    const std::vector<Extent> textures(3, Extent{32, 32});
    auto layout = layoutGrid(textures, Extent{1280, 400}, 8);
    CHECK(layout.columns == 3);
    CHECK(layout.rows == 1);
}

TEST_CASE("grid cells stay inside the screen and keep each shape without overlapping", "[texture_grid]") {
    std::vector<Extent> textures;
    for (int i = 0; i < 23; ++i) {
        textures.push_back(Extent{4 << (i % 7), 4 << ((i * 3) % 7)});
    }
    const Extent screen{1280, 720};
    auto layout = layoutGrid(textures, screen, 8);
    REQUIRE(layout.quads.size() == textures.size());
    CHECK(layout.columns * layout.rows >= static_cast<int>(textures.size()));
    for (std::size_t i = 0; i < layout.quads.size(); ++i) {
        const ScreenRect& quad = layout.quads[i];
        CHECK(quad.x >= 0);
        CHECK(quad.y >= 0);
        CHECK(quad.x + quad.width <= screen.width);
        CHECK(quad.y + quad.height <= screen.height);
        CHECK(quad.width > 0);
        CHECK(quad.height > 0);
        // The shape is kept up to rounding one side down: w * th - h * tw lies strictly between -th and tw.
        const long long cross = static_cast<long long>(quad.width) * textures[i].height -
                                static_cast<long long>(quad.height) * textures[i].width;
        CHECK(cross < textures[i].width);
        CHECK(-cross < textures[i].height);
        for (std::size_t j = 0; j < i; ++j) {
            CHECK_FALSE(overlap(quad, layout.quads[j]));
        }
    }
}

TEST_CASE("a screen too small for the margins gives empty rectangles", "[texture_grid]") {
    const std::vector<Extent> textures(2, Extent{16, 16});
    auto layout = layoutGrid(textures, Extent{10, 10}, 8);
    REQUIRE(layout.quads.size() == 2);
    for (const ScreenRect& quad : layout.quads) {
        CHECK(quad.width == 0);
        CHECK(quad.height == 0);
    }
}
