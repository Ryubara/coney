// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/screen.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::graphics::DisplayAspect;
using coney::graphics::Extent;
using coney::graphics::fitLogicalScreen;
using coney::graphics::isWideView;
using coney::graphics::LogicalRect;
using coney::graphics::logicalToWindow;
using coney::graphics::originalLineShare;
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

TEST_CASE("a view wider than 4:3 is the 16:9 mode", "[screen]") {
    CHECK_FALSE(isWideView(4.0F / 3.0F));
    CHECK_FALSE(isWideView(961.0F / 720.0F)); // a 4:3 window a pixel off
    CHECK_FALSE(isWideView(1.0F));
    CHECK(isWideView(16.0F / 9.0F));
    CHECK(isWideView(16.0F / 10.0F));
}

TEST_CASE("one original line is 1/448 of a view as tall as the logical screen", "[screen]") {
    // 4:3 and 16:9 windows: the logical screen is as tall as the view, so one line is 1/448 of it.
    CHECK(originalLineShare(ScreenRect{0, 0, 960, 720}, ScreenRect{0, 0, 960, 720}) == Approx(1.0 / 448.0));
    CHECK(originalLineShare(ScreenRect{160, 0, 960, 720}, ScreenRect{0, 0, 1280, 720}) == Approx(1.0 / 448.0));
    // A tall window letterboxes the logical screen: a line is 600/448 pixels of the view's 800.
    CHECK(originalLineShare(ScreenRect{0, 100, 800, 600}, ScreenRect{0, 0, 800, 800}) == Approx(600.0 / 448.0 / 800.0));
    CHECK(originalLineShare(ScreenRect{}, ScreenRect{}) == 0.0F);
}

TEST_CASE("logical pixels map onto the logical screen's place in the window", "[screen]") {
    const ScreenRect viewport{160, 0, 960, 672}; // 1.5 times 640 x 448, 160 pixels from the left
    CHECK(logicalToWindow(LogicalRect{0, 0, 640, 448}, viewport) == LogicalRect{160, 0, 960, 672});
    CHECK(logicalToWindow(LogicalRect{320, 224, 64, 32}, viewport) == LogicalRect{640, 336, 96, 48});
}
