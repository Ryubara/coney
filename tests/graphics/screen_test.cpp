// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/screen.h"

#include <catch2/catch_test_macros.hpp>

using coney::graphics::DisplayAspect;
using coney::graphics::Extent;
using coney::graphics::fitLogicalScreen;
using coney::graphics::LogicalRect;
using coney::graphics::logicalToWindow;
using coney::graphics::ScreenRect;

TEST_CASE("the logical screen keeps its 4:3 shape inside any window", "[screen]") {
    // A window of the same shape is filled exactly.
    CHECK(fitLogicalScreen(Extent{960, 720}) == ScreenRect{0, 0, 960, 720});
    // A wide window gets black bars left and right (pillarbox), a tall one above and below (letterbox).
    CHECK(fitLogicalScreen(Extent{1280, 720}) == ScreenRect{160, 0, 960, 720});
    CHECK(fitLogicalScreen(Extent{800, 800}) == ScreenRect{0, 100, 800, 600});
    // 16:9 for the widescreen option.
    CHECK(fitLogicalScreen(Extent{1280, 720}, DisplayAspect{16, 9}) == ScreenRect{0, 0, 1280, 720});
    // Odd sizes round down and stay centred.
    CHECK(fitLogicalScreen(Extent{101, 75}) == ScreenRect{0, 0, 100, 75});
}

TEST_CASE("an empty window gives an empty logical screen", "[screen]") {
    CHECK(fitLogicalScreen(Extent{0, 720}) == ScreenRect{});
    CHECK(fitLogicalScreen(Extent{640, -1}) == ScreenRect{});
}

TEST_CASE("logical pixels map onto the logical screen's place in the window", "[screen]") {
    const ScreenRect viewport{160, 0, 960, 672}; // 1.5 times 640 x 448, 160 pixels from the left
    CHECK(logicalToWindow(LogicalRect{0, 0, 640, 448}, viewport) == LogicalRect{160, 0, 960, 672});
    CHECK(logicalToWindow(LogicalRect{320, 224, 64, 32}, viewport) == LogicalRect{640, 336, 96, 48});
}
