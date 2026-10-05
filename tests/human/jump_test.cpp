// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/jump.h"

#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::human::Gait;
using coney::human::Speeds;

namespace {

// Round speeds: walk 1.5, jog 4, run 7.5, sprint 10.
Speeds testSpeeds() {
    return Speeds{.base = 3.0F, .sneak = 1.2F, .walk = 1.5F, .jog = 4.0F, .run = 7.5F, .sprint = 10.0F};
}

} // namespace

TEST_CASE("a jump needs more than 3.3 m/s, a speed whose stored gait is a jog or more, and no climbable wall ahead",
          "[human][jump]") {
    const Speeds speeds = testSpeeds();
    CHECK_FALSE(coney::human::jumpAllowed(1.5F, speeds, false)); // a walk
    CHECK_FALSE(coney::human::jumpAllowed(3.3F, speeds, false));
    // Just over 3.3 m/s, nearer the jog than the walk: the run start's 3.35 m/s jumped at runtime.
    CHECK(coney::human::jumpAllowed(3.35F, speeds, false));
    CHECK(coney::human::jumpAllowed(4.0F, speeds, false));
    // A slow jog clip: 4 m/s is still nearer the walk, so its stored gait is a walk.
    Speeds slowJog = speeds;
    slowJog.jog = 8.0F;
    CHECK_FALSE(coney::human::jumpAllowed(4.0F, slowJog, false));
    CHECK(coney::human::jumpAllowed(7.5F, speeds, false));
    CHECK(coney::human::jumpAllowed(10.0F, speeds, false));
    CHECK_FALSE(coney::human::jumpAllowed(7.5F, speeds, true));
}

TEST_CASE("a jump leaves at the run speed from a jog or a run and at the sprint speed from a sprint", "[human][jump]") {
    const Speeds speeds = testSpeeds();
    CHECK(coney::human::launchSpeed(Gait::Jog, speeds) == 7.5F);
    CHECK(coney::human::launchSpeed(Gait::Run, speeds) == 7.5F);
    CHECK(coney::human::launchSpeed(Gait::Sprint, speeds) == 10.0F);
    CHECK(coney::human::launchSpeed(Gait::Walk, speeds) == 4.0F);
    CHECK(coney::human::jumpTuning().upSpeed == 5.5F);
    CHECK(coney::human::airTurnLimit() == Approx(4.0F * std::numbers::pi_v<float> / 180.0F));
}
