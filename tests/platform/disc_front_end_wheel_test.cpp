// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the front end's background (docs/research/frontend.md#background). Headless,
// the start-up path reaches PM_Greet with level100's world loaded as the front-end scene and the front end's scene
// system made over the disc's scene list, as main sets them up; then `level100.lua`'s WonderWheelAnim plays
// `WonderWheel_100` (scene 34) looping through its camera, its 29 objects bound, live and drawn, the wheel turning.
// After an attract movie it plays out its last pass and ends (an open question on frontend.md). It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise. It prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/profile_manager_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "platform/front_end_scene.h"
#include "platform/placed_objects.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "world_objects/spawn_records.h"

namespace {

// Whether a spawn record is one of the Wonder Wheel's parts (its carts, the wheel and its neon signs).
bool wheelPart(const coney::world_objects::SpawnRecord& record) {
    return record.typeName.starts_with("dyn_s_ww") || record.typeName.starts_with("dyn_s_neon");
}

} // namespace

TEST_CASE("the disc's front end plays the Wonder Wheel scene behind the menus", "[disc][frontend][scenes]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // What main sets up: the chunk handlers, the headless renderer, the sheet loader, the scene list.
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(handlers);
    coney::platform::addSpriteSheetHandlers(handlers);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::platform::RenderEngine& renderer = **engine;
    const coney::io::Wad& theWad = *wad;
    auto list = coney::scenes::loadSceneList(theWad);
    REQUIRE(list.has_value());
    if (!list) {
        return;
    }
    const coney::scenes::SceneList& sceneList = *list;
    coney::gui::GlobalStrings strings;
    coney::GameModeStack stack;
    std::vector<std::string> log;
    coney::StartUpFlow flow(
        renderer, stack,
        [&theWad, &handlers](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
            return coney::platform::loadSpriteSheetResource(theWad, handlers, name, false);
        },
        strings, coney::LegalScreenSettings{}, [&log](std::string_view line) { log.emplace_back(line); },
        coney::script::wadScriptSource(theWad));
    coney::platform::FrontEndWorldScene* world = nullptr;
    flow.levelFlow().setSceneLoader(
        [&renderer, &theWad, &flow,
         &world](std::string_view level) -> std::expected<std::unique_ptr<coney::FrontEndScene>, coney::Error> {
            auto scene = coney::platform::FrontEndWorldScene::create(
                renderer, theWad, level, [](std::string_view) {},
                coney::platform::FrontEndObjectSource{&flow.spawnRecords(), &flow.objectTypes()});
            if (!scene) {
                return std::unexpected(std::move(scene.error()));
            }
            world = scene->get();
            return std::unique_ptr<coney::FrontEndScene>(std::move(*scene));
        });
    flow.levelFlow().setScenes(
        [&sceneList, &theWad]() {
            return std::make_unique<coney::scenes::SceneSystem>(sceneList, coney::scenes::wadSceneSource(theWad),
                                                                coney::scenes::SceneSystem::ScriptCall{});
        },
        &flow.context());
    flow.start();
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // PM_Greet is up by frame 160; the scene loaded on the menus' first step and started a few updates later.
    stack.runUntilEmpty(timer, {}, 200);
    REQUIRE(stack.topId() == coney::ProfileManagerMode::kId);
    REQUIRE(world != nullptr);
    coney::scenes::SceneSystem* scenes = flow.levelFlow().scenes();
    REQUIRE(scenes != nullptr);
    if (world == nullptr || scenes == nullptr) {
        return;
    }
    CHECK(scenes->idOf("WonderWheel_100") == std::optional<std::uint32_t>{34});
    CHECK(scenes->state(34) == coney::scenes::SceneState::Playing);
    // The view is the scene camera's: camera01's first pose, (462.60, -122.35, -187.93) in the game's axes.
    CHECK(world->cameraActive());
    const std::optional<coney::platform::WorldView> view = world->view();
    REQUIRE(view.has_value());
    if (view) {
        CHECK(std::abs(view->pose.position.x - 462.60F) < 0.5F);
        CHECK(std::abs(view->pose.position.y - -187.93F) < 0.5F);
        CHECK(std::abs(view->pose.position.z - 122.35F) < 0.5F);
        CHECK(std::abs(view->drawDistance - 150.0F) < 0.01F);
    }
    // The 29 objects: live and pinned (SceneAddObject), each placed with its model and posed by the scene.
    const auto partsWhere = [&flow](auto&& test) {
        return std::ranges::count_if(flow.spawnRecords().all(), [&test](const coney::world_objects::SpawnRecord& r) {
            return wheelPart(r) && test(r);
        });
    };
    CHECK(partsWhere([](const auto& r) { return r.live && r.pinned; }) == 29);
    CHECK(partsWhere([world](const auto& r) { return world->objectPoseOf(r.handle).has_value(); }) == 29);
    REQUIRE(world->objects() != nullptr);
    CHECK(world->objects()->placed() == 29);
    // Track events 24 and 25 (messages 0x12 and 0x13) show and hide some of them as the scene plays.
    const std::size_t drawn = world->objects()->drawable();
    CHECK(drawn > 20);

    // The wheel turns and its rim carts ride round: two seconds on, their poses have changed.
    const auto handleOf = [&flow](std::string_view type) {
        const auto& all = flow.spawnRecords().all();
        const auto found = std::ranges::find(all, type, &coney::world_objects::SpawnRecord::typeName);
        return found != all.end() ? found->handle : 0.0;
    };
    const double wheel = handleOf("dyn_s_wwheel_a");
    const double cart = handleOf("dyn_s_wwcart_simple_a");
    const std::optional<coney::scenes::ScenePose> wheelBefore = world->objectPoseOf(wheel);
    const std::optional<coney::scenes::ScenePose> cartBefore = world->objectPoseOf(cart);
    stack.runUntilEmpty(timer, {}, 60);
    const std::optional<coney::scenes::ScenePose> wheelAfter = world->objectPoseOf(wheel);
    const std::optional<coney::scenes::ScenePose> cartAfter = world->objectPoseOf(cart);
    REQUIRE((wheelBefore && wheelAfter && cartBefore && cartAfter));
    if (wheelBefore && wheelAfter && cartBefore && cartAfter) {
        const float turned = std::abs(coney::anim::dot(wheelBefore->rotation, wheelAfter->rotation));
        CHECK(turned < 0.99999F);
        // The hub stays; the cart moves along the rim, about 21.5 m from the hub.
        CHECK(std::abs(wheelAfter->position.x - 515.51F) < 0.1F);
        CHECK(coney::anim::length(coney::anim::subtract(cartAfter->position, cartBefore->position)) > 0.1F);
        CHECK(std::abs(coney::anim::length(coney::anim::subtract(cartAfter->position, wheelAfter->position)) - 21.5F) <
              1.5F);
    }

    // Idle on PM_Greet: the attract movie (L1_IN, the second after the start-up's) comes round after about 70 s.
    // Menu.playMoviePostFade's stopScene (SceneStop, not forced) ends the looping; Menu.movieFinished's startScene asks
    // for the scene again, but ScenePreload of a scene that is still playing only adds a user and calls nothing back
    // (docs/research/scenes.md#loading), so the scene plays out its last 20 s pass and ends, its camera with it
    // (docs/research/frontend.md#coneys-implementation, an open question).
    const float passSeconds = scenes->length(34);
    std::uint64_t idle = 0;
    while (std::ranges::count(flow.services().movies(), std::string("L1_IN")) < 2 && idle < 2400) {
        stack.runUntilEmpty(timer, {}, 1);
        ++idle;
    }
    CHECK(std::ranges::count(flow.services().movies(), std::string("L1_IN")) == 2);
    std::uint64_t after = 0;
    while (flow.levelFlow().scenes() != nullptr && flow.levelFlow().scenes()->playing() && after < 1000) {
        stack.runUntilEmpty(timer, {}, 1);
        ++after;
    }
    CHECK(after > 0);
    CHECK(static_cast<float>(after) <= passSeconds * 30.0F);
    // A second on, the scene has ended and the script's camera (black) is current again.
    stack.runUntilEmpty(timer, {}, 30);
    CHECK(flow.levelFlow().scenes()->stats().ended == 1);
    CHECK_FALSE(world->cameraActive());
    CHECK(flow.scripts().errors() == 0);
    for (const std::string& line : log) {
        UNSCOPED_INFO(line);
    }
    std::printf("  front end: scene 34 played; %zu of 29 objects placed, %zu drawn; a %.0f s pass; the attract movie "
                "after %llu frames, the scene ending %llu frames later; %llu script errors\n",
                world->objects()->placed(), drawn, static_cast<double>(passSeconds),
                static_cast<unsigned long long>(idle), static_cast<unsigned long long>(after),
                static_cast<unsigned long long>(flow.scripts().errors()));
}
