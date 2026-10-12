// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/body.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

// The walls themselves (the 0.25 m rules, the sweep, the corner slide, the airborne push-out) are the physics world's:
// tests/physics/mesh_sweep_test.cpp and body_sweep_test.cpp.

TEST_CASE("the walking sphere is 0.35 m times the scale, its bottom 0.05 m above the feet", "[human][body]") {
    CHECK(coney::human::walkingRadius(1.0F) == Approx(0.35F));
    CHECK(coney::human::walkingRadius(0.97F) == Approx(0.3395F));
    CHECK(coney::human::walkingCentreHeight(0.97F) == Approx(0.3895F));
}

TEST_CASE("a player's walking sphere counts the scale twice, an AI's body factor is 1", "[human][body]") {
    CHECK(coney::human::playerWalkingRadius(0.97F) == Approx(0.4704F).margin(0.0002));
    CHECK(coney::human::aiWalkingRadius(1.01F) == Approx(0.357F).margin(0.0002));
}
