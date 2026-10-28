// SPDX-License-Identifier: GPL-3.0-or-later
// The pause (mode 0xa) and the mission-failed mode (0xc) on the game-mode stack over gameplay, driven by an input
// script with START (docs/research/pause.md).
#include "gamemodes/pause_mode.h"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "gamemodes/armies_game_over_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/mission_failed_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/pause_menu.h"
#include "hud/hud.h"
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

// A loaded level that records how each frame was asked for: "frame", "with HUD" or "world only".
class RecordingLevel final : public coney::GameMode, public coney::OverlaidLevel {
  public:
    explicit RecordingLevel(std::vector<std::string>& renders) : m_renders(renders) {}
    [[nodiscard]] std::uint32_t id() const override { return 0x100; }
    coney::ModeResult update(GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        return coney::ModeResult::Stay;
    }
    void render(const coney::RenderTime& /*time*/) override { m_renders.emplace_back("frame"); }
    void renderWithOverlay(const coney::RenderTime& /*time*/,
                           const std::function<void(coney::graphics::RenderDevice&)>& /*overlay*/) override {
        m_renders.emplace_back("with HUD");
    }
    void renderWorldWithOverlay(const coney::RenderTime& /*time*/,
                                const std::function<void(coney::graphics::RenderDevice&)>& /*overlay*/) override {
        m_renders.emplace_back("world only");
    }

  private:
    std::vector<std::string>& m_renders;
};

// The start-up flow's modes with gameplay pushed alone in `level99` (checkpoint 3), an input script on port 1, and
// the pause hooks recorded.
struct Run {
    coney::test::RecordingDevice device;
    GameModeStack stack;
    coney::gui::GlobalStrings strings;
    std::vector<std::string> log;
    std::vector<bool> soundPauses;
    int radarsOff = 0;
    int radarsBack = 0;
    std::unique_ptr<StartUpFlow> flow;
    std::unique_ptr<coney::ScriptedInput> input;
    GameTimer timer;

    explicit Run(std::string_view script, GameplayMode::LevelLoader loader = {}) {
        input = std::make_unique<coney::ScriptedInput>(coney::parseInputScript(script).value());
        stack.setInput(input.get());
        timer.setFixedStep(true);
        const auto sheets = [](std::string_view /*name*/) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
            return coney::test::testFontSheet();
        };
        flow = std::make_unique<StartUpFlow>(
            device, stack, sheets, strings, coney::LegalScreenSettings{},
            [this](std::string_view line) { log.emplace_back(line); }, coney::script::ScriptSource{}, std::move(loader),
            std::nullopt, 0);
        flow->setSheetRecordLoader(
            [](std::uint32_t /*record*/) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
                return coney::test::testFontSheet();
            });
        flow->setPauseHooks(coney::PauseHooks{
            .pauseSound = [this](bool paused) { soundPauses.push_back(paused); },
            .objectives = {},
            .radarsOff = [this] { ++radarsOff; },
            .radarsBack = [this] { ++radarsBack; },
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
    // Expected: pause.md#pausing, confirmed (code)
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
    // Expected: pause.md#leaving, confirmed (code)
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
    CHECK(run.radarsBack == 1);
}

namespace {

// The audio's side of the banks: the bank in sound RAM, whoever loaded it, and the loads the front end asked for.
struct BankAudio final : coney::FrontEndAudio {
    std::string current;
    std::vector<std::string> loads;
    void loadBank(std::string_view bank) override {
        current = bank;
        loads.emplace_back(bank);
    }
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void playCue(int /*cue*/) override {}
    [[nodiscard]] std::string loadedBank() const override { return current; }
};

} // namespace

TEST_CASE("pause: closing puts back the level's own bank, not the front end's last", "[pause]") {
    // Expected: pause.md#leaving, confirmed (code)
    // The front end loaded `menu`; the level's load then brought in its own bank `sound` without the front end (the
    // loading screen's end, docs/research/sound.md). Closing the pause must give the level `sound` back.
    Run run("5 tap start\n20 tap triangle\n");
    BankAudio audio;
    run.flow->services().attachAudio(&audio);
    run.flow->services().loadBank("menu");
    audio.current = "sound";
    run.frames(90);
    REQUIRE(run.stack.topId() == GameplayMode::kId);
    CHECK(audio.loads == std::vector<std::string>{"menu", std::string(PauseMode::kSoundBank), "sound"});
    CHECK(audio.current == "sound");
    CHECK(run.flow->services().bank() == "sound");
}

TEST_CASE("pause: START closes the menu only after 1.5 s", "[pause]") {
    // Expected: pause.md#leaving, confirmed (code)
    Run run("5 tap start\n20 tap start\n70 tap start\n");
    run.frames(30);
    CHECK_FALSE(run.flow->pause().menu().closing());
    run.frames(42);
    CHECK(run.flow->pause().menu().closing());
}

TEST_CASE("pause: Quit to the main menu leaves gameplay with checkpoint 1 and the level `menu` asked for", "[pause]") {
    // Expected: pause.md#leaving, confirmed (code)
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
    // Expected: pause.md#leaving, confirmed (code)
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
    // Expected: pause.md#the-mission-failed-screen, confirmed (code)
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

TEST_CASE("Armies game over: player 2's Start counts only when he is in the game", "[pause][armies]") {
    // Expected: pause.md#an-game-over, confirmed (code): ANGameOver_Update reads player 2's Start only when the game
    // state's +0x22c human has a pad record.
    Run run("1 p2 connect\n6 p2 tap start\n12 tap start\n");
    // The level's set-up clears the credits: give them after it, as the stage's script does.
    run.frames(2);
    run.flow->hud().armies().setCredits(3);
    run.flow->launchArmiesGameOver();
    run.frames(8);
    // Level99 here has no player 2: his Start spends nothing and the countdown goes on.
    CHECK(run.flow->hud().armies().credits() == 3);
    REQUIRE(run.stack.topId() == coney::ArmiesGameOverMode::kId);
    CHECK_FALSE(run.flow->armiesGameOver().menu().choice());
    // Player 1's Start spends a credit.
    run.frames(4);
    CHECK(run.flow->hud().armies().credits() == 2);
}

TEST_CASE("Armies game over: a continue enables each continuing player's panel again", "[pause][armies]") {
    // Expected: armies.md#game-over, confirmed (code): ANGameOver_Toggle (0x001557f8) calls each continuing player's
    // panel slot +0x24, ANHud_Enable (0x0020fd40), which makes the panel wanted again, so a join message turned on
    // later no longer shows for him.
    Run run("12 tap start\n");
    run.frames(2);
    coney::hud::ArmiesHud& armies = run.flow->hud().armies();
    armies.setCredits(3);
    // Player 1's panel, attached, then dropped by his knock-out.
    armies.panelAttached(0);
    armies.setJoinMessage(0, true);
    armies.update(0, 60, {true, false}, {true, false});
    armies.update(10, 60, {true, false}, {true, false});
    REQUIRE(armies.joinShowing(0));
    run.flow->launchArmiesGameOver();
    run.frames(14);
    REQUIRE(run.stack.topId() == GameplayMode::kId);
    CHECK(armies.credits() == 2);
    armies.setJoinMessage(0, true);
    armies.update(20, 60, {true, false}, {false, false});
    CHECK_FALSE(armies.joinShowing(0));
}

TEST_CASE("Armies game over: HUDLaunchANGameOver with the screen up closes it", "[pause][armies]") {
    // Expected: pause.md#an-game-over, confirmed (code): ANGameOver_Toggle pops mode 0xd when it is on top.
    Run run("");
    run.frames(2);
    run.flow->launchArmiesGameOver();
    run.frames(2);
    REQUIRE(run.stack.topId() == coney::ArmiesGameOverMode::kId);
    run.flow->launchArmiesGameOver();
    run.frames(1);
    CHECK(run.stack.topId() == GameplayMode::kId);
}

TEST_CASE("Armies game over: the screen shows the level's world under its menu, without the HUD", "[pause][armies]") {
    // Expected: armies.md#game-over, confirmed (code): mode 0xd's update (Gm_ANGameOver_Update, 0x00155c78) renders
    // only the world's viewport (GameMode_RenderViewportAlt, 0x00156540) and the menu, no HUD.
    std::vector<std::string> renders;
    Run run("",
            [&renders](const coney::LevelStart& /*start*/, const coney::ScriptedCast& /*cast*/)
                -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
                return std::make_unique<RecordingLevel>(renders);
            });
    run.frames(2);
    run.flow->launchArmiesGameOver();
    run.frames(2);
    REQUIRE(run.stack.topId() == coney::ArmiesGameOverMode::kId);
    REQUIRE_FALSE(renders.empty());
    CHECK(renders.back() == "world only");
}

TEST_CASE("pause: the Options screen's values read from and write to the game state where save.md puts them",
          "[pause][options]") {
    // Expected: save.md#options, confirmed (code)
    coney::GameState state;
    state.brightness = 55;
    state.subtitles = true;
    state.saved.musicVolume = 0.4F;
    state.saved.pads[1].invertCamera = true;
    state.saved.wideVideo = true;
    coney::gui::OptionValues values = coney::optionValuesOf(state);
    CHECK(values.brightness == 55);
    CHECK(values.subtitles);
    CHECK(values.musicVolume == 0.4F);
    CHECK(values.soundFxVolume == 0.9F);
    CHECK(values.invertCamera == std::array<bool, 2>{false, true});
    CHECK(values.autoAdjustCamera == std::array<bool, 2>{true, true});
    CHECK(values.wideVideo);
    values.vibration[0] = false;
    values.proLogic = true;
    values.splitScreenMerge = false;
    values.soundFxVolume = 0.25F;
    coney::storeOptionValues(values, state);
    CHECK_FALSE(state.saved.pads[0].vibration);
    CHECK(state.saved.proLogic2);
    CHECK_FALSE(state.saved.splitScreenMerge);
    CHECK(state.saved.soundFxVolume == 0.25F);
    CHECK(coney::optionValuesOf(state) == values);
}
