// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ps2_float.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

// Single precision rounded toward zero, as the PS2's floating-point unit rounds (docs/research/characters.md#run-stop,
// docs/research/combat.md#damage-table).

TEST_CASE("PS2 arithmetic rounds every result toward zero", "[core][ps2_float]") {
    // 1 + 3 × 2^-25 lies three quarters of the way from 1 to the next float: to nearest it rounds up, toward zero down.
    const double between = 1.0 + (3.0 / 33554432.0);
    CHECK(static_cast<float>(between) == std::nextafter(1.0F, 2.0F));
    CHECK(coney::ps2::towardZero(between) == 1.0F);
    CHECK(coney::ps2::towardZero(-between) == -1.0F);
    // Exact results are kept.
    CHECK(coney::ps2::mul(1.5F, 4.0F) == 6.0F);
    CHECK(coney::ps2::add(0.25F, 0.5F) == 0.75F);
    CHECK(coney::ps2::sqrt(9.0F) == 3.0F);
    // The class damage case: 30 × 115 × 0.01 + 0.5 is 34.999... toward zero, so 34.
    const float percent = coney::ps2::mul(coney::ps2::mul(30.0F, 115.0F), 0.01F);
    CHECK(static_cast<int>(coney::ps2::add(percent, 0.5F)) == 34);
}

TEST_CASE("a velocity's PS2 length never exceeds the exact length", "[core][ps2_float]") {
    // Rembrandt's run speed along headings round the circle: the length rounded toward zero is at most the exact
    // length, and a steady run's measured speed comes out at most a few units in the last place from the run speed.
    const float run = std::bit_cast<float>(0x40f9a3adU);
    int atOrAbove = 0;
    for (int degree = 0; degree < 360; ++degree) {
        const float heading = static_cast<float>(degree) * std::numbers::pi_v<float> / 180.0F;
        const float x = -std::sin(heading) * run;
        const float y = std::cos(heading) * run;
        const float measured = coney::ps2::length(x, y);
        CHECK(static_cast<double>(measured) <= std::sqrt((static_cast<double>(x) * x) + (static_cast<double>(y) * y)));
        const auto units = static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(measured)) -
                           static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(run));
        CHECK(units >= -4);
        CHECK(units <= 2);
        atOrAbove += measured >= run ? 1 : 0;
    }
    // Mostly below, as at runtime (10 of 93 steady-run updates at or above).
    CHECK(atOrAbove < 90);
}
