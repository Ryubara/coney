// SPDX-License-Identifier: GPL-3.0-or-later
// The motion blur's blend (docs/research/graphics.md#looks): to a strength or a colour over a time, from wherever the
// last blend had got to, at once for no time.
#include "effects/motion_blur.h"

#include <catch2/catch_test_macros.hpp>

using coney::effects::MotionBlur;

TEST_CASE("the blur starts white and off, and blends its strength over the time given", "[motion_blur]") {
    MotionBlur blur;
    CHECK(blur.current() == MotionBlur::Colour{255, 255, 255, 0});
    blur.queueAlpha(150, 1.0F);
    CHECK(blur.blending());
    CHECK(blur.current().a == 0);
    blur.step(0.5F);
    CHECK(blur.current().a == 75);
    blur.step(0.75F);
    CHECK_FALSE(blur.blending());
    CHECK(blur.current() == MotionBlur::Colour{255, 255, 255, 150});
}

TEST_CASE("a new blend starts from where the last one got to; no time is at once", "[motion_blur]") {
    MotionBlur blur;
    blur.queueAlpha(200, 2.0F);
    blur.step(1.0F); // 100
    blur.queueColour({0, 50, 100, 0}, 1.0F);
    blur.step(0.5F);
    CHECK(blur.current() == MotionBlur::Colour{128, 153, 178, 50});
    blur.queueAlpha(30, 0.0F);
    CHECK(blur.current().a == 30);
    CHECK(blur.target().a == 30);
    blur.reset();
    CHECK(blur.current().a == 0);
}
