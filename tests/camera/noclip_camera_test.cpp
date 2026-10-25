// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's debug flight camera follows without lag but still accepts deliberate stick input.
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "camera/follow_camera.h"

TEST_CASE("noclip camera follows every axis immediately and permits explicit orbit", "[camera][noclip][debug]") {
    // Expected: unverified, #243 (Coney-only debug camera contract, synthetic target).
    coney::camera::FollowTarget target;
    target.feet = {10.0F, 20.0F, 30.0F};
    target.debugFlight = true;
    coney::camera::FollowCamera camera(target.feet, target.heading);
    camera.updateForPlayer(target, 128, 128, nullptr, 1.0F / 30.0F);
    const auto offset = coney::anim::subtract(camera.position(), target.feet);
    for (const float direction : {1.0F, -1.0F}) {
        for (int frame = 0; frame < 30; ++frame) {
            target.feet = coney::anim::add(target.feet, {direction * 0.2F, direction * 0.3F, direction * 0.8F});
            camera.updateForPlayer(target, 128, 128, nullptr, 1.0F / 30.0F);
            const auto now = coney::anim::subtract(camera.position(), target.feet);
            CHECK(now.x == Catch::Approx(offset.x).margin(0.0001F));
            CHECK(now.y == Catch::Approx(offset.y).margin(0.0001F));
            CHECK(now.z == Catch::Approx(offset.z).margin(0.0001F));
        }
    }
    const auto before = camera.position();
    // Flying sideways turns the human but must not feed that turn back into camera-relative steering.
    target.heading += 1.5F;
    camera.updateForPlayer(target, 128, 128, nullptr, 1.0F / 30.0F);
    CHECK(coney::anim::distance(camera.position(), before) < 0.0001F);
    camera.updateForPlayer(target, 255, 128, nullptr, 1.0F / 30.0F);
    CHECK(coney::anim::distance(camera.position(), before) > 0.01F);
    const auto turned = camera.position();
    camera.updateForPlayer(target, 128, 128, nullptr, 1.0F / 30.0F);
    CHECK(coney::anim::distance(camera.position(), turned) < 0.0001F);

    // Ordinary play restores its normal lag when debug flight ends.
    target.debugFlight = false;
    target.feet.z += 10.0F;
    camera.updateForPlayer(target, 128, 128, nullptr, 1.0F / 30.0F);
    CHECK(std::abs((camera.position().z - target.feet.z) - offset.z) > 0.1F);
}
