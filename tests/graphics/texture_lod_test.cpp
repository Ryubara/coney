// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/texture_lod.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using coney::graphics::TextureLod;
using coney::graphics::unpackTextureLod;

TEST_CASE("a raster's packed K is the GS's signed fixed point with four fraction bits", "[graphics][texture_lod]") {
    CHECK(unpackTextureLod(0xFC0).k == Catch::Approx(-4.0F)); // librw's default
    CHECK(unpackTextureLod(0xFB2).k == Catch::Approx(-4.875F));
    CHECK(unpackTextureLod(0x020).k == Catch::Approx(2.0F));
    CHECK(unpackTextureLod(0xFC0).l == 0);
    CHECK(unpackTextureLod(0x2FC0).l == 2);
    CHECK(unpackTextureLod(0x2FC0).k == Catch::Approx(-4.0F));
}

TEST_CASE("the level follows the distance: level 0 to 2^-K m, level 1 at twice that", "[graphics][texture_lod]") {
    const TextureLod lod{.k = -4.875F};
    // The page's example: level 0 up to 29 m, blended fully to level 1 by 59 m.
    CHECK(lod.at(29.34F) == Catch::Approx(0.0F).margin(0.01F));
    CHECK(lod.at(58.69F) == Catch::Approx(1.0F).margin(0.01F));
    CHECK(lod.at(3.8F) < 0.0F); // magnified up close
    CHECK(lod.at(0.0F) < -10.0F);
}

TEST_CASE("L scales the distance term", "[graphics][texture_lod]") {
    const TextureLod lod{.k = -4.0F, .l = 1};
    CHECK(lod.at(16.0F) == Catch::Approx(4.0F));
}
