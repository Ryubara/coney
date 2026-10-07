// SPDX-License-Identifier: GPL-3.0-or-later
// The screen tint (docs/research/rendering.md#tint): the script's floats stored x 255, truncated; the GS alpha the
// byte is drawn with; the store's 0.25 s blends from wherever the tint has got to.
#include "effects/screen_tint.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using coney::effects::ScreenTint;
using Colour = ScreenTint::Colour;

TEST_CASE("a component is stored x 255, truncated, and drawn with alpha x 128 / 255", "[screen_tint]") {
    // level2's overlay (0.15, 0.18, 0.2, 0.22) is stored (38, 45, 51, 56) and drawn with As 28.
    CHECK(ScreenTint::byteOf(0.15) == 38);
    CHECK(ScreenTint::byteOf(0.18) == 45);
    CHECK(ScreenTint::byteOf(0.2) == 51);
    CHECK(ScreenTint::byteOf(0.22) == 56);
    CHECK(ScreenTint::gsAlphaOf(56) == 28);
    CHECK(ScreenTint::gsAlphaOf(30) == 15);   // level87
    CHECK(ScreenTint::gsAlphaOf(51) == 25);   // level80
    CHECK(ScreenTint::gsAlphaOf(255) == 128); // full: the driver's conversion
    CHECK(ScreenTint::byteOf(1.0) == 255);
    CHECK(ScreenTint::byteOf(-0.5) == 0);
    CHECK_THAT(ScreenTint::opacityOf(30), WithinAbs(15.0F / 128.0F, 1e-6F));
}

TEST_CASE("the tint starts clear; the level's colour is at once and a store's blends both ways", "[screen_tint]") {
    ScreenTint tint;
    CHECK_FALSE(tint.drawn());
    CHECK(tint.look() == ScreenTint::kLevelLook);
    tint.setLevelColour(Colour{7, 20, 30, 51});
    CHECK(tint.current() == Colour{7, 20, 30, 51});
    tint.enterStore(Colour{107, 120, 130, 151});
    CHECK(tint.current() == Colour{7, 20, 30, 51});
    tint.step(0.125F);
    CHECK(tint.current() == Colour{57, 70, 80, 101});
    // Leaving half-way blends back from where it got to.
    tint.exitStore();
    CHECK(tint.look() == ScreenTint::kLevelLook);
    tint.step(0.125F);
    CHECK(tint.current() == Colour{32, 45, 55, 76});
    tint.step(1.0F);
    CHECK(tint.current() == Colour{7, 20, 30, 51});
    tint.reset();
    CHECK_FALSE(tint.drawn());
}

TEST_CASE("the game-over tint blends from the level's, can be finished early and put back at once", "[screen_tint]") {
    ScreenTint tint;
    tint.setLevelColour(Colour{7, 20, 30, 51});
    CHECK(ScreenTint::gsAlphaOf(ScreenTint::kGameOver.a) == 104);
    const Colour saved = tint.target();
    tint.blendTintTo(ScreenTint::kGameOver, 6.5F);
    CHECK_FALSE(tint.blendDone());
    tint.step(3.25F);
    CHECK(tint.current() == Colour{13, 10, 15, 129}); // each byte truncated
    tint.finishBlend();
    CHECK(tint.blendDone());
    CHECK(tint.current() == ScreenTint::kGameOver);
    // A retry: the saved tint again, at once.
    tint.blendTintTo(saved, 0.0F);
    CHECK(tint.blendDone());
    CHECK(tint.current() == Colour{7, 20, 30, 51});
}
