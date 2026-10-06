// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's scene stage handing a scene's camera to player 1's cameras (docs/research/scenes.md,
// docs/research/camera.md#scenes): the scene's start pushes the camera shown, its keys set the scene camera's view
// and its end pops the camera back over the scene's blend.
#include "platform/scene_stage.h"

#include <cmath>
#include <expected>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/cameras.h"
#include "camera/locked_camera.h"
#include "core/error.h"
#include "scenes/scene_host.h"
#include "scenes/scene_record.h"

using coney::anim::Quat;
using coney::anim::Vec3;
using coney::camera::CameraKind;
using coney::camera::Cameras;
using coney::platform::SceneStage;
using coney::platform::StageCharacter;
using coney::scenes::SceneLens;
using coney::scenes::ScenePose;

namespace {

constexpr double kLockedHandle = 8.0;

// A stage that draws every bound human itself (no puppets, so no characters to load) and prints nothing.
SceneStage quietStage() {
    return SceneStage(
        [](std::string_view) -> std::expected<StageCharacter, coney::Error> {
            return coney::fail(coney::ErrorCode::NotFound, "no characters in this test");
        },
        [](double, std::size_t, std::string_view) { return std::string{}; }, [](std::string_view) {});
}

// Whether two vectors are the same within a hundredth on every axis.
bool near(Vec3 a, Vec3 b) {
    return std::abs(a.x - b.x) <= 0.01F && std::abs(a.y - b.y) <= 0.01F && std::abs(a.z - b.z) <= 0.01F;
}

} // namespace

TEST_CASE("a scene's camera is player 1's scene camera, pushed over the camera shown and popped back",
          "[scene_stage]") {
    Cameras cameras;
    cameras.createLocked(kLockedHandle, coney::camera::LockedCamera{.position = Vec3{50.0F, 40.0F, 3.0F},
                                                                    .headingDegrees = 90.0F,
                                                                    .pitchDegrees = 10.0F,
                                                                    .rollDegrees = 0.0F,
                                                                    .fieldOfView = 50.0F,
                                                                    .nearClip = 0.1F,
                                                                    .farClip = 100.0F});
    cameras.makeActive(kLockedHandle, 0.0F);
    REQUIRE(cameras.current().kind == CameraKind::Locked);

    SceneStage stage = quietStage();
    stage.setCameras(&cameras);
    // The identity looks along +y, so the scene camera looks at the point a metre north of it.
    const SceneLens lens{.fieldOfView = 40.0F, .nearClip = 0.2F, .farClip = 90.0F};
    stage.cameraBegin(ScenePose{.position = Vec3{1.0F, 2.0F, 3.0F}, .rotation = Quat{}}, lens);
    CHECK(cameras.current().kind == CameraKind::Scene);
    REQUIRE(cameras.stack().size() == 1);
    CHECK(cameras.stack().back().kind == CameraKind::Locked);
    CHECK(near(cameras.view().position, Vec3{1.0F, 2.0F, 3.0F}));
    CHECK(near(cameras.view().lookAt, Vec3{1.0F, 3.0F, 3.0F}));
    CHECK(cameras.view().fieldOfView == 40.0F);
    CHECK(cameras.view().farClip == 90.0F);
    CHECK(stage.cameraActive());

    // A key moves the scene camera's view.
    stage.cameraPose(ScenePose{.position = Vec3{1.5F, 2.0F, 3.0F}, .rotation = Quat{}}, lens);
    CHECK(near(cameras.view().position, Vec3{1.5F, 2.0F, 3.0F}));

    // The end pops the locked camera back, at once with no blend.
    stage.cameraEnd(0.0F);
    CHECK_FALSE(stage.cameraActive());
    CHECK(cameras.stack().empty());
    CHECK(cameras.current().kind == CameraKind::Locked);
    CHECK_FALSE(cameras.blending());

    // With BlendCam above 0 the cameras blend back from the scene camera's last view.
    stage.cameraBegin(ScenePose{.position = Vec3{1.0F, 2.0F, 3.0F}, .rotation = Quat{}}, lens);
    stage.cameraEnd(1.0F);
    CHECK(cameras.current().kind == CameraKind::Locked);
    CHECK(cameras.blending());
}

TEST_CASE("without player 1's cameras the stage keeps the scene camera alone", "[scene_stage]") {
    SceneStage stage = quietStage();
    stage.cameraBegin(ScenePose{.position = Vec3{1.0F, 2.0F, 3.0F}, .rotation = Quat{}}, SceneLens{});
    CHECK(stage.cameraActive());
    stage.cameraEnd(1.0F);
    CHECK_FALSE(stage.cameraActive());
}
