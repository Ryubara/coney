// SPDX-License-Identifier: GPL-3.0-or-later
// The Level view's fly camera and its click selection (docs/guides/studio/level-view.md#fly-camera, #selection).
#include "studio/level_camera.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sandbox/sandbox_layout.h"
#include "studio/level_document.h"

using Catch::Approx;
namespace anim = coney::anim;
using coney::studio::FlyCamera;
using coney::studio::FlyInput;
using coney::studio::LevelDocument;
using coney::studio::LevelView;
using coney::studio::PanelPoint;

namespace {

// One fixed step, 1/30 s (docs/guides/conventions.md#update-and-render).
constexpr float kStep = 1.0F / 30.0F;

// A camera 10 m south of the origin, 1 m up, looking north (+y) and level, for an 800 x 600 panel.
LevelView southView() {
    const FlyCamera camera({0.0F, -10.0F, 1.0F}, 0.0F, 0.0F);
    return coney::studio::levelView(camera.pose(), 800.0F, 600.0F, 0.5F, 0.1F);
}

// A layout of a box at the origin, a spawn 5 m east of it and a camera view.
LevelDocument boxAndSpawn() {
    auto layout = coney::sandbox::parseSandboxLayout("box at=0,0,0 size=2,2,2\n"
                                                     "spawn east at=5,0,0\n");
    REQUIRE(layout.has_value());
    return LevelDocument::fromLayout(std::move(*layout));
}

} // namespace

TEST_CASE("a camera at a layout's view looks along the game's heading", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the controls and selection (design); speeds are Coney's
    // choices. Expected: a view's yaw 0 looks along +y and turns counter-clockwise from above (docs/guides/sandbox.md,
    // `view`).
    const FlyCamera north({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    CHECK(north.pose().forward.y == Approx(1.0F));
    CHECK(north.pose().right.x == Approx(1.0F));
    const FlyCamera west({0.0F, 0.0F, 0.0F}, 90.0F, 0.0F);
    CHECK(west.pose().forward.x == Approx(-1.0F).margin(1e-6));
    const FlyCamera down({0.0F, 0.0F, 0.0F}, 0.0F, -30.0F);
    CHECK(down.pose().forward.z == Approx(-0.5F));
    CHECK(down.pitchDegrees() == Approx(-30.0F));
}

TEST_CASE("the point the camera looks at is in the middle of the panel and a ray goes back through it", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the controls and selection (design); speeds are Coney's
    // choices.
    const LevelView view = southView();
    const auto centre = coney::studio::project(view, {0.0F, 0.0F, 1.0F});
    REQUIRE(centre.has_value());
    if (!centre) {
        return; // REQUIRE has failed the test; this keeps clang-tidy's optional check satisfied
    }
    CHECK(centre->x == Approx(400.0F));
    CHECK(centre->y == Approx(300.0F));
    // East is to the right, up is towards the top.
    const auto eastUp = coney::studio::project(view, {1.0F, 0.0F, 2.0F});
    REQUIRE(eastUp.has_value());
    if (!eastUp) {
        return; // REQUIRE has failed the test; this keeps clang-tidy's optional check satisfied
    }
    CHECK(eastUp->x > 400.0F);
    CHECK(eastUp->y < 300.0F);
    CHECK_FALSE(coney::studio::project(view, {0.0F, -20.0F, 1.0F}).has_value()); // behind the camera
    const auto ray = coney::studio::rayThrough(view, *eastUp);
    const float t = 10.0F / ray.direction.y;
    CHECK(ray.origin.x + (ray.direction.x * t) == Approx(1.0F));
    CHECK(ray.origin.z + (ray.direction.z * t) == Approx(2.0F));
}

TEST_CASE("a click selects the nearest entity under it and empty space selects nothing", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the controls and selection (design); speeds are Coney's
    // choices.
    const LevelDocument level = boxAndSpawn();
    const LevelView view = southView();
    const auto box = coney::studio::project(view, {0.0F, 0.0F, 1.0F});
    REQUIRE(box.has_value());
    if (!box) {
        return; // REQUIRE has failed the test; this keeps clang-tidy's optional check satisfied
    }
    CHECK(coney::studio::pickEntity(level, view, *box) == 0U);
    const auto spawn = coney::studio::project(view, {5.0F, 0.0F, 0.9F});
    REQUIRE(spawn.has_value());
    if (!spawn) {
        return; // REQUIRE has failed the test; this keeps clang-tidy's optional check satisfied
    }
    CHECK(coney::studio::pickEntity(level, view, *spawn) == 1U);
    CHECK_FALSE(coney::studio::pickEntity(level, view, PanelPoint{400.0F, 20.0F}).has_value()); // the sky
}

TEST_CASE("the fly camera moves only while the right button is held", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the controls and selection (design); speeds are Coney's
    // choices.
    FlyCamera camera({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    camera.step(FlyInput{.forward = 1.0F}, kStep);
    CHECK(camera.position().y == Approx(0.0F));
    // Expected: W flies along the view at the base speed, 6 m/s, from the first moment (the acceleration starts at 1,
    // docs/guides/studio/level-view.md#fly-acceleration), so a whole second's step moves 6 m.
    camera.step(FlyInput{.flying = true, .forward = 1.0F}, 1.0F);
    CHECK(camera.position().y == Approx(6.0F));
}

TEST_CASE("Shift flies faster, Alt slower, E rises and the wheel changes the base speed", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the design: Shift faster and Alt slower while held, the
    // wheel's speed stays; the factors are Coney's choices.
    FlyCamera fast({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    fast.step(FlyInput{.flying = true, .strafe = 1.0F, .fast = true}, 1.0F);
    CHECK(fast.position().x == Approx(FlyCamera::kBaseSpeed * FlyCamera::kFastFactor));
    FlyCamera slow({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    slow.step(FlyInput{.flying = true, .rise = 1.0F, .slow = true}, 1.0F);
    CHECK(slow.position().z == Approx(FlyCamera::kBaseSpeed * FlyCamera::kSlowFactor));
    FlyCamera wheel({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    wheel.step(FlyInput{.flying = true, .wheel = 2.0F}, kStep);
    CHECK(wheel.baseSpeed() == Approx(FlyCamera::kBaseSpeed * FlyCamera::kWheelFactor * FlyCamera::kWheelFactor));
    wheel.step(FlyInput{.flying = true, .forward = 1.0F}, 1.0F);
    CHECK(wheel.position().y == Approx(wheel.baseSpeed()));
}

TEST_CASE("the mouse turns the camera and the pitch stops short of straight up", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the controls and selection (design); speeds are Coney's
    // choices.
    FlyCamera camera({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    // Moving the mouse right turns the view right (clockwise from above): the heading falls.
    camera.step(FlyInput{.flying = true, .lookX = 100.0F}, kStep);
    CHECK(camera.yawDegrees() < 0.0F);
    camera.step(FlyInput{.flying = true, .lookY = -100000.0F}, kStep);
    CHECK(camera.pitchDegrees() == Approx(FlyCamera::kMaxPitch * 180.0F / 3.14159265F).epsilon(1e-4));
}

TEST_CASE("the same input flies the same path every time", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the controls and selection (design); speeds are Coney's
    // choices.
    const auto fly = [] {
        FlyCamera camera({1.0F, 2.0F, 3.0F}, 45.0F, -10.0F);
        for (int i = 0; i < 90; ++i) {
            camera.step(FlyInput{.flying = true, .lookX = 3.0F, .lookY = -1.0F, .forward = 0.7F, .strafe = -0.3F},
                        kStep);
        }
        return camera.position();
    };
    const auto a = fly();
    const auto b = fly();
    CHECK(anim::distance(a, FlyCamera({1.0F, 2.0F, 3.0F}, 45.0F, -10.0F).position()) > 1.0F); // it flew
    CHECK(a.x == b.x);
    CHECK(a.y == b.y);
    CHECK(a.z == b.z);
}

TEST_CASE("F glides the camera to frame the selection, slow at first and faster each step", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, F frames the selection (Unity's Frame Selected); the
    // eased glide is the maintainer's request for the prototype (2026-10-28), and so is its length: a quarter of a
    // second, twice as quick as the first build's half.
    CHECK(FlyCamera::kGlideSeconds == Approx(0.25F));
    FlyCamera camera({0.0F, -30.0F, 2.0F}, 0.0F, -10.0F);
    const anim::Vec3 start = camera.position();
    const coney::studio::Bounds box{.min = {9.0F, 9.0F, 0.0F}, .max = {11.0F, 11.0F, 2.0F}};
    camera.frame(box, 0.5F);
    REQUIRE(camera.gliding());
    // The first step has hardly left the start; each step covers more than the one before.
    std::vector<float> steps;
    anim::Vec3 before = camera.position();
    CHECK(before.x == start.x);
    while (camera.gliding()) {
        camera.step(FlyInput{}, kStep);
        steps.push_back(anim::distance(before, camera.position()));
        before = camera.position();
    }
    CHECK(steps.size() == 8); // 0.25 s of 1/30 s steps: seven whole ones and a short last one
    // (The eighth is the short one that lands on the target, so only the seven whole steps are compared.)
    for (std::size_t i = 1; i + 1 < steps.size(); ++i) {
        CHECK(steps[i] > steps[i - 1]);
    }
    // It ends exactly at the target, the heading and pitch kept, the box's middle in the middle of the view and its
    // bounding sphere inside the view window.
    CHECK(camera.position().x == camera.glideTarget().x);
    CHECK(camera.position().y == camera.glideTarget().y);
    CHECK(camera.yawDegrees() == Approx(0.0F).margin(1e-4));
    CHECK(camera.pitchDegrees() == Approx(-10.0F));
    const LevelView view = coney::studio::levelView(camera.pose(), 800.0F, 600.0F, 0.5F, 0.1F);
    const auto middle = coney::studio::project(view, {10.0F, 10.0F, 1.0F});
    REQUIRE(middle.has_value());
    if (!middle) {
        return; // REQUIRE has failed the test; this keeps clang-tidy's optional check satisfied
    }
    CHECK(middle->x == Approx(400.0F));
    CHECK(middle->y == Approx(300.0F));
    const float radius = anim::length(anim::Vec3{1.0F, 1.0F, 1.0F});
    const float distance = anim::distance(camera.position(), {10.0F, 10.0F, 1.0F});
    CHECK(radius / distance == Approx(std::sin(std::atan(0.5F))));
}

TEST_CASE("flying takes the camera back from a glide", "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-camera, the right button flies the camera at once.
    FlyCamera camera({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    camera.frame(coney::studio::Bounds{.min = {50.0F, 50.0F, 0.0F}, .max = {51.0F, 51.0F, 1.0F}}, 0.5F);
    camera.step(FlyInput{}, kStep);
    CHECK(camera.position().x > 0.0F); // under way
    camera.step(FlyInput{.flying = true}, kStep);
    CHECK_FALSE(camera.gliding());
    const anim::Vec3 held = camera.position();
    camera.step(FlyInput{}, kStep);
    CHECK(camera.position().x == held.x);
}

TEST_CASE("the camera speeds up the longer the movement keys are held and starts over when they are let go",
          "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-acceleration, Unity's Camera Acceleration (continuously faster
    // until movement stops); the rate 1.8 a second and the ceiling of 8 are Coney's choices.
    CHECK(FlyCamera::accelerationAfter(0.0F) == Approx(1.0F));
    CHECK(FlyCamera::accelerationAfter(1.0F) == Approx(1.8F));
    CHECK(FlyCamera::accelerationAfter(2.0F) == Approx(3.24F));
    CHECK(FlyCamera::accelerationAfter(60.0F) == Approx(8.0F)); // it stops at the ceiling
    FlyCamera camera({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    const auto speedNow = [&camera](FlyInput input) {
        const float before = camera.position().y;
        camera.step(input, kStep);
        return (camera.position().y - before) / kStep;
    };
    const FlyInput forward{.flying = true, .forward = 1.0F};
    CHECK(speedNow(forward) == Approx(FlyCamera::kBaseSpeed).epsilon(1e-3)); // the first step is at the base speed
    for (int i = 1; i < 30; ++i) {
        camera.step(forward, kStep);
    }
    CHECK(camera.acceleration() == Approx(1.8F)); // a second of flying
    CHECK(speedNow(forward) == Approx(FlyCamera::kBaseSpeed * 1.8F).epsilon(1e-3));
    // Letting go of every movement key starts it over, and so does Q or E alone count as movement.
    camera.step(FlyInput{.flying = true}, kStep);
    CHECK(camera.acceleration() == Approx(1.0F));
    camera.step(FlyInput{.flying = true, .rise = 1.0F}, kStep);
    camera.step(FlyInput{.flying = true, .rise = 1.0F}, kStep);
    CHECK(camera.acceleration() > 1.0F);
    // Letting go of the right button starts it over too.
    camera.step(FlyInput{}, kStep);
    CHECK(camera.acceleration() == Approx(1.0F));
}

TEST_CASE("Shift multiplies the accelerating speed and Alt slows it, the acceleration going on while they are held",
          "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-acceleration, Shift multiplies the speed the acceleration has
    // reached (Unity's Shift flies faster); the factors 4 and 0.25 are Coney's choices.
    const auto after = [](float seconds, bool fast, bool slow) {
        FlyCamera camera({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
        const FlyInput input{.flying = true, .forward = 1.0F, .fast = fast, .slow = slow};
        for (int i = 0; i < std::lround(seconds * 30.0F); ++i) {
            camera.step(input, kStep);
        }
        const float before = camera.position().y;
        camera.step(input, kStep);
        return (camera.position().y - before) / kStep; // the speed of the next step, metres a second
    };
    // Held from the start, Shift is 4 times the plain speed at every moment, and both grow by 1.8 a second.
    CHECK(after(0.0F, true, false) == Approx(6.0F * 4.0F).epsilon(1e-3));
    CHECK(after(1.0F, true, false) == Approx(6.0F * 4.0F * 1.8F).epsilon(1e-3));
    CHECK(after(2.0F, true, false) == Approx(6.0F * 4.0F * 3.24F).epsilon(1e-3));
    CHECK(after(2.0F, false, false) == Approx(6.0F * 3.24F).epsilon(1e-3));
    CHECK(after(2.0F, false, true) == Approx(6.0F * 0.25F * 3.24F).epsilon(1e-3));
    // At the ceiling Shift flies 4 x 8 times the base speed.
    CHECK(after(10.0F, true, false) == Approx(6.0F * 4.0F * 8.0F).epsilon(1e-3));
    // Shift pressed late meets the speed already reached: no restart.
    FlyCamera camera({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    for (int i = 0; i < 60; ++i) {
        camera.step(FlyInput{.flying = true, .forward = 1.0F}, kStep);
    }
    const float before = camera.position().y;
    camera.step(FlyInput{.flying = true, .forward = 1.0F, .fast = true}, kStep);
    CHECK((camera.position().y - before) / kStep == Approx(6.0F * 4.0F * 3.24F).epsilon(1e-3));
}

TEST_CASE("the look is per pixel of the display's scale, so a hand movement turns the same on any display",
          "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-look, 1x counted in the display's scale-1 pixels; the default
    // 0.0025 radians (0.143 degrees) a pixel, lower than the first build's 0.004, is Coney's choice.
    CHECK(FlyCamera::lookRadiansPerPixel(1.0F, 1.0F) == Approx(0.0025F));
    CHECK(FlyCamera::lookRadiansPerPixel(2.0F, 1.0F) == Approx(0.00125F)); // twice the pixels for the same movement
    CHECK(FlyCamera::lookRadiansPerPixel(1.5F, 1.0F) == Approx(0.0025F / 1.5F));
    CHECK(FlyCamera::lookRadiansPerPixel(1.0F, 2.0F) == Approx(0.005F));  // the sensitivity setting scales it
    CHECK(FlyCamera::lookRadiansPerPixel(0.0F, 1.0F) == Approx(0.0025F)); // a scale of nothing counts as 1
    // A camera given the pixels turns by that much a pixel, and the default input is the default sensitivity.
    CHECK(FlyInput{}.lookRadiansPerPixel == Approx(FlyCamera::lookRadiansPerPixel(1.0F, 1.0F)));
    FlyCamera camera({0.0F, 0.0F, 0.0F}, 0.0F, 0.0F);
    camera.step(
        FlyInput{.flying = true, .lookX = 100.0F, .lookRadiansPerPixel = FlyCamera::lookRadiansPerPixel(2.0F, 1.0F)},
        kStep);
    CHECK(camera.yawDegrees() == Approx(-100.0F * 0.00125F * 180.0F / 3.14159265F));
}
