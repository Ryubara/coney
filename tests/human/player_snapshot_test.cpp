// SPDX-License-Identifier: GPL-3.0-or-later
// What drawing reads from a step (repo:src/human/player.h): the camera's up travels with its eye and target, so a
// scripted camera's roll (docs/research/camera.md#scripted-angles) reaches the renderer, and a cut is never swept.
#include "human/player.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::PlayerSnapshot;

TEST_CASE("a snapshot's camera up is world up unless set, and is mixed between two steps", "[player]") {
    const PlayerSnapshot upright;
    CHECK(upright.cameraUp.x == 0.0F);
    CHECK(upright.cameraUp.y == 0.0F);
    CHECK(upright.cameraUp.z == 1.0F);
    PlayerSnapshot rolled;
    rolled.cameraUp = Vec3{1.0F, 0.0F, 0.0F};
    const PlayerSnapshot half = coney::human::interpolate(upright, rolled, 0.5F);
    CHECK(half.cameraUp.x == Approx(0.5F));
    CHECK(half.cameraUp.z == Approx(0.5F));
    // At the newest step it is the newest camera's up exactly.
    CHECK(coney::human::interpolate(upright, rolled, 1.0F).cameraUp.x == 1.0F);
}

TEST_CASE("between two steps across a camera cut the view is the new camera's, not a sweep", "[player]") {
    PlayerSnapshot before;
    before.feet = Vec3{0.0F, 0.0F, 0.0F};
    before.cameraEye = Vec3{0.0F, -5.0F, 2.0F};
    before.cameraTarget = Vec3{0.0F, 0.0F, 1.4F};
    PlayerSnapshot after = before;
    after.feet = Vec3{1.0F, 0.0F, 0.0F};
    after.cameraEye = Vec3{40.0F, 20.0F, 3.0F};
    after.cameraTarget = Vec3{38.0F, 18.0F, 2.0F};
    // Without a cut the camera moves with the step.
    const PlayerSnapshot smooth = coney::human::interpolate(before, after, 0.5F);
    CHECK(smooth.cameraEye.x == Approx(20.0F));
    // With one it is already at the new camera, while the human still moves half way.
    after.cameraCuts = before.cameraCuts + 1;
    const PlayerSnapshot cut = coney::human::interpolate(before, after, 0.5F);
    CHECK(cut.cameraEye.x == Approx(40.0F));
    CHECK(cut.cameraTarget.y == Approx(18.0F));
    CHECK(cut.feet.x == Approx(0.5F));
    CHECK(cut.cameraCuts == after.cameraCuts);
}
