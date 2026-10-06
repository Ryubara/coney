// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the start-up path from the legal screen to the main menu, headless, with the
// disc's sprite sheets and the game's own scripts (the preloads, global.lua, level100.lua and the Menu callbacks), and
// a scripted pad: START on PM_Greet, then quick rumble (chosen with the analog stick), then story through to the level
// request and back to the menus. It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise. It prints counts and states only (LEGAL.md).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/language.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/profile_manager_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "gui/profile_management_gui/pm_new_game_screens.h"
#include "platform/placed_objects.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "scripting/config_strings.h"
#include "world_objects/object_list.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

TEST_CASE("the disc's start-up path reaches PM_Greet, and START the main menu", "[disc][frontend]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    if (!wad) {
        return;
    }

    // What main sets up: the chunk handlers, the headless renderer, the UI strings (filled by the preloads) and the
    // sheet loader.
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(handlers);
    coney::platform::addSpriteSheetHandlers(handlers);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::gui::GlobalStrings strings;
    const coney::io::Wad& theWad = *wad;
    const coney::ProfileManagerMode::SheetLoader loader =
        [&theWad, &handlers](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
        return coney::platform::loadSpriteSheetResource(theWad, handlers, name, false);
    };

    // START on frame 200, well after PM_Greet is up (frame 152) and Menu.onStart's 1.5 s fade in is over. Then the
    // stick pushed up most of the way (wrapping to quick rumble) and cross; triangle backs out of the Rumble menu's
    // first screen; once the menu has faded back in, the stick down a little past half (wrapping to story), cross, and
    // a new profile: cross on CREATE NEW PROFILE, one character, the stick right eight times and up to OK, cross, and
    // cross on the defaults of PM_Difficulty, PM_Light and PM_Subtitles.
    auto script = coney::parseInputScript("200 tap start\n"
                                          "212 stick left 0 70\n"
                                          "214 stick left 0 0\n"
                                          "225 tap cross\n"
                                          "265 tap triangle\n"
                                          "320 stick left 0 -60\n"
                                          "322 stick left 0 0\n"
                                          "335 tap cross\n"
                                          "355 tap cross\n"
                                          "365 tap cross\n"
                                          "370 stick left 70 0\n"
                                          "372 stick left 0 0\n"
                                          "376 stick left 70 0\n"
                                          "378 stick left 0 0\n"
                                          "382 stick left 70 0\n"
                                          "384 stick left 0 0\n"
                                          "388 stick left 70 0\n"
                                          "390 stick left 0 0\n"
                                          "394 stick left 70 0\n"
                                          "396 stick left 0 0\n"
                                          "400 stick left 70 0\n"
                                          "402 stick left 0 0\n"
                                          "406 stick left 70 0\n"
                                          "408 stick left 0 0\n"
                                          "412 stick left 70 0\n"
                                          "414 stick left 0 0\n"
                                          "420 stick left 0 70\n"
                                          "422 stick left 0 0\n"
                                          "428 tap cross\n"
                                          "438 tap cross\n"
                                          "448 tap cross\n"
                                          "458 tap cross\n");
    REQUIRE(script.has_value());
    coney::ScriptedInput input(std::move(*script));
    coney::GameModeStack stack;
    stack.setInput(&input);
    std::vector<std::string> log;
    coney::StartUpFlow flow(
        **engine, stack, loader, strings, coney::LegalScreenSettings{},
        [&log](std::string_view line) { log.emplace_back(line); }, coney::script::wadScriptSource(*wad));
    flow.start();
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // The main loop in test mode, the frame index carrying on across the two runs below.
    const auto run = [&](std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); };

    run(160);
    const coney::ProfileManagerMode& menus = flow.profileManager();
    CHECK(stack.topId() == coney::ProfileManagerMode::kId);
    CHECK(menus.controller().currentName() == "PM_Greet");
    // Every sheet loaded: the legal screen and the profile manager log a line for each one that fails.
    for (const std::string& line : log) {
        INFO(line);
        CHECK(!line.starts_with("legal screen:"));
        for (const std::string_view sheet : {"menu_system", "part_page0", "big_font"}) {
            CHECK(line.find(std::string("profile manager: ") + std::string(sheet)) == std::string::npos);
        }
    }
    // level100.lua's 29 Wonder Wheel objects are spawn records, each of a type CfgObj configured
    // (docs/research/objects.md#wonder-wheel): eight carts of each row, the wheel and four neons.
    std::size_t wheelParts = 0;
    for (const coney::world_objects::SpawnRecord& record : flow.spawnRecords().all()) {
        if (record.typeName.starts_with("dyn_s_ww") || record.typeName.starts_with("dyn_s_neon")) {
            ++wheelParts;
            CHECK(flow.objectTypes().find(record.typeName) != nullptr);
        }
        if (record.typeName == "dyn_s_wwheel_a") {
            CHECK(record.tint == 0x474542FFU);
        }
    }
    CHECK(wheelParts == 29);
    // Each of them has a model the front-end scene can draw: the Object List record of its type's model hash, one
    // atomic over its dictionary (docs/research/objects.md#models).
    auto objectList = coney::world_objects::loadObjectList(*wad);
    REQUIRE(objectList.has_value());
    if (objectList) {
        coney::platform::PlacedObjects placed(
            *wad, *objectList, [](std::string_view line) { UNSCOPED_INFO(line); }, false);
        for (const coney::world_objects::SpawnRecord& record : flow.spawnRecords().all()) {
            const coney::world_objects::ObjectType* type = flow.objectTypes().find(record.typeName);
            if (type != nullptr &&
                (record.typeName.starts_with("dyn_s_ww") || record.typeName.starts_with("dyn_s_neon"))) {
                placed.place(record.handle, type->modelHash, coney::anim::Vec3{}, coney::anim::Quat{});
            }
        }
        CHECK(placed.placed() == 29);
        CHECK(placed.drawable() == 29);
    }
    run(41);
    CHECK(menus.controller().currentName() == "PM_Mode");
    CHECK(menus.controller().mode().grid().items() == 3);
    CHECK(flow.services().cues() == std::vector<int>{9});
    const std::size_t items = menus.controller().mode().grid().items();

    // Whether a log line contains `text`.
    const auto logged = [&log](std::string_view text) {
        return std::ranges::any_of(log, [text](const std::string& line) { return line.contains(text); });
    };
    // Quick rumble: Menu.fadeToRMI fades out, Menu.launchRMI opens the Rumble menu (mode 0x11), triangle backs out and
    // Menu.cancelRumbleMode fades back in (the Rumble menu fades in over 0.7 s and out over 0.7 s around it).
    run(115);
    CHECK(logged("script: Menu.fadeToRMI"));
    CHECK(logged("rumble menu: Game Mode"));
    CHECK(logged("rumble menu: cancelled"));
    CHECK(flow.rumbleMenu().cancelled());
    CHECK(menus.controller().currentName() == "PM_Mode");
    CHECK(flow.fade().level() == 0.0F);
    // Story with a new profile: the screens, the fade out, the profile created; Menu.startGame asks for a level; the
    // front end comes back in a fresh Lua state.
    run(200);
    for (const std::string_view screen : {"PM_Profile", "PM_Create", "PM_Difficulty", "PM_Light", "PM_Subtitles"}) {
        CHECK(logged(std::string("profile manager: ") + std::string(screen)));
    }
    CHECK(flow.profiles().count() == 1);
    CHECK(flow.state().profileDifficulty == 1.0);
    CHECK(flow.state().brightness == 40);
    CHECK_FALSE(flow.state().subtitles);
    const coney::gui::NameKeyboard& keys = menus.controller().create().keyboard();
    CHECK(keys.rows() == std::vector<std::size_t>{12, 12, 12, 11});
    CHECK(logged("script: Menu.startGame"));
    CHECK(logged("level start requested"));
    // Menu.startGame asks once, and the mission-complete mode's UnlockAndLoad asks again (runNextMission(1) twice).
    CHECK(flow.levelFlow().levelRequests().size() == 2);
    CHECK(flow.missionComplete().launches() == 2);
    CHECK(flow.scripts().generation() == 2);
    CHECK(stack.topId() == coney::ProfileManagerMode::kId);
    CHECK(menus.controller().currentName() == "PM_Greet");
    // No script failed and no binding was missing, in either Lua state.
    for (const std::string& line : log) {
        UNSCOPED_INFO(line);
    }
    CHECK(flow.scripts().errors() == 0);
    CHECK(flow.scripts().skippedCalls() == 0);
    CHECK(!logged("did not show the menus"));
    std::printf("  start-up: %zu movies skipped, %zu log lines; PM_Mode with %zu items; %zu level records; "
                "%llu script errors, %llu skipped calls, %llu Lua states; %zu level requests; screen %s; %zu keys\n",
                flow.services().movies().size(), log.size(), items, flow.state().levels.count(),
                static_cast<unsigned long long>(flow.scripts().errors()),
                static_cast<unsigned long long>(flow.scripts().skippedCalls()),
                static_cast<unsigned long long>(flow.scripts().generation()), flow.levelFlow().levelRequests().size(),
                std::string(menus.controller().currentName()).c_str(), keys.cells().size());
}
