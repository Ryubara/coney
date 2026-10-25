// SPDX-License-Identifier: GPL-3.0-or-later
// The noclip is Coney's own debug tool: its idle pose must not keep animating during flight.
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "human/human.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

TEST_CASE("noclip holds the idle clip still and resumes animation after flight", "[human][noclip][debug]") {
    // Expected: unverified, #243 (Coney-only debug idle-pose contract, synthetic character)
    const auto data = coney::test::locomotionData();
    const coney::characters::AnimSet anims(data, nullptr);
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    coney::human::Human human(anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), {40.0F, 40.0F, 0.0F}, 0.0F);
    human.setFlight(coney::human::Flight{.speed = 6.0F});
    REQUIRE(human.animator().tasks().top() != nullptr);
    const auto idle = human.animator().animId();
    const float time = human.animator().tasks().top()->time();
    const coney::human::HumanInput input{.stickY = 1.0F, .cameraForward = {0.0F, 1.0F, 0.0F}, .targets = {}};
    for (int frame = 0; frame < 30; ++frame) {
        human.step(input, mesh.get());
        REQUIRE(human.animator().tasks().top() != nullptr);
        CHECK(human.animator().animId() == idle);
        CHECK(human.animator().state() == coney::human::AnimState::Idle);
        CHECK(human.animator().tasks().top()->time() == time);
    }
    human.setFlight(std::nullopt);
    const coney::human::HumanInput rest{.cameraForward = {0.0F, 1.0F, 0.0F}, .targets = {}};
    human.step(rest, mesh.get());
    REQUIRE(human.animator().tasks().top() != nullptr);
    CHECK(human.animator().tasks().top()->time() > time);
}
