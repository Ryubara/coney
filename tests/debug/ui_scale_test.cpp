// SPDX-License-Identifier: GPL-3.0-or-later
// The developer overlay's size rule: the scale for a display height, and the override choices.
#include "debug/ui_scale.h"

#include <cstddef>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::debug::autoUiScale;
using coney::debug::uiScaleFor;
using coney::debug::uiScaleLabels;
using coney::debug::uiScaleText;

TEST_CASE("the automatic scale is 1 at 1080 and grows with the display's height", "[debug][ui-scale]") {
    CHECK(autoUiScale(1080.0F) == Approx(1.0F));
    CHECK(autoUiScale(2160.0F) == Approx(2.0F));
    CHECK(autoUiScale(1440.0F) == Approx(1.375F)); // 1.333 rounded to the nearest eighth
    CHECK(autoUiScale(4320.0F) == Approx(4.0F));
}

TEST_CASE("the automatic scale never goes below 1 or above 4", "[debug][ui-scale]") {
    CHECK(autoUiScale(720.0F) == Approx(1.0F));
    CHECK(autoUiScale(480.0F) == Approx(1.0F));
    CHECK(autoUiScale(8640.0F) == Approx(4.0F));
    // Nothing measured yet (a headless run, a window not sized): the designed size.
    CHECK(autoUiScale(0.0F) == Approx(1.0F));
    CHECK(autoUiScale(-5.0F) == Approx(1.0F));
}

TEST_CASE("the automatic scale moves in steps, not on every pixel", "[debug][ui-scale]") {
    // Two heights a few pixels apart give one scale, so dragging a window edge does not remake the font every pixel.
    CHECK(autoUiScale(1620.0F) == autoUiScale(1624.0F));
    // A larger change moves it.
    CHECK(autoUiScale(1620.0F) < autoUiScale(1900.0F));
}

TEST_CASE("the scale is the same physical size across resolutions", "[debug][ui-scale]") {
    // The scale times the 1080p reference is the display height (within a step): the overlay fills the same share of
    // the screen at any resolution.
    for (const float height : {1080.0F, 1200.0F, 1440.0F, 1600.0F, 2160.0F, 3000.0F}) {
        CHECK(autoUiScale(height) * 1080.0F == Approx(height).margin(1080.0F * 0.0625F));
    }
}

TEST_CASE("the choices are Auto then fixed scales, and a fixed one ignores the height", "[debug][ui-scale]") {
    const auto& labels = uiScaleLabels();
    REQUIRE(labels.size() == 8);
    CHECK(labels[0] == "Auto");
    CHECK(labels[1] == "1x");
    CHECK(labels[2] == "1.25x");
    CHECK(labels[3] == "1.5x");
    CHECK(labels[4] == "2x");
    CHECK(labels[7] == "4x");
    CHECK(uiScaleFor(0, 2160.0F) == Approx(2.0F)); // Auto follows the height
    CHECK(uiScaleFor(0, 1080.0F) == Approx(1.0F));
    CHECK(uiScaleFor(3, 480.0F) == Approx(1.5F)); // 1.5x stays 1.5x on a small window
    CHECK(uiScaleFor(4, 4320.0F) == Approx(2.0F));
    CHECK(uiScaleFor(7, 1080.0F) == Approx(4.0F));
    CHECK(uiScaleFor(99, 2160.0F) == Approx(2.0F)); // past the end: Auto
}

TEST_CASE("a scale prints without trailing zeros", "[debug][ui-scale]") {
    CHECK(uiScaleText(1.0F) == "1x");
    CHECK(uiScaleText(1.5F) == "1.5x");
    CHECK(uiScaleText(1.375F) == "1.375x");
}
