// SPDX-License-Identifier: GPL-3.0-or-later
// The start-up path from the legal screen to the main menu (modes 5, 6, 8 and 0x12), run on the game-mode stack with
// synthetic sheets and a scripted pad (docs/research/frontend.md#mode-flow).
#include "gamemodes/start_up_flow.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
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
