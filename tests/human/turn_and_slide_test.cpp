// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/turn_and_slide.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "human/locomotion.h"

// An attack's steer onto its target (docs/research/combat.md#targets): the turn and the slide at a constant rate over
// the time to the clip's first event + 0.1 s, the last step only for the time left; the goal led by the target's
// velocity and short of it by the reach. Synthetic numbers only.

using Catch::Approx;
using coney::anim::AnimClip;
using coney::anim::ClipEvent;
using coney::anim::Vec3;
using coney::human::attackSteerGoal;
using coney::human::firstContactTime;
using coney::human::kStepSeconds;
using coney::human::TurnAndSlide;
using coney::human::TurnAndSlideStep;

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;

// A clip of `frames` frames with events (frame, type).
AnimClip clipWith(float frames, std::initializer_list<std::pair<std::uint16_t, std::uint16_t>> events) {
    AnimClip clip;
    clip.duration = frames / 30.0F;
    for (const auto& [frame, type] : events) {
        clip.events.push_back(
            ClipEvent{.frame = frame, .type = type, .value = 0, .word = 0, .position = {}, .rotation = {}});
    }
    return clip;
}

} // namespace

TEST_CASE("the steer's time is the time to the clip's first hit or window event at its rate", "[human][steer]") {
    // X1 as Rembrandt's clip has it: a warning (0x24) and an anim cue (0xb) first, then the hit (0xf) at frame 5 and
    // the window (0x2c) at 8, played at 0.8: the hit's 0.208 s is the time; at runtime the turn lasted 9.25 updates,
    // that + 0.1 s.
    const AnimClip x1 = clipWith(30.0F, {{3, 0x24}, {3, 0xb}, {5, 0xf}, {8, 0x2c}, {16, 0x2d}});
    CHECK(firstContactTime(x1, 0.8F) == Approx(5.0F / 30.0F / 0.8F));
    CHECK((firstContactTime(x1, 0.8F) + coney::human::kSteerLeadExtraSeconds) / kStepSeconds == Approx(9.25F));
    // Every type that ends a steer counts; with none, the clip's whole playing time.
    for (const std::uint16_t type : std::array<std::uint16_t, 7>{0x9, 0xf, 0x13, 0x2c, 0x34, 0x36, 0x41}) {
        CHECK(firstContactTime(clipWith(20.0F, {{4, 0x24}, {7, type}}), 1.0F) == Approx(7.0F / 30.0F));
    }
    CHECK(firstContactTime(clipWith(15.0F, {{2, 0x24}, {6, 0x2d}}), 0.75F) == Approx(0.5F / 0.75F));
}

TEST_CASE("the steer turns at a constant rate over its time, the last step only for the time left", "[human][steer]") {
    // 23.43° over 9.25 updates, as X1's turn at runtime: 2.533° on each of 9 updates, then 0.633°, then nothing.
    TurnAndSlide steer;
    const float seconds = 9.25F * kStepSeconds;
    steer.turnToOver(0.0F, 23.43F * kDegrees, seconds);
    float total = 0.0F;
    for (int update = 0; update < 9; ++update) {
        const TurnAndSlideStep step = steer.step(kStepSeconds);
        CHECK(step.turn / kDegrees == Approx(2.533F).margin(0.001));
        total += step.turn;
    }
    CHECK(steer.step(kStepSeconds).turn / kDegrees == Approx(0.633F).margin(0.001));
    CHECK_FALSE(steer.turning());
    CHECK(steer.step(kStepSeconds).turn == 0.0F);
    CHECK((total / kDegrees) + 0.633F == Approx(23.43F).margin(0.01));
    // The short way round, and nothing under 0.01 rad or without time.
    steer.turnToOver(170.0F * kDegrees, -170.0F * kDegrees, seconds);
    CHECK(steer.turnRate() > 0.0F);
    steer.turnToOver(0.0F, 0.009F, seconds);
    CHECK_FALSE(steer.turning());
    steer.turnToOver(0.0F, 1.0F, 0.0F);
    CHECK_FALSE(steer.turning());
}

TEST_CASE("the steer slides to its goal at a constant velocity, within its limits", "[human][steer]") {
    TurnAndSlide steer;
    // 0.3 m in 0.25 s: 1.2 m/s for 7 updates, then a quarter of it for the time left; 0.3 m in all.
    steer.moveToOver(Vec3{}, Vec3{0.0F, 0.3F, 5.0F}, 7.5F * kStepSeconds);
    float travelled = 0.0F;
    for (int update = 0; update < 8; ++update) {
        const TurnAndSlideStep step = steer.step(kStepSeconds);
        CHECK(step.velocity.x == 0.0F);
        CHECK(step.velocity.z == 0.0F);
        CHECK(step.velocity.y == Approx(update < 7 ? 1.2F : 0.6F));
        travelled += step.velocity.y * kStepSeconds;
    }
    CHECK(travelled == Approx(0.3F));
    CHECK_FALSE(steer.sliding());
    // None under 0.01 m, at 13 m or more, or faster than 50 m/s over more than one update (one update allows any).
    steer.moveToOver(Vec3{}, Vec3{0.009F, 0.0F, 0.0F}, 0.3F);
    CHECK_FALSE(steer.sliding());
    steer.moveToOver(Vec3{}, Vec3{13.0F, 0.0F, 0.0F}, 1.0F);
    CHECK_FALSE(steer.sliding());
    steer.moveToOver(Vec3{}, Vec3{6.0F, 0.0F, 0.0F}, 0.1F);
    CHECK_FALSE(steer.sliding());
    steer.moveToOver(Vec3{}, Vec3{6.0F, 0.0F, 0.0F}, kStepSeconds);
    CHECK(steer.sliding());
    steer.clear();
    CHECK_FALSE(steer.sliding());
    CHECK_FALSE(steer.turning());
}

TEST_CASE("the steer's goal is the target led by its velocity, short of it by the reach", "[human][steer]") {
    // A target 1.5 m ahead standing still: stand 1.0 m short of it.
    const auto still = attackSteerGoal(Vec3{}, Vec3{0.0F, 1.5F, 0.0F}, Vec3{}, 1.0F, 0.2F);
    CHECK(still.aim.y == Approx(1.5F));
    CHECK(still.stand.x == Approx(0.0F).margin(1e-6));
    CHECK(still.stand.y == Approx(0.5F));
    // Walking towards the attacker at 1.5 m/s: led by 1.5 × (0.2 + 0.1) = 0.45 m, so it is struck 1.05 m away.
    const auto coming = attackSteerGoal(Vec3{}, Vec3{0.0F, 1.5F, 0.0F}, Vec3{0.0F, -1.5F, 0.0F}, 1.0F, 0.2F);
    CHECK(coming.aim.y == Approx(1.05F));
    CHECK(coming.stand.y == Approx(0.05F));
    // A lead over 1 m that carries it away is cut to 0.5 m: 5 m/s × 0.3 s = 1.5 m, so 0.5 m.
    const auto fleeing = attackSteerGoal(Vec3{}, Vec3{0.0F, 1.5F, 0.0F}, Vec3{0.0F, 5.0F, 0.0F}, 1.0F, 0.2F);
    CHECK(fleeing.aim.y == Approx(2.0F));
    // The same lead towards the attacker is kept whole; a target nearer than the reach puts the goal behind him.
    const auto rushing = attackSteerGoal(Vec3{}, Vec3{0.0F, 2.5F, 0.0F}, Vec3{0.0F, -5.0F, 0.0F}, 1.2F, 0.2F);
    CHECK(rushing.aim.y == Approx(1.0F));
    CHECK(rushing.stand.y == Approx(-0.2F));
    // The goal keeps the attacker's height.
    CHECK(attackSteerGoal(Vec3{0.0F, 0.0F, 3.0F}, Vec3{1.0F, 0.0F, 9.0F}, Vec3{}, 0.5F, 0.2F).stand.z == 3.0F);
}
