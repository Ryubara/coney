// SPDX-License-Identifier: GPL-3.0-or-later
// The lighting rig's side of the lighting lab (docs/guides/realtime-lighting.md#lab-built) on librw's NULL device, with
// no window or GPU: the lighting frame's selection rows as the rig draws a synthetic human and object, and Light from,
// which moves the lights' cull to the game camera but leaves the rig's own camera the one drawn through.
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "characters/character_model.h"
#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/lighting_switches.h"
#include "platform/character_mesh.h"
#include "platform/perf_probes.h"
#include "platform/render_engine.h"
#include "platform/scene_lighting.h"
#include "support/character_fixtures.h"
#include "world/view_frustum.h"

namespace {

// The synthetic character the fixtures build.
coney::characters::CharacterModel fixtureModel() {
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    return std::move(*model);
}

} // namespace

TEST_CASE("the lighting frame keeps one row per human, a hidden human's without point lights", "[lighting_lab]") {
    // Expected: lighting.md#select, confirmed (code): HumanRender_Draw selects once per human (0x001746f0), with point
    // lights unless he is hidden; a world object always looks for them
    using coney::platform::DrawKind;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const coney::characters::CharacterModel model = fixtureModel();
    {
        coney::platform::CharacterMesh mesh(model, nullptr);
        coney::graphics::LevelLighting level;
        coney::platform::SceneLighting scene(level, std::nullopt, std::nullopt);
        scene.beginFrame(coney::world::CameraPose{}, 0.1F, 100.0F, 0);
        {
            // A hidden human in two passes, his blood layer marked as the play mode marks it.
            const coney::platform::DrawKindScope kind(DrawKind::Humans);
            scene.noteHiddenHuman(true);
            scene.drawHumanAtomic(mesh.atomic(), true);
            scene.setMeshLayer(mesh.bloodAtomic());
            scene.drawHumanAtomic(mesh.bloodAtomic(), true);
            scene.setMeshLayer(nullptr);
        }
        {
            const coney::platform::DrawKindScope kind(DrawKind::Objects);
            scene.drawObjectAtomic(mesh.atomic());
        }
        const auto& rows = scene.lightingFrame().selections;
        REQUIRE(rows.size() == 2);
        CHECK(rows[0].caller == coney::graphics::SelectionCaller::Human);
        CHECK(rows[0].human);
        CHECK_FALSE(rows[0].pointLights);
        CHECK(rows[1].caller == coney::graphics::SelectionCaller::Object);
        CHECK(rows[1].pointLights);
    }
}

TEST_CASE("Light from culls the lights from the game camera and keeps the rig's camera the one drawn through",
          "[lighting_lab]") {
    // Expected: unverified, #1250 (Coney's own debug tooling: Light from, docs/guides/realtime-lighting.md#lab-built)
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::graphics::LevelLighting level;
    coney::platform::SceneLighting scene(level, std::nullopt, std::nullopt);
    coney::graphics::LightingSwitches switches;
    scene.setSwitches(&switches);
    const coney::graphics::LightView game{.position = {50.0F, 2.0F, 0.0F}, .forward = {0.0F, 0.0F, 1.0F}};
    scene.setGameView(game);
    coney::world::CameraPose drawn;
    drawn.position = {0.0F, 30.0F, 0.0F};
    scene.beginFrame(drawn, 0.1F, 100.0F, 0);
    // The lights are culled from the game camera; the rig's camera, which the caps, the coronas and the fades read,
    // is the one drawn through.
    CHECK(scene.lightingFrame().view.position.x == 50.0F);
    CHECK(scene.pose().position.x == 0.0F);
    CHECK(scene.pose().position.y == 30.0F);
    // Off, the lights follow the camera drawn through.
    switches.lightFromGameCamera = false;
    scene.beginFrame(drawn, 0.1F, 100.0F, 0);
    CHECK(scene.lightingFrame().view.position.y == 30.0F);
}
