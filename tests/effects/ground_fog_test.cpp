// SPDX-License-Identifier: GPL-3.0-or-later
// The ground fog and the camera's litter (docs/references/bindings/effects.md#start3dfog, #startgarbage): the top-ups
// to 20 wisps, 10 at a time every 5 frames, round the camera's target; the fade-in; the far wisps dropped and the near
// ones hidden; MaxFogParticles; and the litter's kinds.
#include "effects/ground_fog.h"

#include <cmath>

#include <catch2/catch_test_macros.hpp>

using coney::anim::Vec3;
using coney::effects::CameraLitter;
using coney::effects::EffectsViewer;
using coney::effects::FogSettings;
using coney::effects::GroundFog;

namespace {

constexpr float kFrame = 1.0F / 60.0F;

// Wisps fading in to alpha 90 over 9 steps, adding 10 a step.
FogSettings settings() {
    return FogSettings{
        .sprite = 34734080U, .colour = {200, 200, 255, 90}, .drift = 0.4F, .fadeSpeed = 1.0F, .fadeRate = 1.0F};
}

} // namespace

TEST_CASE("the fog tops its view up to 20 wisps, 10 every 5 frames, round the camera's target", "[fog]") {
    GroundFog fog;
    // The camera at its target, so no wisp placed within 20 m of the target is more than 20 m from it.
    const EffectsViewer viewer{.position = Vec3{0, 0, 1}, .target = Vec3{0, 0, 1}};
    fog.step(kFrame, viewer); // no fog running: nothing
    CHECK(fog.wisps().empty());
    fog.start(settings());
    fog.step(kFrame, viewer);
    CHECK(fog.wisps().size() == 10);
    for (int i = 0; i < 4; ++i) {
        fog.step(kFrame, viewer);
    }
    CHECK(fog.wisps().size() == 10); // the next top-up is 5 frames on
    fog.step(kFrame, viewer);
    CHECK(fog.wisps().size() == 20);
    for (int i = 0; i < 20; ++i) {
        fog.step(kFrame, viewer);
    }
    CHECK(fog.wisps().size() == 20);
    for (const GroundFog::Wisp& wisp : fog.wisps()) {
        const float dx = wisp.position.x - viewer.target.x;
        const float dy = wisp.position.y - viewer.target.y;
        CHECK(std::sqrt((dx * dx) + (dy * dy)) <= GroundFog::kRadius + 1.0F);
        CHECK(wisp.position.z >= viewer.target.z + GroundFog::kMinHeight);
        CHECK(wisp.position.z <= viewer.target.z + GroundFog::kMaxHeight);
    }
}

TEST_CASE("a wisp fades in to the colour's alpha over 9 / fadeSpeed steps", "[fog]") {
    GroundFog fog;
    const EffectsViewer viewer{.position = Vec3{0, 0, 1}, .target = Vec3{0, 0, 0}};
    fog.start(settings());
    fog.step(kFrame, viewer);
    REQUIRE_FALSE(fog.wisps().empty());
    CHECK(std::abs(fog.wisps().front().alpha - 10.0F) < 0.01F);
    for (int i = 0; i < 20; ++i) {
        fog.step(kFrame, viewer);
    }
    CHECK(fog.wisps().front().alpha == 90.0F);
}

TEST_CASE("wisps far from the camera are dropped and near ones hidden; MaxFogParticles lowers the count", "[fog]") {
    GroundFog fog;
    fog.setMaxWisps(5); // no fog running: nothing changes
    CHECK(fog.maxWisps() == GroundFog::kDefaultMaxWisps);
    fog.start(settings());
    fog.setMaxWisps(5);
    const EffectsViewer viewer{.position = Vec3{0, 0, 1}, .target = Vec3{0, 0, 0}};
    fog.step(kFrame, viewer);
    CHECK(fog.wisps().size() == 5);
    for (const GroundFog::Wisp& wisp : fog.wisps()) {
        const float dx = wisp.position.x;
        const float dy = wisp.position.y;
        const float dz = wisp.position.z - 1.0F;
        CHECK(wisp.hidden == ((dx * dx) + (dy * dy) + (dz * dz) < GroundFog::kHideWithin * GroundFog::kHideWithin));
    }
    // The camera moves 100 m away: every old wisp is dropped; new ones come round the new target.
    const EffectsViewer away{.position = Vec3{100, 0, 1}, .target = Vec3{100, 0, 0}};
    for (int i = 0; i < 5; ++i) {
        fog.step(kFrame, away);
    }
    CHECK_FALSE(fog.wisps().empty());
    for (const GroundFog::Wisp& wisp : fog.wisps()) {
        CHECK(wisp.position.x > 70.0F);
    }
    // A new start forgets the old wisps and the lowered count.
    fog.start(settings());
    CHECK(fog.wisps().empty());
    CHECK(fog.maxWisps() == GroundFog::kDefaultMaxWisps);
    fog.stop();
    CHECK_FALSE(fog.settings().has_value());
}

TEST_CASE("StartGarbage takes kinds 0-3 and EndGarbage stops it", "[fog]") {
    CameraLitter litter;
    litter.start(4);
    CHECK_FALSE(litter.kind().has_value());
    litter.start(2);
    CHECK(litter.kind() == 2U);
    litter.end();
    CHECK_FALSE(litter.kind().has_value());
}
