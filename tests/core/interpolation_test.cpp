// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/interpolation.h"

#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::Interpolated;
using coney::lerp;
using coney::lerpAngle;
using coney::lerpLooping;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
} // namespace

TEST_CASE("lerp gives each end exactly and the middle between", "[interpolation]") {
    // Values whose difference does not round-trip in float: from + (to - from) would miss `to` by an ulp.
    const float from = 0.1F;
    const float to = 123456.789F;
    CHECK(lerp(from, to, 0.0F) == from);
    CHECK(lerp(from, to, 1.0F) == to);
    CHECK(lerp(-2.0F, 6.0F, 0.25F) == 0.0F);
}

TEST_CASE("lerpAngle takes the short way round and keeps the ends exact", "[interpolation]") {
    // From just below +pi to just above -pi: the short way crosses pi, not 0.
    const float from = kPi - 0.1F;
    const float to = -kPi + 0.1F;
    const float middle = lerpAngle(from, to, 0.5F);
    CHECK(std::abs(std::remainder(middle - kPi, 2.0F * kPi)) == Approx(0.0F).margin(1e-5));
    CHECK(lerpAngle(from, to, 1.0F) == to);
    CHECK(lerpAngle(from, to, 0.0F) == from);
    CHECK(lerpAngle(0.0F, 1.0F, 0.5F) == Approx(0.5F));
}

TEST_CASE("lerpLooping runs on past the end when the playhead wrapped", "[interpolation]") {
    // A 1 s clip: from 0.9 to 0.1 wrapped, so halfway is 1.0, which is 0.0 of the next pass.
    CHECK(lerpLooping(0.9F, 0.1F, 0.5F, 1.0F) == Approx(0.0F).margin(1e-6));
    CHECK(lerpLooping(0.9F, 0.1F, 0.25F, 1.0F) == Approx(0.95F));
    CHECK(lerpLooping(0.9F, 0.1F, 0.75F, 1.0F) == Approx(0.05F));
    // No wrap: a plain blend; the ends are exact.
    CHECK(lerpLooping(0.2F, 0.4F, 0.5F, 1.0F) == Approx(0.3F));
    CHECK(lerpLooping(0.9F, 0.1F, 1.0F, 1.0F) == 0.1F);
    CHECK(lerpLooping(0.9F, 0.1F, 0.0F, 1.0F) == 0.9F);
    CHECK(lerpLooping(0.9F, 0.1F, 0.5F, 0.0F) == 0.1F);
}

TEST_CASE("Interpolated keeps the last two steps and resets without a blend", "[interpolation]") {
    Interpolated<float> value(1.0F);
    CHECK(value.previous() == 1.0F);
    value.commit();
    value.current() = 3.0F;
    CHECK(value.previous() == 1.0F);
    CHECK(value.current() == 3.0F);
    value.commit();
    CHECK(value.previous() == 3.0F);
    value.reset(7.0F);
    CHECK(value.previous() == 7.0F);
    CHECK(value.current() == 7.0F);
}
