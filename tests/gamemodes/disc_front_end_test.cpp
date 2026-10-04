// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the start-up path from the legal screen to the main menu, headless, with the
// disc's sprite sheets and UI strings and a scripted pad that presses START on PM_Greet. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise. It prints counts and states only (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

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
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "scripting/config_strings.h"

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

    // What main sets up: the chunk handlers, the headless renderer, the UI strings and the sheet loader.
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(handlers);
    coney::platform::addSpriteSheetHandlers(handlers);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::gui::GlobalStrings strings;
    REQUIRE(coney::script::loadGlobalStrings(coney::script::wadScriptSource(*wad), coney::Language::English, strings));
    const coney::io::Wad& theWad = *wad;
    const coney::ProfileManagerMode::SheetLoader loader =
        [&theWad, &handlers](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
        return coney::platform::loadSpriteSheetResource(theWad, handlers, name, false);
    };

    // START on frame 200, well after PM_Greet is up (frame 152).
    auto script = coney::parseInputScript("200 tap start\n");
    REQUIRE(script.has_value());
    coney::ScriptedInput input(std::move(*script));
    coney::GameModeStack stack;
    stack.setInput(&input);
    std::vector<std::string> log;
    coney::StartUpFlow flow(**engine, stack, loader, strings, coney::LegalScreenSettings{},
                            [&log](std::string_view line) { log.emplace_back(line); });
    flow.start();
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // The main loop's frame (pads, timer, step), the index carrying on across the two runs below.
    std::uint64_t index = 0;
    const auto run = [&](std::uint64_t count) {
        for (const std::uint64_t end = index + count; index < end; ++index) {
            stack.samplePads(index);
            const std::uint64_t advanced = timer.update();
            stack.step(coney::FrameTime{index, coney::GameTimer::toSeconds(advanced), timer.ticks(), advanced});
        }
    };
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
    run(41);
    CHECK(menus.controller().currentName() == "PM_Mode");
    CHECK(menus.controller().mode().grid().items() == 3);
    CHECK(flow.services().cues() == std::vector<int>{9});
    std::printf("  start-up: %zu movies skipped, music %s, %zu log lines; screen %s with %zu items\n",
                flow.services().movies().size(), flow.services().music().c_str(), log.size(),
                std::string(menus.controller().currentName()).c_str(), menus.controller().mode().grid().items());
}
