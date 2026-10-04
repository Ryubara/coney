// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sdl_input.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using coney::platform::pressureFromTrigger;
using coney::platform::stickByteFromAxis;
namespace pad = coney::pad;

// These cover the conversions only: opening real gamepads needs devices, which tests never assume.

TEST_CASE("SDL stick axes map onto the PS2's raw stick bytes, at rest inside the dead zone", "[sdl_input]") {
    CHECK(stickByteFromAxis(-32768) == 0);
    CHECK(stickByteFromAxis(0) == 128);
    CHECK(stickByteFromAxis(32767) == 255);
    CHECK(pad::stickValue(stickByteFromAxis(0)) == 0.0F);
    CHECK(pad::stickValue(stickByteFromAxis(-32768)) == -1.0F);
    CHECK(pad::stickValue(stickByteFromAxis(32767)) == 1.0F);
    // A small drift stays in the game's dead zone (95..160).
    CHECK(pad::stickValue(stickByteFromAxis(-8000)) == 0.0F);
    CHECK(pad::stickValue(stickByteFromAxis(8000)) == 0.0F);
}

TEST_CASE("SDL trigger axes become pressure bytes, negative values counting as released", "[sdl_input]") {
    CHECK(pressureFromTrigger(0) == 0);
    CHECK(pressureFromTrigger(-5) == 0);
    CHECK(pressureFromTrigger(32767) == 255);
    CHECK(pressureFromTrigger(16384) == 127);
    CHECK(pressureFromTrigger(8224) >= coney::platform::kTriggerHeldPressure);
}
