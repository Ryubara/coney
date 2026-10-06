// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/reference_sprites.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace graphics = coney::graphics;

namespace {

// An RGBA image of `width` × `height` whose pixel (x, y) is (x, y, 7, 255): every pixel tells where it came from.
std::vector<std::uint8_t> coordinateImage(int width, int height) {
    std::vector<std::uint8_t> rgba;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            rgba.insert(rgba.end(), {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), 7, 255});
        }
    }
    return rgba;
}

// Pixel (x, y) of an RGBA image `width` pixels wide.
std::vector<std::uint8_t> pixel(const std::vector<std::uint8_t>& rgba, int width, int x, int y) {
    const auto at = static_cast<std::size_t>((y * width) + x) * 4;
    return {rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

} // namespace

TEST_CASE("a sheet rectangle covers the whole texels its inset corners fall in", "[reference_sprites]") {
    // Radar icon 28 of part_page0 (512 x 256): texels 33.25-45.75 across, 144.125-156.875 down, as the disc's
    // rectangles are inset by a quarter texel of the width.
    const graphics::UvRect icon{33.25F / 512.0F, 144.125F / 256.0F, 45.75F / 512.0F, 156.875F / 256.0F};
    CHECK(graphics::rectTexels(icon, 512, 256) == graphics::TexelBox{33, 144, 13, 13});
    // Corners on texel edges are kept as they are.
    CHECK(graphics::rectTexels({0.25F, 0.5F, 0.75F, 1.0F}, 64, 32) == graphics::TexelBox{16, 16, 32, 16});
    // Clamped to the texture, and never empty.
    CHECK(graphics::rectTexels({-0.5F, 0.0F, 2.0F, 1.0F}, 8, 8) == graphics::TexelBox{0, 0, 8, 8});
    CHECK(graphics::rectTexels({0.5F, 0.5F, 0.5F, 0.5F}, 8, 8) == graphics::TexelBox{4, 4, 1, 1});
    CHECK(graphics::rectTexels({1.0F, 1.0F, 1.0F, 1.0F}, 8, 8) == graphics::TexelBox{7, 7, 1, 1});
}

TEST_CASE("an icon keeps its size within 64 x 64 and a sprite is scaled to fit it", "[reference_sprites]") {
    // Radar icons: their own size, only shrunk past the limit.
    CHECK(graphics::fitWithin(13, 13, graphics::kReferenceIconLimit, false) == graphics::ImageSize{13, 13});
    CHECK(graphics::fitWithin(64, 20, graphics::kReferenceIconLimit, false) == graphics::ImageSize{64, 20});
    CHECK(graphics::fitWithin(128, 32, graphics::kReferenceIconLimit, false) == graphics::ImageSize{64, 16});
    // Particle sprites: the longer side becomes 64, the proportion kept and rounded to nearest.
    CHECK(graphics::fitWithin(16, 16, graphics::kReferenceIconLimit, true) == graphics::ImageSize{64, 64});
    CHECK(graphics::fitWithin(120, 120, graphics::kReferenceIconLimit, true) == graphics::ImageSize{64, 64});
    CHECK(graphics::fitWithin(102, 50, graphics::kReferenceIconLimit, true) == graphics::ImageSize{64, 31});
    CHECK(graphics::fitWithin(10, 30, graphics::kReferenceIconLimit, true) == graphics::ImageSize{21, 64});
    // A sliver keeps one pixel.
    CHECK(graphics::fitWithin(1000, 1, graphics::kReferenceIconLimit, false) == graphics::ImageSize{64, 1});
}

TEST_CASE("a crop takes the box's pixels, rows top down", "[reference_sprites]") {
    const auto image = coordinateImage(6, 5);
    const auto cut = graphics::cropRgba(image, 6, 5, {2, 1, 3, 2});
    REQUIRE(cut.size() == std::size_t{3} * 2 * 4);
    CHECK(pixel(cut, 3, 0, 0) == std::vector<std::uint8_t>{2, 1, 7, 255});
    CHECK(pixel(cut, 3, 2, 1) == std::vector<std::uint8_t>{4, 2, 7, 255});
}

TEST_CASE("resampling repeats pixels going up and averages areas going down", "[reference_sprites]") {
    // Up by four: each source pixel becomes a 4 x 4 block, unchanged.
    const auto image = coordinateImage(2, 2);
    const auto up = graphics::resampleRgba(image, 2, 2, 8, 8);
    CHECK(pixel(up, 8, 0, 0) == std::vector<std::uint8_t>{0, 0, 7, 255});
    CHECK(pixel(up, 8, 3, 3) == std::vector<std::uint8_t>{0, 0, 7, 255});
    CHECK(pixel(up, 8, 4, 3) == std::vector<std::uint8_t>{1, 0, 7, 255});
    CHECK(pixel(up, 8, 7, 7) == std::vector<std::uint8_t>{1, 1, 7, 255});

    // Down by two: the average of each 2 x 2 block (x 0-1 and 2-3 average to 0.5 and 2.5, rounded up).
    const auto down = graphics::resampleRgba(coordinateImage(4, 4), 4, 4, 2, 2);
    CHECK(pixel(down, 2, 0, 0) == std::vector<std::uint8_t>{1, 1, 7, 255});
    CHECK(pixel(down, 2, 1, 1) == std::vector<std::uint8_t>{3, 3, 7, 255});

    // A transparent neighbour gives no colour, only less alpha: one red opaque pixel and one clear black one.
    const std::vector<std::uint8_t> pair{200, 0, 0, 255, 0, 0, 0, 0};
    const auto merged = graphics::resampleRgba(pair, 2, 1, 1, 1);
    CHECK(merged == std::vector<std::uint8_t>{200, 0, 0, 128});

    // A non-whole factor weighs each source pixel by its overlap: 3 pixels into 2, the middle one split.
    const std::vector<std::uint8_t> three{0, 0, 0, 255, 90, 0, 0, 255, 180, 0, 0, 255};
    const auto two = graphics::resampleRgba(three, 3, 1, 2, 1);
    CHECK(pixel(two, 2, 0, 0) == std::vector<std::uint8_t>{30, 0, 0, 255});  // (0 * 2 + 90 * 1) / 3
    CHECK(pixel(two, 2, 1, 0) == std::vector<std::uint8_t>{150, 0, 0, 255}); // (90 * 1 + 180 * 2) / 3
}

TEST_CASE("the radar icons and particle sprites are the listed ones, named as the index says", "[reference_sprites]") {
    const auto icons = graphics::radarIconIds();
    CHECK(icons.size() == 21);
    CHECK(std::ranges::is_sorted(icons));
    CHECK(std::ranges::find(icons, 28U) != icons.end());
    CHECK(graphics::radarIconFileName(28) == "icon-28.png");

    const auto sprites = graphics::particleSprites();
    CHECK(sprites.size() == 58);
    std::set<std::string> names;
    for (const graphics::ReferenceSprite& sprite : sprites) {
        names.emplace(sprite.name);
        CHECK(!sprite.sheet.empty());
    }
    CHECK(names.size() == sprites.size()); // each type once
    const auto flash = std::ranges::find(sprites, std::string_view{"part_gun_flash"}, &graphics::ReferenceSprite::name);
    REQUIRE(flash != sprites.end());
    CHECK(flash->sheet == "lighting");
    CHECK(flash->rect == 2);
}
