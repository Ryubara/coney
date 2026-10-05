// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/stamina.h"

#include <catch2/catch_test_macros.hpp>

using coney::human::Gait;
using coney::human::RefillBlocks;
using coney::human::Stamina;

namespace {

constexpr float kStep = 1.0F / 30.0F;

} // namespace

TEST_CASE("stamina drains 20 a second at the sprint gait, one point every 1.5 updates", "[human][stamina]") {
    Stamina stamina(135);
    CHECK(stamina.value() == 135);
    stamina.drain(Gait::Sprint, false, kStep);
    CHECK(stamina.value() == 135); // two thirds of a point carried
    stamina.drain(Gait::Sprint, false, kStep);
    CHECK(stamina.value() == 134);
    stamina.drain(Gait::Sprint, false, kStep);
    CHECK(stamina.value() == 133);
    // A second of sprinting is 20 points (within the rounding of the carried fraction).
    for (int i = 0; i < 27; ++i) {
        stamina.drain(Gait::Sprint, false, kStep);
    }
    CHECK(stamina.value() >= 114);
    CHECK(stamina.value() <= 116);
}

TEST_CASE("stamina does not drain below the sprint gait or while an action's clip plays", "[human][stamina]") {
    Stamina stamina(135);
    for (const Gait gait : {Gait::Standing, Gait::Walk, Gait::Jog, Gait::Run}) {
        CHECK_FALSE(stamina.drain(gait, false, 1.0F));
    }
    CHECK_FALSE(stamina.drain(Gait::Sprint, true, 1.0F));
    CHECK(stamina.value() == 135);
}

TEST_CASE("135 points of stamina last 6.75 s of sprint, and the drain reports the empty update once",
          "[human][stamina]") {
    Stamina stamina(135);
    int updates = 0;
    bool emptied = false;
    while (!emptied && updates < 1000) {
        emptied = stamina.drain(Gait::Sprint, false, kStep);
        ++updates;
    }
    CHECK(emptied);
    CHECK(stamina.value() == 0);
    CHECK(updates >= 201);
    CHECK(updates <= 204);
    CHECK_FALSE(stamina.drain(Gait::Sprint, false, kStep));
}

TEST_CASE("stamina refills 40 a second with no delay, not at the sprint gait, in the air or running with L2",
          "[human][stamina]") {
    Stamina stamina(135);
    while (!stamina.drain(Gait::Sprint, false, kStep)) {
    }
    // Blocked: sprint gait, airborne, run with L2 held.
    stamina.refill(RefillBlocks{.gait = Gait::Sprint, .airborne = false, .sprintHeld = false}, 1.0F);
    stamina.refill(RefillBlocks{.gait = Gait::Walk, .airborne = true, .sprintHeld = false}, 1.0F);
    stamina.refill(RefillBlocks{.gait = Gait::Run, .airborne = false, .sprintHeld = true}, 1.0F);
    CHECK(stamina.value() == 0);
    // Not blocked: a run with L2 let go, a walk with L2 held, standing. 4 points every 3 updates.
    stamina.refill(RefillBlocks{.gait = Gait::Run, .airborne = false, .sprintHeld = false}, kStep);
    stamina.refill(RefillBlocks{.gait = Gait::Walk, .airborne = false, .sprintHeld = true}, kStep);
    stamina.refill(RefillBlocks{.gait = Gait::Standing, .airborne = false, .sprintHeld = false}, kStep);
    CHECK(stamina.value() >= 3);
    CHECK(stamina.value() <= 4);
    // 3.4 s from empty to full, and never past it.
    for (int i = 0; i < 120; ++i) {
        stamina.refill(RefillBlocks{}, kStep);
    }
    CHECK(stamina.value() == 135);
}

TEST_CASE("the sprint is asked for only while L2 is held and stamina is not 0", "[human][stamina]") {
    CHECK(coney::human::sprintAsked(true, 135));
    CHECK(coney::human::sprintAsked(true, 1));
    CHECK_FALSE(coney::human::sprintAsked(true, 0));
    CHECK_FALSE(coney::human::sprintAsked(false, 135));
    CHECK_FALSE(coney::human::sprintAsked(true, 135, true));
}
