// SPDX-License-Identifier: GPL-3.0-or-later
// The start-up path from the legal screen to the main menu (modes 5, 6, 8 and 0x12), run on the game-mode stack with
// synthetic sheets and a scripted pad (docs/research/frontend.md#mode-flow).
#include "gamemodes/start_up_flow.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/memory_card_mode.h"
#include "gamemodes/profile_manager_mode.h"
#include "gui/global_strings.h"
#include "support/font_fixtures.h"
#include "support/lua_fixtures.h"
#include "support/recording_device.h"

using coney::GameModeStack;
using coney::GameTimer;
using coney::LevelFlowMode;
using coney::MemoryCardMode;
using coney::ProfileManagerMode;
using coney::StartUpFlow;
using coney::graphics::SpriteSheet;

namespace {

// Every sheet is the synthetic font sheet (a font, so the text sheets load too); names asked for are recorded.
struct Sheets {
    std::vector<std::string> requested;
    ProfileManagerMode::SheetLoader loader() {
        return [this](std::string_view name) -> std::expected<SpriteSheet, coney::Error> {
            requested.emplace_back(name);
            return coney::test::testFontSheet();
        };
    }
};

// A stack running the start-up flow with an input script, a recording device and a log.
struct Run {
    coney::test::RecordingDevice device;
    GameModeStack stack;
    Sheets sheets;
    coney::gui::GlobalStrings strings;
    std::vector<std::string> log;
    std::unique_ptr<StartUpFlow> flow;
    std::unique_ptr<coney::ScriptedInput> input;
    GameTimer timer;

    explicit Run(std::string_view script) {
        strings.set(0x76, "PRESS START");
        input = std::make_unique<coney::ScriptedInput>(coney::parseInputScript(script).value());
        stack.setInput(input.get());
        timer.setFixedStep(true);
        flow = std::make_unique<StartUpFlow>(device, stack, sheets.loader(), strings, coney::LegalScreenSettings{},
                                             [this](std::string_view line) { log.emplace_back(line); });
        flow->start();
    }

    // Runs `count` more frames as the main loop does (pads, timer, step), the frame index carrying on from the last.
    void frames(std::uint64_t count) {
        for (std::uint64_t i = 0; i < count; ++i, ++index) {
            stack.samplePads(index);
            const std::uint64_t advanced = timer.update();
            stack.step(coney::FrameTime{index, GameTimer::toSeconds(advanced), timer.ticks(), advanced});
        }
    }

    std::uint64_t index = 0; ///< The next frame's index.
};

} // namespace

TEST_CASE("start-up: movies skipped, then modes 8, 6 and 5 pushed with the boot check asked", "[start_up]") {
    Run run("");
    CHECK(run.flow->services().movies() == std::vector<std::string>{"LOGO", "PLOGO", "L1_IN"});
    CHECK(run.stack.size() == 3);
    CHECK(run.stack.topId() == coney::LegalScreenMode::kId);
    CHECK(run.flow->memoryCard().bootCheck() == MemoryCardMode::BootCheck::Pending);
}

TEST_CASE("start-up: legal screen, memory card, level flow, then the profile manager at PM_Greet", "[start_up]") {
    Run run("");
    // The legal screen holds 150 frames (frames 0-149).
    run.frames(150);
    CHECK(run.stack.topId() == MemoryCardMode::kId);
    CHECK(run.flow->levelFlow().entered() == false);

    // Frame 150: the memory-card check passes through; its exit finds the level flow below.
    run.frames(1);
    CHECK(run.stack.topId() == LevelFlowMode::kId);
    CHECK(run.flow->memoryCard().bootCheck() == MemoryCardMode::BootCheck::Done);
    CHECK_FALSE(run.flow->levelFlow().loadFrontEndOnResume());

    // Frame 151: the level flow enters, starts the front end and shows the profile manager.
    run.frames(1);
    LevelFlowMode& levelFlow = run.flow->levelFlow();
    CHECK(levelFlow.frontEndLoaded());
    CHECK(levelFlow.currentLevel() == "level100");
    CHECK(levelFlow.chosenLevel() == LevelFlowMode::kNoLevel);
    CHECK(run.flow->services().music() == "menu");
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.flow->profileManager().onRumble() == "Menu.fadeToRMI");
    CHECK(run.flow->profileManager().onStartGame() == "Menu.startGame");

    // Frame 152: the profile manager enters at PM_Greet, loading the menu sheet and the two fonts.
    run.frames(1);
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Greet");
    CHECK(run.sheets.requested == std::vector<std::string>{"legal_screen", "menu_system", "part_page0", "big_font"});
    CHECK(run.stack.size() == 2);
    // Many more frames: PM_Greet waits for START.
    run.frames(300);
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Greet");
    CHECK(run.flow->services().cues().empty());
}

TEST_CASE("start-up: START on PM_Greet reaches the main menu, PM_Mode", "[start_up]") {
    Run run("200 tap start\n");
    run.frames(201);
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Mode");
    CHECK(run.flow->services().cues() == std::vector<int>{9});
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    // Every frame of the front end was begun on black and presented.
    CHECK(run.device.clears.back() == coney::graphics::kBlack);
    CHECK(run.device.calls.back() == "present");
}

TEST_CASE("profile manager: show() pushes the mode only when it is not on top", "[start_up]") {
    Run run("");
    run.frames(152);
    REQUIRE(run.stack.topId() == ProfileManagerMode::kId);
    const std::size_t size = run.stack.size();
    run.flow->profileManager().show(run.stack, "a", "b");
    CHECK(run.stack.size() == size);
    CHECK(run.flow->profileManager().onStartGame() == "b");
}

namespace {

// Synthetic scripts for the front end, written like the game's (Lua 4.0 bytecode built by hand, nothing from the
// disc): the preload fills a two-record level table, level100.lua defines Menu.onStart (show the menus) and
// Menu.startGame (ask for level 1).
std::map<std::string, std::vector<std::byte>, std::less<>> frontEndScripts() {
    using coney::test::LuaAsm;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    // enum_preload.lua: Preloaded = 1
    LuaAsm enumPreload;
    enumPreload.pushInt(1).setGlobal("Preloaded");
    files["enum_preload.lua"] = coney::test::luaChunk(enumPreload.end());
    // config_preload3.lua: CfgLevelName(0, "level100", "", "level100", "", 100), the same for "level1" at index 1.
    LuaAsm levels;
    for (const auto& [index, name] : {std::pair{0, "level100"}, std::pair{1, "level1"}}) {
        levels.getGlobal("CfgLevelName").pushInt(index).pushString(name).pushString("").pushString(name);
        levels.pushString("").pushInt(index == 0 ? 100 : 1).call(6);
    }
    files["config_preload3.lua"] = coney::test::luaChunk(levels.end());
    for (const char* empty : {"config_preload.lua", "config_preload2.lua", "global.lua"}) {
        files[empty] = coney::test::luaChunk(LuaAsm().end());
    }
    // level100.lua: Menu = {}; Menu.onStart = function() ShowProfileManager("Menu.fadeToRMI", "Menu.startGame") end;
    // Menu.startGame = function() MenuLoadLevel("level1") end
    LuaAsm onStart;
    onStart.getGlobal("ShowProfileManager").pushString("Menu.fadeToRMI").pushString("Menu.startGame").call(2);
    LuaAsm startGame;
    startGame.getGlobal("MenuLoadLevel").pushString("level1").call(1);
    LuaAsm level;
    level.spec.protos = {onStart.end(), startGame.end()};
    level.newTable().setGlobal("Menu");
    level.getGlobal("Menu").pushString("onStart").closure(0).setTable();
    level.getGlobal("Menu").pushString("startGame").closure(1).setTable();
    files["level100.lua"] = coney::test::luaChunk(level.end());
    return files;
}

// A run of the start-up flow with the synthetic scripts.
struct ScriptedRun {
    coney::test::RecordingDevice device;
    GameModeStack stack;
    Sheets sheets;
    coney::gui::GlobalStrings strings;
    std::vector<std::string> log;
    std::map<std::string, std::vector<std::byte>, std::less<>> files = frontEndScripts();
    std::unique_ptr<StartUpFlow> flow;
    std::unique_ptr<coney::ScriptedInput> input;
    GameTimer timer;
    std::uint64_t index = 0;

    explicit ScriptedRun(std::string_view script) {
        input = std::make_unique<coney::ScriptedInput>(coney::parseInputScript(script).value());
        stack.setInput(input.get());
        timer.setFixedStep(true);
        flow = std::make_unique<StartUpFlow>(
            device, stack, sheets.loader(), strings, coney::LegalScreenSettings{},
            [this](std::string_view line) { log.emplace_back(line); },
            [this](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
                const auto found = files.find(name);
                if (found == files.end()) {
                    return coney::fail(coney::ErrorCode::NotFound, "no such script");
                }
                return found->second;
            });
        flow->start();
    }

    // Runs `count` more frames as the main loop does.
    void frames(std::uint64_t count) {
        for (std::uint64_t i = 0; i < count; ++i, ++index) {
            stack.samplePads(index);
            const std::uint64_t advanced = timer.update();
            stack.step(coney::FrameTime{index, GameTimer::toSeconds(advanced), timer.ticks(), advanced});
        }
    }

    // Whether a log line contains `text`.
    [[nodiscard]] bool logged(std::string_view text) const {
        return std::ranges::any_of(log, [text](const std::string& line) { return line.contains(text); });
    }
};

} // namespace

TEST_CASE("start-up with scripts: preloads at the legal screen, Menu.onStart shows the menus", "[start_up]") {
    ScriptedRun run("");
    CHECK(run.flow->scripts().generation() == 1);
    // Frame 0: the legal screen's entry ran the preloads in the one state.
    run.frames(1);
    CHECK(run.flow->scripts().vm().global("Preloaded").number() == 1.0);
    CHECK(run.flow->state().levels.count() == 2);
    // Frame 151: the level flow runs global.lua and level100.lua; Menu.onStart's ShowProfileManager pushes the menus.
    run.frames(151);
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.flow->levelFlow().currentLevel() == "level100");
    CHECK(run.flow->profileManager().onStartGame() == "Menu.startGame");
    CHECK_FALSE(run.logged("did not show the menus"));
    CHECK(run.flow->scripts().errors() == 0);
}

TEST_CASE("start-up with scripts: story reaches Menu.startGame, the level request, and back to the menus",
          "[start_up]") {
    // START, then cross on story (PM_Profile), then cross on the stand-in, which ends the profile manager.
    ScriptedRun run("200 tap start\n215 tap cross\n235 tap cross\n");
    run.frames(240);
    CHECK(run.flow->levelFlow().levelRequests() == std::vector<std::string>{"level1"});
    CHECK(run.logged("level start requested: level1"));
    // The front-end level was unloaded (a fresh Lua state) and started again: the menus are back at PM_Greet.
    CHECK(run.flow->scripts().generation() == 2);
    run.frames(2);
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Greet");
    CHECK(run.flow->scripts().errors() == 0);
}
