// SPDX-License-Identifier: GPL-3.0-or-later
// The pause (mode 0xa) and the mission-failed mode (0xc) on the game-mode stack over gameplay, driven by an input
// script with START (docs/research/pause.md).
#include "gamemodes/pause_mode.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/mission_failed_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/pause_menu.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"
#include "warriors/level_table.h"

using coney::GameModeStack;
using coney::GameplayMode;
using coney::GameTimer;
using coney::MissionFailedMode;
using coney::PauseMode;
using coney::StartUpFlow;

namespace {

// The start-up flow's modes with gameplay pushed alone in `level99` (checkpoint 3), an input script on port 1, and
// the pause hooks recorded.
struct Run {
    coney::test::RecordingDevice device;
    GameModeStack stack;
    coney::gui::GlobalStrings strings;
    std::vector<std::string> log;
    std::vector<bool> soundPauses;
    int radarsOff = 0;
    std::unique_ptr<StartUpFlow> flow;
    std::unique_ptr<coney::ScriptedInput> input;
    GameTimer timer;

    explicit Run(std::string_view script) {
        input = std::make_unique<coney::ScriptedInput>(coney::parseInputScript(script).value());
        stack.setInput(input.get());
        timer.setFixedStep(true);
        const auto sheets = [](std::string_view /*name*/) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
            return coney::test::testFontSheet();
        };
        flow = std::make_unique<StartUpFlow>(
            device, stack, sheets, strings, coney::LegalScreenSettings{},
            [this](std::string_view line) { log.emplace_back(line); }, coney::script::ScriptSource{},
            GameplayMode::LevelLoader{}, std::nullopt, 0);
        flow->setSheetRecordLoader(
            [](std::uint32_t /*record*/) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
                return coney::test::testFontSheet();
            });
        flow->setPauseHooks(coney::PauseHooks{
            .pauseSound = [this](bool paused) { soundPauses.push_back(paused); },
            .objectives = {},
            .radarsOff = [this] { ++radarsOff; },
        });
        coney::GameState& state = flow->state();
        coney::LevelRecord front;
        front.id = 0;
        front.name = "level100";
        front.number = 100;
        state.levels.set(front);
        coney::LevelRecord level;
        level.id = 1;
        level.name = "level99";
        level.number = 99;
        state.levels.set(level);
        state.currentLevel = 1;
        state.checkPoint = 3;
        flow->gameplay().setLevel("level99");
        stack.push(flow->gameplay());
    }

    // Runs `count` frames in test mode.
    void frames(std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); }
};

} // namespace

TEST_CASE("pause: START pushes mode 0xa, pauses the sound and loads the pause bank", "[pause]") {
    Run run("5 tap start\n");
    run.frames(5);
    CHECK(run.stack.topId() == GameplayMode::kId);
    run.frames(2);
    REQUIRE(run.stack.topId() == PauseMode::kId);
    CHECK(run.flow->pause().pauses() == 1);
    CHECK(run.flow->pause().player() == 0);
    CHECK(run.soundPauses == std::vector<bool>{true});
    CHECK(run.radarsOff == 1);
    CHECK(run.flow->services().bank() == PauseMode::kSoundBank);
    CHECK(run.flow->pause().menu().grid().items() == 7);
    // The paused frame draws the world (black: no level) with the tint and the menu over it.
    CHECK(run.device.calls.back() == "present");
}

TEST_CASE("pause: triangle resumes after the fade and hold, and START is ignored for one frame after", "[pause]") {
    Run run("5 tap start\n20 tap triangle\n");
    run.frames(22);
    REQUIRE(run.flow->pause().menu().closing());
    // 1.5 s of fade and 0.5 s of hold: 60 frames.
    run.frames(58);
    CHECK(run.stack.topId() == PauseMode::kId);
    run.frames(6);
    REQUIRE(run.stack.topId() == GameplayMode::kId);
    CHECK(run.soundPauses == std::vector<bool>{true, false});
    CHECK(run.flow->pause().cooldown() == 0);
}

TEST_CASE("pause: START closes the menu only after 1.5 s", "[pause]") {
    Run run("5 tap start\n20 tap start\n70 tap start\n");
    run.frames(30);
    CHECK_FALSE(run.flow->pause().menu().closing());
    run.frames(42);
    CHECK(run.flow->pause().menu().closing());
}

TEST_CASE("pause: Quit to the main menu leaves gameplay with checkpoint 1 and the level `menu` asked for", "[pause]") {
    // Seven items: Quit is the last; right six times from Objectives, then cross, left to Yes, cross.
    Run run("5 tap start\n15 tap right\n20 tap right\n25 tap right\n30 tap right\n35 tap right\n40 tap right\n"
            "45 tap cross\n55 tap left\n60 tap cross\n");
    run.frames(62);
    REQUIRE(run.flow->pause().menu().outcome() == coney::gui::PauseOutcome::QuitToMainMenu);
    run.frames(64);
    CHECK(run.stack.empty());
    CHECK(run.flow->state().checkPoint == 1);
    CHECK(run.flow->levelFlow().levelRequests().back() == "menu");
    CHECK(run.flow->levelFlow().chosenLevel() == coney::LevelFlowMode::kNoLevel);
}

TEST_CASE("pause: Restart Last Check Point reloads the level at the same checkpoint", "[pause]") {
    // Restart is item 4: right four times, cross (the checkpoint choice is selected), cross, left to Yes, cross.
    Run run("5 tap start\n15 tap right\n20 tap right\n25 tap right\n30 tap right\n35 tap cross\n45 tap cross\n"
            "55 tap left\n60 tap cross\n");
    run.frames(62);
    REQUIRE(run.flow->pause().menu().outcome() == coney::gui::PauseOutcome::RestartCheckpoint);
    run.frames(64);
    CHECK(run.stack.empty());
    CHECK(run.flow->state().checkPoint == 3);
    CHECK(run.flow->levelFlow().chosenLevel() == 1);
}

TEST_CASE("mission failed: the launch pushes mode 0xc, and Restart level reloads from checkpoint 1", "[pause]") {
    // Down to the second row (Restart level), cross.
    Run run("15 tap down\n25 tap cross\n");
    run.frames(2);
    run.flow->launchMissionFailed("Busted");
    run.frames(1);
    REQUIRE(run.stack.topId() == MissionFailedMode::kId);
    CHECK(run.flow->missionFailed().reason() == "Busted");
    CHECK(run.soundPauses == std::vector<bool>{true});
    run.frames(30);
    CHECK(run.stack.empty());
    CHECK(run.flow->state().checkPoint == 1);
    CHECK(run.flow->levelFlow().chosenLevel() == 1);
    CHECK(run.soundPauses == std::vector<bool>{true, false});
}
