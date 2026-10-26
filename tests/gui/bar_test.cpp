// SPDX-License-Identifier: GPL-3.0-or-later
// Tests of the meter bar's fill (gui::Bar::setFill, the original's HudGenericBar_SetFill): kept in 0-1, and a value
// that is not a number reads as empty.

#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/bar.h"

using Catch::Approx;

TEST_CASE("bar: the fill is clamped to 0-1, and a fill that is not a number is empty", "[widgets]") {
    // Expected: hud-functions-panels.md#fn-generic-bar, confirmed (code)
    coney::gui::Bar bar(nullptr, 0);
    bar.setFill(-0.5F);
    CHECK(bar.fill() == 0.0F);
    bar.setFill(1.0F);
    CHECK(bar.fill() == 1.0F);
    bar.setFill(0.25F);
    CHECK(bar.fill() == Approx(0.25F));
    bar.setFill(std::numeric_limits<float>::quiet_NaN());
    CHECK(bar.fill() == 0.0F);
}
