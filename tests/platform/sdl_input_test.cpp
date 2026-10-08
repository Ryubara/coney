// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sdl_input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "combat/attacks.h"
#include "core/pad.h"

using coney::platform::pressureFromTrigger;
using coney::platform::stickByteFromAxis;
using coney::platform::stickBytesFromAxes;
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

namespace {

// The stick's length as the game sees it, from a pair of raw bytes (y negated, as the pad does).
float gameLength(const std::array<std::uint8_t, 2>& bytes) {
    return std::min(1.0F, std::hypot(pad::stickValue(bytes[0]), -pad::stickValue(bytes[1])));
}

} // namespace

TEST_CASE("a modern gamepad's round stick is squared off like a DualShock 2's", "[sdl_input]") {
    // At rest and straight pushes are as before.
    CHECK(stickBytesFromAxes(0, 0) == std::array<std::uint8_t, 2>{128, 128});
    CHECK(stickBytesFromAxes(32767, 0) == std::array<std::uint8_t, 2>{255, 128});
    CHECK(stickBytesFromAxes(0, -32768) == std::array<std::uint8_t, 2>{128, 0});
    // A full diagonal on a round gate (0.707 on each axis) reaches the corner, past the 0.95 a run needs, every way.
    for (const int sx : {-1, 1}) {
        for (const int sy : {-1, 1}) {
            const auto bytes =
                stickBytesFromAxes(static_cast<std::int16_t>(sx * 23170), static_cast<std::int16_t>(sy * 23170));
            CHECK(gameLength(bytes) > 0.95F);
        }
    }
    // Unsquared, the same diagonal falls short of a run: the walk players saw.
    CHECK(gameLength({stickByteFromAxis(23170), stickByteFromAxis(23170)}) < 0.95F);
    // A half push along a diagonal stays a walk, and a slight one stays in the dead zone.
    const float half = gameLength(stickBytesFromAxes(11585, -11585));
    CHECK(half > 0.12F);
    CHECK(half < 0.95F);
    CHECK(gameLength(stickBytesFromAxes(4000, 4000)) == 0.0F);
}

TEST_CASE("a stick pushed to its rim reads as a full push, past the 0.95 a snap and a run need", "[sdl_input]") {
    // 0.93 of the travel, straight to each side and back: what a real stick at its rim often reports.
    constexpr auto kRim = static_cast<std::int16_t>(0.93 * 32767);
    for (const auto& [x, y] : {std::pair<int, int>{kRim, 0}, {-kRim, 0}, {0, kRim}, {0, -kRim}}) {
        const auto bytes = stickBytesFromAxes(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y));
        CHECK(gameLength(bytes) > coney::combat::kSnapStick);
    }
    // Without the rim the same push falls short: the snaps that never came.
    CHECK(gameLength({stickByteFromAxis(kRim), stickByteFromAxis(0)}) < coney::combat::kSnapStick);
    // A push well short of the rim stays a walk.
    const float part = gameLength(stickBytesFromAxes(static_cast<std::int16_t>(0.6 * 32767), 0));
    CHECK(part > 0.12F);
    CHECK(part < coney::combat::kSnapStick);
}
