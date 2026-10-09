// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "combat/throw_aim.h"

using Catch::Approx;
using coney::anim::Vec3;
using namespace coney::combat;

namespace {

// A world with nothing in it but a floor at z = 0 (handle 0, the level mesh).
AimWorld floorOnly() {
    AimWorld world;
    world.anything = [](Vec3 from, Vec3 to, float) -> std::optional<AimHit> {
        if (to.z > 0.0F || from.z <= 0.0F) {
            return std::nullopt;
        }
        return AimHit{.handle = 0, .fraction = from.z / (from.z - to.z)};
    };
    return world;
}

// A world whose pass 1 meets human 7 in any segment crossing y = `at`, and whose pass 2 meets the floor.
AimWorld humanAt(float at, int needSide = 0) {
    AimWorld world = floorOnly();
    world.humans = [at, needSide](Vec3 from, Vec3 to, float, int side) -> std::optional<AimHit> {
        if (needSide != 0 && side != needSide) {
            return std::nullopt;
        }
        if (from.y > at || to.y < at) {
            return std::nullopt;
        }
        return AimHit{.handle = 7, .fraction = (at - from.y) / (to.y - from.y)};
    };
    world.targetPoint = [at](double) -> std::optional<Vec3> { return Vec3{2.0F, at, 1.6F}; };
    return world;
}

} // namespace

TEST_CASE("the thrower's frame turns with his heading: forward is facing(h)", "[combat]") {
    const Vec3 ahead = throwerToWorld(Vec3{0.0F, 1.0F, 0.0F}, 0.5F);
    CHECK(ahead.x == Approx(-std::sin(0.5F)));
    CHECK(ahead.y == Approx(std::cos(0.5F)));
    const Vec3 right = throwerToWorld(Vec3{1.0F, 0.0F, 2.0F}, 0.5F);
    CHECK(right.x == Approx(std::cos(0.5F)));
    CHECK(right.y == Approx(std::sin(0.5F)));
    CHECK(right.z == Approx(2.0F));
}

TEST_CASE("entering the aim starts at a pitch of 0.157 and the release point is the hand offset", "[combat]") {
    ThrowAimState aim;
    aim.enter(Vec3{10.0F, 20.0F, 1.0F}, 0.0F);
    CHECK(aim.active());
    CHECK(aim.pitch() == Approx(kAimStartPitch));
    aim.step(AimInput{}, AimWorld{});
    CHECK(aim.releasePoint().x == Approx(10.3009F));
    CHECK(aim.releasePoint().y == Approx(19.3646F));
    CHECK(aim.releasePoint().z == Approx(2.5865F));
    // A bottle (w = 1) leaves at 20 m/s along the pitch.
    CHECK(coney::anim::length(aim.velocity()) == Approx(20.0F));
    CHECK(aim.velocity().z / aim.velocity().y == Approx(std::tan(kAimStartPitch)));
}

TEST_CASE("the aim's first frame takes the follow camera's heading, later ones keep his own", "[combat]") {
    ThrowAimState aim;
    aim.enter(Vec3{}, 0.2F);
    aim.step(AimInput{.followHeading = 1.0F}, AimWorld{});
    CHECK(aim.heading() == Approx(1.0F));
    aim.step(AimInput{.followHeading = 2.0F}, AimWorld{});
    CHECK(aim.heading() == Approx(1.0F));

    ThrowAimState locked;
    locked.enter(Vec3{}, 0.2F);
    locked.step(AimInput{}, AimWorld{});
    CHECK(locked.heading() == Approx(0.2F));
}

TEST_CASE("the stick turns and pitches the aim outside its dead zone, the pitch clamped to 36 degrees", "[combat]") {
    ThrowAimState aim;
    aim.enter(Vec3{}, 0.0F);
    // Inside the dead zone nothing moves.
    CHECK_FALSE(aim.step(AimInput{.stickX = 26, .stickY = -26, .followHeading = std::nullopt}, AimWorld{}));
    CHECK(aim.heading() == Approx(0.0F));
    CHECK(aim.pitch() == Approx(kAimStartPitch));
    // Full right: (127 - 25) steps clockwise; full up: as many up.
    CHECK(aim.step(AimInput{.stickX = 127, .stickY = -127, .followHeading = std::nullopt}, AimWorld{}));
    CHECK(aim.heading() == Approx(-102 * kAimRate));
    CHECK(aim.pitch() == Approx(kAimStartPitch + (102 * kAimRate)));
    for (int i = 0; i < 100; ++i) {
        aim.step(AimInput{.stickY = 127, .followHeading = std::nullopt}, AimWorld{});
    }
    CHECK(aim.pitch() == Approx(-kAimPitchLimit));
}

TEST_CASE("the arc keeps every sixth of its 108 steps and ends where it meets the floor", "[combat]") {
    ThrowAimState aim;
    aim.enter(Vec3{0.0F, 0.0F, 0.0F}, 0.0F);
    aim.trace(AimInput{}, AimWorld{}, 0);
    CHECK(aim.arcPoints() == kAimTracePoints);
    CHECK(aim.target() == 0);
    CHECK(aim.aimed() == 0);
    // Point 1 is six steps of 1/60 s on, the z speed losing 0.2613 a step after each.
    const Vec3 v = aim.velocity();
    const Vec3 p1 = aim.arc()[1];
    CHECK(p1.y == Approx(aim.releasePoint().y + (v.y * 0.1F)));
    CHECK(p1.z == Approx(aim.releasePoint().z + (v.z * 0.1F) - (kAimTraceGravityStep * 15.0F / 60.0F)));

    aim.trace(AimInput{}, floorOnly(), 0);
    REQUIRE(aim.arcPoints() < kAimTracePoints);
    CHECK(aim.arc()[static_cast<std::size_t>(aim.arcPoints() - 1)].z == Approx(0.0F).margin(1e-4));
    CHECK(aim.aimed() == 0);
}

TEST_CASE("a human in the arc's way becomes the target and is turned to; a wall before him ends the arc", "[combat]") {
    ThrowAimState aim;
    aim.enter(Vec3{}, 0.0F);
    aim.step(AimInput{}, humanAt(5.0F));
    CHECK(aim.target() == 7);
    CHECK(aim.aimed() == 7);
    // Found this frame: he turns straight to the target's point from the release point.
    CHECK(aim.heading() == Approx(std::atan2(-(2.0F - aim.releasePoint().x), 5.0F - aim.releasePoint().y)));

    // A wall at y = 3 comes first: no target, nothing aimed at (the level mesh).
    AimWorld walled = humanAt(5.0F);
    walled.anything = [](Vec3 from, Vec3 to, float) -> std::optional<AimHit> {
        if (from.y > 3.0F || to.y < 3.0F) {
            return std::nullopt;
        }
        return AimHit{.handle = 0, .fraction = (3.0F - from.y) / (to.y - from.y)};
    };
    ThrowAimState blocked;
    blocked.enter(Vec3{}, 0.0F);
    blocked.trace(AimInput{}, walled, 0);
    CHECK(blocked.target() == 0);
    CHECK(blocked.aimed() == 0);
    CHECK(blocked.arc()[static_cast<std::size_t>(blocked.arcPoints() - 1)].y == Approx(3.0F));

    // An object with THROWNWEAPONTARGET in the way is aimed at, though not a target.
    AimWorld marker;
    marker.anything = [](Vec3 from, Vec3 to, float) -> std::optional<AimHit> {
        if (from.y > 4.0F || to.y < 4.0F) {
            return std::nullopt;
        }
        return AimHit{.handle = 42, .fraction = (4.0F - from.y) / (to.y - from.y)};
    };
    ThrowAimState objective;
    objective.enter(Vec3{}, 0.0F);
    objective.step(AimInput{}, marker);
    CHECK(objective.target() == 0);
    CHECK(objective.aimed() == 42);
    // Leaving keeps what was aimed at (HuIsAimingAt) and puts the pitch back.
    objective.leave();
    CHECK_FALSE(objective.active());
    CHECK(objective.aimed() == 42);
    CHECK(objective.pitch() == Approx(kAimStartPitch));
}

TEST_CASE("the stick turns him off a human target only after the five frames that face it", "[combat]") {
    ThrowAimState aim;
    aim.enter(Vec3{}, 0.0F);
    const AimWorld world = humanAt(5.0F);
    aim.step(AimInput{}, world);
    const float facing = aim.heading();
    // The four frames left of the five face him whatever the stick does. The heading is taken from the release point,
    // which moves with the heading, so it settles by a few hundredths over them rather than turning with the stick
    // (0.06 rad a frame at full tilt).
    for (int i = 0; i < kAimFaceFrames - 1; ++i) {
        CHECK_FALSE(aim.step(AimInput{.stickX = 127, .followHeading = std::nullopt}, world));
        CHECK(aim.heading() == Approx(facing).margin(0.04));
    }
    // Then the stick turns him.
    CHECK(aim.step(AimInput{.stickX = 127, .followHeading = std::nullopt}, world));
    CHECK(aim.heading() < facing);
}

TEST_CASE("pushed sideways the stick asks pass 1 for a target on that side only", "[combat]") {
    ThrowAimState aim;
    aim.enter(Vec3{}, 0.0F);
    aim.step(AimInput{.stickX = 60, .followHeading = std::nullopt}, humanAt(5.0F, -1));
    CHECK(aim.target() == 0);
    ThrowAimState right;
    right.enter(Vec3{}, 0.0F);
    right.step(AimInput{.stickX = 60, .followHeading = std::nullopt}, humanAt(5.0F, 1));
    CHECK(right.target() == 7);
}
