// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ps2_float.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

// Single precision rounded toward zero, as the PS2's floating-point unit rounds
// (docs/research/characters-locomotion.md#run-stop, docs/research/combat/index.md#damage-table).

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

TEST_CASE("PS2 addition keeps one guard bit of the smaller operand before it rounds", "[core][ps2_float]") {
    // Expected: characters-locomotion.md#run-velocity, confirmed (runtime)
    // The FPU lines the smaller operand up with the larger one and keeps a single bit below the larger one's last
    // place, so 1 - 1.5 × 2^-25 loses the whole subtrahend (0.75 of a unit below 1, under the guard bit) and stays 1,
    // where the exact difference rounded toward zero would be the float just below 1.
    const float small = std::bit_cast<float>(0x33400000U); // 1.5 × 2^-25
    CHECK(coney::ps2::sub(1.0F, small) == 1.0F);
    CHECK(coney::ps2::add(-1.0F, small) == -1.0F);
    // 1 - 2^-24 is the guard bit itself and is kept: the float just below 1.
    const float guard = std::bit_cast<float>(0x33800000U); // 2^-24
    CHECK(coney::ps2::sub(1.0F, guard) == std::nextafter(1.0F, 0.0F));
    // Like signs add as before: the bits lost below the guard could not have reached the result's last place.
    CHECK(coney::ps2::add(1.0F, small) == 1.0F);
    CHECK(coney::ps2::add(0.25F, 0.5F) == 0.75F);
}

TEST_CASE("a velocity's PS2 length takes its third part", "[core][ps2_float]") {
    // Expected: characters-locomotion.md#run-velocity, confirmed (code)
    // The skid test's length is sqrt((x × x + y × y) + z × z) in VU0 (0x00240f38-0x00240f50).
    CHECK(coney::ps2::length(0.0F, 3.0F, 4.0F) == 5.0F);
    CHECK(coney::ps2::length(2.0F, 3.0F, 6.0F) == 7.0F);
    CHECK(coney::ps2::length(3.0F, 4.0F, 0.0F) == 5.0F);
}
