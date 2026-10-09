// SPDX-License-Identifier: GPL-3.0-or-later
// The ground fog and the camera's litter (docs/references/bindings/effects.md#start3dfog, #startgarbage): the top-ups
// to 20 wisps, 10 at a time every 5 frames, round the camera's target; the fade-in; the far wisps dropped and the near
// ones hidden; MaxFogParticles; the drift toward the camera; and the litter (docs/research/particles.md#garbage): its
// kinds, the grid, the fall and landing, the fade and respawn round the camera, and a piece with no ground below.
#include "effects/ground_fog.h"

#include <cmath>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "effects/camera_litter.h"

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
    const EffectsViewer viewer{.position = Vec3{0, 0, 1}, .target = Vec3{0, 0, 1}, .window = std::nullopt};
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

TEST_CASE("a wisp fades in by alpha / steps every 2 ticks and grows from 0 over its first update", "[fog]") {
    GroundFog fog;
    // The camera looks down from 30 m over the target with no view window: every wisp is in view and none near.
    const EffectsViewer viewer{.position = Vec3{0, 0, 10}, .target = Vec3{0, 0, 0}, .window = std::nullopt};
    fog.start(settings());
    CHECK(fog.alphaStep() == 10);
    CHECK(fog.fadeUpdates() == 9);
    CHECK(fog.updateTicks() == 2);
    fog.step(kFrame, viewer);
    REQUIRE_FALSE(fog.wisps().empty());
    const GroundFog::Wisp& born = fog.wisps().front();
    CHECK(born.alpha == 0);
    CHECK(born.previousSize == 0.0F);
    CHECK(born.size >= GroundFog::kMinSize);
    CHECK(born.size <= GroundFog::kMaxSize);
    fog.step(kFrame, viewer); // its first update, 2 ticks after birth
    CHECK(fog.wisps().front().alpha == 10);
    CHECK(fog.wisps().front().previousSize == fog.wisps().front().size);
    for (int i = 0; i < 40; ++i) {
        fog.step(kFrame, viewer);
    }
    CHECK(fog.wisps().front().alpha == 90);
    // A step that comes out 0 is 1, every 30 ticks.
    FogSettings faint = settings();
    faint.colour[3] = 5;
    fog.start(faint);
    CHECK(fog.alphaStep() == 1);
    CHECK(fog.updateTicks() == 30);
}

TEST_CASE("a hidden wisp ends from its third update; a frame draws the first 10 wisps that show", "[fog]") {
    GroundFog fog;
    const EffectsViewer viewer{.position = Vec3{0, 0, 10}, .target = Vec3{0, 0, 0}, .window = std::nullopt};
    fog.start(settings());
    for (int i = 0; i < 12; ++i) {
        fog.step(kFrame, viewer);
    }
    REQUIRE(fog.wisps().size() == 20);
    const std::vector<GroundFog::Drawn> drawn = fog.drawn();
    CHECK(drawn.size() <= GroundFog::kDrawnPerFrame);
    CHECK_FALSE(drawn.empty());
    CHECK((drawn.front().colour >> 8U) == 0xC8C8FFU);
    CHECK(drawn.front().size >= 2.0F * GroundFog::kMinSize);
    // Looking the other way, every wisp is more than 5 m out of view: hidden, then gone.
    const EffectsViewer away{
        .position = Vec3{0, 0, 10},
        .target = Vec3{0, 0, 20},
        .window = coney::effects::ViewWindow{.forward = Vec3{0, 0, 1}, .right = Vec3{1, 0, 0}, .up = Vec3{0, -1, 0}}};
    for (int i = 0; i < 3; ++i) {
        fog.step(kFrame, away);
    }
    CHECK(fog.drawn().empty());
    for (const GroundFog::Wisp& wisp : fog.wisps()) {
        CHECK(wisp.alpha == 0);
    }
}

TEST_CASE("a point counts as in view up to the margin outside the frustum", "[fog]") {
    using coney::effects::nearView;
    const coney::effects::ViewWindow window; // looking along +y, 90 degrees wide
    CHECK(nearView(Vec3{}, window, Vec3{0, 10, 0}, 0.0F));
    CHECK_FALSE(nearView(Vec3{}, window, Vec3{0, -10, 0}, 5.0F));
    CHECK(nearView(Vec3{}, window, Vec3{0, -4, 0}, 5.0F));
    CHECK_FALSE(nearView(Vec3{}, window, Vec3{20, 10, 0}, 0.0F));
    CHECK(nearView(Vec3{}, window, Vec3{14, 10, 0}, 5.0F)); // 4 / sqrt(2) = 2.8 m outside
    CHECK_FALSE(nearView(Vec3{}, window, Vec3{0, 200, 0}, 5.0F));
}

TEST_CASE("wisps far from the camera end and near ones are hidden; MaxFogParticles lowers the count", "[fog]") {
    GroundFog fog;
    fog.setMaxWisps(5); // no fog running: nothing changes
    CHECK(fog.maxWisps() == GroundFog::kDefaultMaxWisps);
    fog.start(settings());
    fog.setMaxWisps(5);
    const EffectsViewer viewer{.position = Vec3{0, 0, 1}, .target = Vec3{0, 0, 0}, .window = std::nullopt};
    fog.step(kFrame, viewer);
    CHECK(fog.wisps().size() == 5);
    for (const GroundFog::Wisp& wisp : fog.wisps()) {
        const float dx = wisp.position.x;
        const float dy = wisp.position.y;
        const float dz = wisp.position.z - 1.0F;
        CHECK(wisp.hidden == ((dx * dx) + (dy * dy) + (dz * dz) < GroundFog::kHideWithin * GroundFog::kHideWithin));
    }
    // The camera moves 100 m away: every old wisp is hidden, then ends; new ones come round the new target.
    const EffectsViewer away{.position = Vec3{100, 0, 1}, .target = Vec3{100, 0, 0}, .window = std::nullopt};
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

TEST_CASE("a wisp drifts toward the camera at drift x 1.75-2.25 m/s, a little to either side", "[fog]") {
    GroundFog fog;
    fog.start(settings());
    const EffectsViewer viewer{.position = Vec3{0, -10, 2}, .target = Vec3{0, 0, 0}, .window = std::nullopt};
    fog.step(kFrame, viewer);
    REQUIRE_FALSE(fog.wisps().empty());
    for (const GroundFog::Wisp& wisp : fog.wisps()) {
        const Vec3 v = wisp.velocity;
        const float speed = std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
        CHECK(speed >= 0.4F * 1.75F - 1e-4F);
        CHECK(speed <= 0.4F * 2.25F + 1e-4F);
        // Toward the camera: the velocity points the way the camera lies, give or take the 2 m sideways.
        const Vec3 to{viewer.position.x - wisp.position.x, viewer.position.y - wisp.position.y,
                      viewer.position.z - wisp.position.z};
        CHECK((v.x * to.x) + (v.y * to.y) + (v.z * to.z) > 0.0F);
    }
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

namespace {

// Flat ground at z = 0 everywhere within 100 m of the origin.
std::optional<coney::effects::LitterHit> ground(Vec3 from, Vec3 to) {
    if ((from.z >= 0.0F) == (to.z >= 0.0F) || std::abs(from.x) > 100.0F || std::abs(from.y) > 100.0F) {
        return std::nullopt;
    }
    const float t = from.z / (from.z - to.z);
    return coney::effects::LitterHit{
        .point = Vec3{from.x + ((to.x - from.x) * t), from.y + ((to.y - from.y) * t), 0.0F}, .normal = Vec3{0, 0, 1}};
}

} // namespace

TEST_CASE("litter is armed on the grid round the camera with its kind's looks", "[fog]") {
    CameraLitter litter;
    litter.start(2);
    litter.step(1.0F / 30.0F, Vec3{10, 20, 3}, ground);
    REQUIRE(litter.armed());
    for (const coney::effects::LitterPiece& piece : litter.pieces()) {
        CHECK(piece.rect >= 8);
        CHECK(piece.rect <= 11);
        CHECK(piece.size >= 16.0F / 256.0F);
        CHECK(piece.size <= 32.0F / 256.0F);
        CHECK(piece.grey >= 128);
        CHECK(piece.grey <= 190);
        CHECK(piece.position.x >= 10.0F - 28.0F);
        CHECK(piece.position.x <= 10.0F + 21.0F);
        CHECK(piece.position.y >= 20.0F - 28.0F);
        CHECK(piece.position.y <= 20.0F + 21.0F);
    }
    CHECK(litter.pieces()[0].position.x == 10.0F - 28.0F);
    CHECK(litter.pieces()[63].position.y == 20.0F + 21.0F);
}

TEST_CASE("litter falls, lands and lies flat on the ground", "[fog]") {
    CameraLitter litter;
    litter.start(0);
    for (int i = 0; i < 90; ++i) {
        litter.step(1.0F / 30.0F, Vec3{0, 0, 3}, ground);
    }
    for (const coney::effects::LitterPiece& piece : litter.pieces()) {
        CHECK(piece.grounded);
        CHECK(std::abs(piece.position.z) < 1e-3F);
        CHECK(piece.tilt == 0.0F);
        CHECK(piece.alpha() == 255.0F);
    }
}

TEST_CASE("litter left behind by the camera fades out and comes back round it", "[fog]") {
    CameraLitter litter;
    litter.start(3);
    for (int i = 0; i < 30; ++i) {
        litter.step(1.0F / 30.0F, Vec3{0, 0, 3}, ground);
    }
    // The camera moves 60 m: every piece is too far, fades over 60 updates, and is back round the camera.
    litter.step(1.0F / 30.0F, Vec3{60, 0, 3}, ground);
    CHECK(litter.pieces()[0].fade == CameraLitter::kFadeUpdates);
    CHECK(litter.pieces()[0].alpha() == 4.25F * 60.0F);
    for (int i = 0; i < 60; ++i) {
        litter.step(1.0F / 30.0F, Vec3{60, 0, 3}, ground);
    }
    for (const coney::effects::LitterPiece& piece : litter.pieces()) {
        CHECK(piece.fade == 0);
        CHECK(std::abs(piece.position.x - 60.0F) <= 28.0F);
    }
}

TEST_CASE("a piece with no ground below is put back", "[fog]") {
    CameraLitter litter;
    litter.start(1);
    // Over the edge of the ground: no hit below the pieces, which are placed afresh at each ground ray.
    const auto none = [](Vec3, Vec3) { return std::optional<coney::effects::LitterHit>{}; };
    for (int i = 0; i < 25; ++i) {
        litter.step(1.0F / 30.0F, Vec3{0, 0, 3}, none);
    }
    for (const coney::effects::LitterPiece& piece : litter.pieces()) {
        CHECK_FALSE(piece.grounded);
        CHECK(piece.position.z >
              -1.0F); // at most 20 updates of falling, about 2.2 m, from 2 m below the camera or higher
    }
}
