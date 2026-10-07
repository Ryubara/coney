// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/mash_meter.h"

#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"

using coney::hud::MashMeter;

TEST_CASE("the uncuffing's mash meter alternates R1 and L1 every 400 ms and fills from 0 to 1", "[hud]") {
    MashMeter meter;
    CHECK_FALSE(meter.shown());
    CHECK(meter.glyph(0) == 0);

    meter.show(1, MashMeter::kUncuffWord);
    CHECK(meter.shown());
    CHECK(meter.fill() == 0.0F);
    // R1 (0x9c) in the first half of every 800 ms, L1 (0xa0) in the second.
    CHECK(meter.glyph(0) == static_cast<char>(0x9c));
    CHECK(meter.glyph(399) == static_cast<char>(0x9c));
    CHECK(meter.glyph(400) == static_cast<char>(0xa0));
    CHECK(meter.glyph(799) == static_cast<char>(0xa0));
    CHECK(meter.glyph(800) == static_cast<char>(0x9c));

    meter.setFill(0.5F);
    CHECK(meter.fill() == 0.5F);
    meter.setFill(1.7F);
    CHECK(meter.fill() == 1.0F);
    meter.setFill(-0.01F);
    CHECK(meter.fill() == 0.0F);

    // Any other word blinks the triangle in the second half only.
    meter.show(1, 0x172);
    CHECK(meter.glyph(100) == 0);
    CHECK(meter.glyph(500) == static_cast<char>(0x96));

    meter.hide();
    CHECK_FALSE(meter.shown());
    CHECK(meter.glyph(500) == 0);
}

TEST_CASE("a mash meter on screen hides the scroll-in messages, the hint box and the prompts", "[hud]") {
    coney::hud::Hud hud;
    CHECK_FALSE(hud.scrollInHidden());
    hud.mashMeter(0).show(1, MashMeter::kUncuffWord);
    CHECK(hud.scrollInHidden());
    CHECK(hud.hintsHidden());
    hud.mashMeter(0).hide();
    CHECK_FALSE(hud.scrollInHidden());
}
