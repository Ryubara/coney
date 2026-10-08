// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's scene stage handing a scene's camera to player 1's cameras (docs/research/scenes.md,
// docs/research/camera.md#scenes): the scene's start pushes the camera shown, its keys set the scene camera's view
// and its end pops the camera back over the scene's blend; and the scene soundtrack
// (docs/research/sound.md#scene-sound) kept from its preparation to its event, and stopped only by a skip or a give-up.
#include "platform/scene_stage.h"

#include <cmath>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "audio/audio_fixtures.h"
#include "audio/mixer.h"
#include "audio/sound_data.h"
#include "audio/sound_engine.h"
#include "audio/sound_player.h"
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
                                                                    .farClip = 100.0F,
                                                                    .keptInView = {}});
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

TEST_CASE("a released human is handed to the play mode at the release, at its end pose after a skip", "[scene_stage]") {
    SceneStage stage = quietStage();
    std::vector<SceneStage::Release> released;
    stage.setReleaseHandler([&released](const SceneStage::Release& release) { released.push_back(release); });
    stage.humanJoin(5.0, 1, 0, ScenePose{}, 0);
    CHECK(stage.holds(5.0));
    // Called at once, before anything the scene's end function then does to the human.
    stage.humanRelease(5.0, ScenePose{.position = Vec3{4.0F, 5.0F, 6.0F}, .rotation = Quat{}});
    REQUIRE(released.size() == 1);
    CHECK(released[0].human == 5.0);
    CHECK(near(released[0].feet, Vec3{4.0F, 5.0F, 6.0F}));
    CHECK_FALSE(stage.holds(5.0));
    // A human the scene does not hold is not handed over.
    stage.humanRelease(6.0, std::nullopt);
    CHECK(released.size() == 1);
}

TEST_CASE("a human's join goal switches its brain off at the join and on again at the release", "[scene_stage]") {
    SceneStage stage = quietStage();
    std::vector<std::pair<double, bool>> joins;
    stage.setJoinHandler([&joins](double human, bool joined) { joins.emplace_back(human, joined); });
    stage.humanJoin(5.0, 1, 0, ScenePose{}, 0);
    REQUIRE(joins.size() == 1);
    CHECK(joins[0] == std::pair{5.0, true});
    // Released without being placed (the scene never took him in): the goal's destroy still gives the brain back.
    stage.humanRelease(5.0, std::nullopt);
    REQUIRE(joins.size() == 2);
    CHECK(joins[1] == std::pair{5.0, false});
    // A human the scene does not hold has no join goal to destroy.
    stage.humanRelease(6.0, std::nullopt);
    CHECK(joins.size() == 2);
}

TEST_CASE("a scene soundtrack outlives its cinematic's end and stops only on a skip or a give-up",
          "[scene_stage][audio]") {
    // One made-up stereo stream (class flags 0x04, priority 3, two channels); with no stream files it plays virtually,
    // which is enough to follow its task.
    constexpr std::uint32_t kSoundtrack = 0x600;
    const std::vector<coney::test::TestSound> sounds{
        coney::test::TestSound{.hash = kSoundtrack, .size = 64'000, .soundClass = 0}};
    auto list = coney::audio::SoundTables::parseSoundList(coney::test::soundListChunk(sounds));
    REQUIRE(list.has_value());
    coney::audio::Mixer mixer;
    coney::audio::SoundPlayer player(mixer);
    player.attach(std::make_unique<coney::audio::SoundEngine>(
        mixer, coney::audio::SoundTables(std::move(*list), {coney::test::testClass(0, 0, 0x04, 3, 2)}, {}, {}), nullptr,
        coney::audio::SoundEngine::BankLoader{}, coney::audio::SoundEngine::RandomRange{}));
    coney::audio::SoundEngine& engine = *player.engine();
    SceneStage stage = quietStage();
    stage.setSounds(&player);

    // Prepared as the scene loads, kept until its event, which starts it as the cinematic plays.
    stage.soundtrackPrepare(kSoundtrack);
    const coney::audio::SoundHandle prepared = engine.sceneSound();
    REQUIRE(prepared.valid());
    CHECK(stage.soundtrackReady());
    for (int step = 0; step < 5; ++step) {
        stage.setCinematic(false);
        player.update(1000.0F / 30.0F);
    }
    stage.setCinematic(true);
    stage.soundtrackStart();
    player.update(1000.0F / 30.0F);
    CHECK(engine.isPlaying(prepared));
    // The cinematic's end leaves it playing to its own end (or the next scene's preload).
    stage.setCinematic(false);
    player.update(1000.0F / 30.0F);
    CHECK(engine.isPlaying(prepared));
    // A skip (or a cinematic given up) stops it.
    stage.soundtrackStop();
    CHECK_FALSE(engine.isPlaying(prepared));
}
