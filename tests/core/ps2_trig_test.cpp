// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ps2_trig.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

namespace {

// A float's raw word.
std::uint32_t word(float value) { return std::bit_cast<std::uint32_t>(value); }

// The units in the last place between two floats of one sign.
std::int64_t units(float a, float b) { return static_cast<std::int64_t>(word(a)) - static_cast<std::int64_t>(word(b)); }

} // namespace

TEST_CASE("the game's sine and cosine give the original's words for a run's half headings", "[core][ps2_trig]") {
    // Expected: characters-locomotion.md#run-velocity, confirmed (runtime)
    // Half headings of chars_run_stop_hit's straight run (just over π/4, so through the reduction by π/2) with the
    // sine and cosine Math_Sinf and Math_Cosf returned for each, logged as raw words.
    struct Sample {
        std::uint32_t half;
        std::uint32_t sine;
        std::uint32_t cosine;
    };
    constexpr std::array<Sample, 4> kSamples{{
        {0x3f4b4daaU, 0x3f3698ebU, 0x3f336d6fU},
        {0x3f4b4bfbU, 0x3f3697bdU, 0x3f336ea3U},
        {0x3f4b4baaU, 0x3f369784U, 0x3f336edcU},
        {0x3f4b4b15U, 0x3f36971bU, 0x3f336f47U},
    }};
    for (const Sample& sample : kSamples) {
        const float half = std::bit_cast<float>(sample.half);
        CHECK(word(coney::ps2::sinf(half)) == sample.sine);
        CHECK(word(coney::ps2::cosf(half)) == sample.cosine);
    }
    // Elsewhere fdlibm's float kernels (maths.md#trig, confirmed (code)), direct below π/4 and reduced by π/2 up to
    // 3π/4, either sign, stay within a few units of the true values.
    CHECK(coney::ps2::sinf(0.0F) == 0.0F);
    CHECK(coney::ps2::cosf(0.0F) == 1.0F);
    for (int step = -46; step <= 46; ++step) {
        const float x = static_cast<float>(step) * 0.05F;
        const auto exactSine = static_cast<float>(std::sin(static_cast<double>(x)));
        const auto exactCosine = static_cast<float>(std::cos(static_cast<double>(x)));
        if (std::fabs(exactSine) > 0.01F) {
            CHECK(std::llabs(units(coney::ps2::sinf(x), exactSine)) <= 4);
        }
        if (std::fabs(exactCosine) > 0.01F) {
            CHECK(std::llabs(units(coney::ps2::cosf(x), exactCosine)) <= 4);
        }
    }
}
