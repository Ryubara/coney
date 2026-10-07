// SPDX-License-Identifier: GPL-3.0-or-later
// The room-smoke overlay (effects/room_smoke.h): its drifts' ranges, the blend from one drift to the next, the camera's
// tilt and turn, and a restart while running. Synthetic viewers at 30 steps a second.
#include "effects/room_smoke.h"

#include <cmath>
#include <cstdint>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using coney::effects::EffectsViewer;
using coney::effects::RoomSmoke;
using coney::effects::RoomSmokeSettings;

namespace {

// One fixed step, s.
constexpr float kStep = 1.0F / 30.0F;

// The scripts' settings: a grey-blue haze, alpha 220-240, amount 0.5.
RoomSmokeSettings scriptSettings() {
    return RoomSmokeSettings{.tint = {120, 120, 140}, .lowestAlpha = 220, .highestAlpha = 240, .amount = 0.5F};
}

// A camera at the origin looking level along +y.
EffectsViewer level() { return EffectsViewer{.position = {0.0F, 0.0F, 0.0F}, .target = {0.0F, 10.0F, 0.0F}}; }

} // namespace

TEST_CASE("room smoke draws nothing until started and keeps its drifts in their ranges", "[effects]") {
    RoomSmoke smoke;
    smoke.step(kStep, level());
    CHECK_FALSE(smoke.running());
    smoke.start(scriptSettings());
    CHECK(smoke.running());
    for (int i = 0; i < 30 * 60; ++i) {
        smoke.step(kStep, level());
        const auto& drift = smoke.drift();
        REQUIRE(std::abs(drift.scroll) <= 0.0015F * 0.5F + 1e-6F);
        REQUIRE(drift.widthScale >= 1.63F - 1e-4F);
        REQUIRE(drift.widthScale <= 1.93F + 1e-4F);
        REQUIRE(drift.heightScale >= 1.0F - 1e-4F);
        REQUIRE(drift.heightScale <= 1.2F + 1e-4F);
        REQUIRE(smoke.sprite().colour[3] >= 220);
        REQUIRE(smoke.sprite().colour[3] <= 240);
        REQUIRE(smoke.sprite().u >= 0.0F);
        REQUIRE(smoke.sprite().u < 1.0F);
    }
    CHECK(smoke.sprite().colour[0] == 120);
    CHECK(smoke.sprite().colour[2] == 140);
    // The sprite is 1.3 × the width scale wide and the height scale high, overlay units.
    CHECK(smoke.sprite().width == Catch::Approx(1.3F * smoke.drift().widthScale));
    CHECK(smoke.sprite().height == Catch::Approx(smoke.drift().heightScale));
    smoke.stop();
    CHECK_FALSE(smoke.running());
}

TEST_CASE("room smoke rides with the camera's tilt and slides as it turns", "[effects]") {
    RoomSmoke smoke;
    smoke.start(scriptSettings());
    smoke.step(kStep, level());
    // Level: GUI y 0.175.
    CHECK(smoke.sprite().guiY == Catch::Approx(0.175F).margin(1e-4F));
    // Looking 60° down is past -50°: the top of the range, -0.25.
    smoke.step(kStep, EffectsViewer{.position = {0.0F, 0.0F, 0.0F}, .target = {0.0F, 1.0F, -std::sqrt(3.0F)}});
    CHECK(smoke.sprite().guiY == Catch::Approx(-0.25F).margin(1e-4F));
    // A half turn between ticks slides the haze by twice the heading's change (a half of 0.9999), wrapped: what the
    // drift alone would scroll, plus 0.9999.
    RoomSmoke turning;
    turning.start(scriptSettings());
    turning.step(kStep, level());
    const float before = turning.sprite().u;
    turning.step(kStep, EffectsViewer{.position = {0.0F, 0.0F, 0.0F}, .target = {0.0F, -10.0F, 0.0F}});
    const float moved = turning.sprite().u - before - turning.drift().scroll;
    const float wrapped = moved - std::floor(moved);
    CHECK((wrapped == Catch::Approx(0.9999F).margin(1e-3F) || wrapped < 1e-3F));
}

TEST_CASE("room smoke started again while running jumps to a new drift at its next tick", "[effects]") {
    RoomSmoke smoke;
    smoke.start(scriptSettings());
    smoke.step(kStep, level());
    smoke.start(RoomSmokeSettings{.tint = {10, 20, 30}, .lowestAlpha = 50, .highestAlpha = 50, .amount = 1.0F});
    smoke.step(kStep, level());
    CHECK(smoke.sprite().colour[3] == 50);
    CHECK(smoke.sprite().colour[0] == 10);
}

TEST_CASE("room smoke without a camera waits", "[effects]") {
    RoomSmoke smoke;
    smoke.start(scriptSettings());
    const float u = smoke.sprite().u;
    smoke.step(kStep, std::nullopt);
    CHECK(smoke.sprite().u == u);
    CHECK(smoke.sprite().width == 0.0F);
}
