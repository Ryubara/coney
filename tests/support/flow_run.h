// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The start-up flow's modes with gameplay pushed alone in a synthetic `level99`, driven by an input script: the
// harness the pause, mission-failed, Rumble result and Armies game-over tests share, and network play's tests use as
// a scripted host. Nothing here comes from the game (LEGAL.md, "No game data").

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/pause_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"
#include "warriors/level_table.h"

namespace coney::test {

/// The start-up flow's modes with gameplay pushed alone in `level99` (level id 1, number 99, checkpoint 3; the front
/// end `level100` is id 0), an input script on port 1, and the pause hooks recorded.
struct FlowRun {
    RecordingDevice device;
    GameModeStack stack;
    gui::GlobalStrings strings;
    std::vector<std::string> log;
    std::vector<bool> soundPauses;
    int radarsOff = 0;
    int radarsBack = 0;
    std::unique_ptr<StartUpFlow> flow;
    std::unique_ptr<ScriptedInput> input;
    GameTimer timer;

    /// Builds the flow and pushes gameplay; `loader` replaces gameplay's level load (empty: the default).
    explicit FlowRun(std::string_view script, GameplayMode::LevelLoader loader = {}) {
        input = std::make_unique<ScriptedInput>(parseInputScript(script).value());
        stack.setInput(input.get());
        timer.setFixedStep(true);
        const auto sheets = [](std::string_view /*name*/) -> std::expected<graphics::SpriteSheet, Error> {
            return testFontSheet();
        };
        flow = std::make_unique<StartUpFlow>(
            device, stack, sheets, strings, LegalScreenSettings{},
            [this](std::string_view line) { log.emplace_back(line); }, script::ScriptSource{}, std::move(loader),
            std::nullopt, 0);
        flow->setSheetRecordLoader(
            [](std::uint32_t /*record*/) -> std::expected<graphics::SpriteSheet, Error> { return testFontSheet(); });
        flow->setPauseHooks(PauseHooks{
            .pauseSound = [this](bool paused) { soundPauses.push_back(paused); },
            .objectives = {},
            .radarsOff = [this] { ++radarsOff; },
            .radarsBack = [this] { ++radarsBack; },
        });
        GameState& state = flow->state();
        LevelRecord front;
        front.id = 0;
        front.name = "level100";
        front.number = 100;
        state.levels.set(front);
        setLevelNumber(99);
        state.currentLevel = 1;
        state.checkPoint = 3;
        flow->gameplay().setLevel("level99");
        stack.push(flow->gameplay());
    }

    /// Gives the level being played (id 1) the number `number`, as a level of that number would have (the
    /// mission-failed menu lays out `level95` its own way).
    void setLevelNumber(double number) {
        LevelRecord level;
        level.id = 1;
        level.name = "level99";
        level.number = number;
        flow->state().levels.set(level);
    }

    /// Runs `count` frames in test mode.
    void frames(std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); }
};

} // namespace coney::test
