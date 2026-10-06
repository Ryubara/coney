// SPDX-License-Identifier: GPL-3.0-or-later
// STORY from the main menu into a level, through the original's states: the profile manager's exit calls
// Menu.startGame, which sets the checkpoint, chooses the level and launches the mission-complete mode (0xb); its enter
// calls UnlockAndLoad, which chooses again; it pops; the level flow (8) finishes the front end and pushes gameplay (1),
// whose enter runs the level script, where HuCreate makes player 1, and loads the level with him at that start
// (docs/research/frontend.md#story-start, docs/research/level-loading.md#story-into-level99). Synthetic scripts and a
// fake level; nothing from the disc.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
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
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/level_start.h"
#include "gamemodes/mission_complete_mode.h"
#include "gamemodes/profile_manager_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "gui/profile_management_gui/pm_new_game_screens.h"
#include "scenes/scene_player.h"
#include "scripting/script_system.h"
#include "support/fixtures.h"
#include "support/font_fixtures.h"
#include "support/lua_fixtures.h"
#include "support/recording_device.h"
#include "warriors/disk_profile_store.h"

using coney::GameModeStack;
using coney::GameplayMode;
using coney::LevelFlowMode;
using coney::MissionCompleteMode;

namespace {

// The scripts of a story start, written like the game's (Lua 4.0 bytecode built by hand): a two-record level table;
// level100.lua's Menu.startGame does what runNextMission(1) does (SetCheckPoint(2), MenuLoadLevel("level1"),
// HUDLaunchMissionComplete(4)); global.lua's UnlockAndLoad chooses the level again (left out when `unlockAndLoad` is
// false); level1.lua creates Ash as player 2, then Rembrandt as player 1 at {GetCheckPoint(), 20, 30} heading 90.
std::map<std::string, std::vector<std::byte>, std::less<>> storyScripts(bool unlockAndLoad) {
    using coney::test::LuaAsm;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    LuaAsm levels;
    for (const auto& [index, name] : {std::pair{0, "level100"}, std::pair{1, "level1"}}) {
        levels.getGlobal("CfgLevelName").pushInt(index).pushString(name).pushString("").pushString(name);
        levels.pushString("").pushInt(index == 0 ? 100 : 1).call(6);
    }
    files["config_preload3.lua"] = coney::test::luaChunk(levels.end());
    for (const char* empty : {"enum_preload.lua", "config_preload.lua", "config_preload2.lua"}) {
        files[empty] = coney::test::luaChunk(LuaAsm().end());
    }

    // global.lua: UnlockAndLoad = function() MenuLoadLevel("level1"); HUDLaunchMissionComplete(4) end
    LuaAsm unlock;
    unlock.getGlobal("MenuLoadLevel").pushString("level1").call(1);
    unlock.getGlobal("HUDLaunchMissionComplete").pushInt(4).call(1);
    LuaAsm global;
    if (unlockAndLoad) {
        global.spec.protos = {unlock.end()};
        global.closure(0).setGlobal("UnlockAndLoad");
    }
    files["global.lua"] = coney::test::luaChunk(global.end());

    // level100.lua: Menu.onStart shows the menus and fades in (the movies left the screen black); Menu.startGame
    // starts the story.
    LuaAsm onStart;
    onStart.getGlobal("ShowProfileManager").pushString("Menu.fadeToRMI").pushString("Menu.startGame").call(2);
    onStart.getGlobal("ScreenQueueEffect").pushInt(0).pushInt(1).call(2);
    LuaAsm startGame;
    startGame.getGlobal("SetCheckPoint").pushInt(2).call(1);
    startGame.getGlobal("MenuLoadLevel").pushString("level1").call(1);
    startGame.getGlobal("HUDLaunchMissionComplete").pushInt(4).call(1);
    LuaAsm front;
    front.spec.protos = {onStart.end(), startGame.end()};
    front.newTable().setGlobal("Menu");
    front.getGlobal("Menu").pushString("onStart").closure(0).setTable();
    front.getGlobal("Menu").pushString("startGame").closure(1).setTable();
    files["level100.lua"] = coney::test::luaChunk(front.end());

    // level1.lua: pos = {}; pos[1] = GetCheckPoint(); pos[2] = 20; pos[3] = 30; then the two HuCreate calls.
    LuaAsm level;
    level.newTable().setGlobal("pos");
    level.getGlobal("pos").pushInt(1).getGlobal("GetCheckPoint").call(0, 1).setTable();
    level.getGlobal("pos").pushInt(2).pushInt(20).setTable();
    level.getGlobal("pos").pushInt(3).pushInt(30).setTable();
    level.getGlobal("HuCreate").pushString("Ash").pushInt(40).getGlobal("pos").pushInt(235).pushString("warr_sw");
    level.pushInt(2).call(6);
    level.getGlobal("HuCreate").pushString("Rembrandt").pushInt(32).getGlobal("pos").pushInt(90).pushString("warr_sw");
    level.pushInt(1).call(6);
    files["level1.lua"] = coney::test::luaChunk(level.end());
    return files;
}

// A loaded level that only counts what the stack asks of it.
class FakeLevel final : public coney::GameMode {
  public:
    explicit FakeLevel(int& updates) : m_updates(updates) {}
    [[nodiscard]] std::uint32_t id() const override { return 0x1ff; }
    coney::ModeResult update(GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        ++m_updates;
        return coney::ModeResult::Stay;
    }

  private:
    int& m_updates;
};

// The start-up flow with the story scripts, a scripted pad and a level loader that records each start it is given.
struct StoryRun {
    coney::test::RecordingDevice device;
    GameModeStack stack;
    coney::gui::GlobalStrings strings;
    std::vector<std::string> log;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<coney::LevelStart> starts;
    coney::camera::Cameras* castCameras = nullptr; // the cameras the last load was given
    std::vector<const coney::scenes::SceneSystem*> castScenes; // each load's ScriptedCast::scenes
    int levelUpdates = 0;
    std::unique_ptr<coney::StartUpFlow> flow;
    std::unique_ptr<coney::ScriptedInput> input;
    coney::GameTimer timer;

    // `profiles`: the folder of saved profiles; nothing keeps them for the run only.
    StoryRun(std::vector<coney::InputEvent> script, bool unlockAndLoad,
             const std::optional<std::filesystem::path>& profiles = std::nullopt)
        : files(storyScripts(unlockAndLoad)) {
        // PM_Create's keyboard (global string 0x97); the synthetic preloads set no strings.
        strings.set(coney::gui::PmCreate::kCharactersString, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.  !?#&");
        input = std::make_unique<coney::ScriptedInput>(std::move(script));
        stack.setInput(input.get());
        timer.setFixedStep(true);
        flow = std::make_unique<coney::StartUpFlow>(
            device, stack,
            [](std::string_view) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
                return coney::test::testFontSheet();
            },
            strings, coney::LegalScreenSettings{}, [this](std::string_view line) { log.emplace_back(line); },
            [this](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
                const auto found = files.find(name);
                if (found == files.end()) {
                    return coney::fail(coney::ErrorCode::NotFound, "no such script");
                }
                return found->second;
            },
            [this](const coney::LevelStart& start,
                   const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
                starts.push_back(start);
                castCameras = cast.cameras;
                castScenes.push_back(cast.scenes);
                return std::make_unique<FakeLevel>(levelUpdates);
            },
            profiles);
        flow->start();
    }

    // Runs frames one at a time until the mode on top is no longer `id` (at most two seconds), so a check can start on
    // the frame a mode left.
    void untilTopLeaves(std::uint32_t id) {
        for (int i = 0; i < 60 && stack.topId() == id; ++i) {
            frames(1);
        }
    }

    // Runs `count` more frames of the main loop in test mode, the frame index carrying on from the last.
    void frames(std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); }

    // Whether a log line contains `text`.
    [[nodiscard]] bool logged(std::string_view text) const {
        return std::ranges::any_of(log, [text](const std::string& line) { return line.contains(text); });
    }
};

// STORY with a new profile (tests/support/story_new_profile.txt, which `coney --input-script` plays too): the menus
// are done on frame 419 and fade out for a second.
std::vector<coney::InputEvent> storyScript() {
    return coney::loadInputScript(std::string(CONEY_TEST_SUPPORT_DIR) + "/story_new_profile.txt").value();
}

} // namespace

TEST_CASE("story: the profile manager's exit launches the mission-complete mode over the level flow", "[story_start]") {
    StoryRun run(storyScript(), true);
    run.frames(420);
    run.untilTopLeaves(coney::ProfileManagerMode::kId);
    // Menu.startGame ran in the profile manager's exit: checkpoint 2, level1 chosen, mode 0xb pushed over mode 8.
    CHECK(run.logged("script: Menu.startGame"));
    CHECK(run.flow->state().checkPoint == 2.0);
    CHECK(run.flow->levelFlow().chosenLevel() == 1);
    CHECK(run.stack.topId() == MissionCompleteMode::kId);
    CHECK(run.stack.size() == 2);
    CHECK(run.flow->missionComplete().kind() == 4);

    // Next frame: its enter calls UnlockAndLoad, which chooses the level again and launches it again (already on top,
    // so no second push); its update pops it, kind 4 doing nothing more.
    run.frames(1);
    CHECK(run.flow->missionComplete().launches() == 2);
    CHECK(run.logged("mission complete: kind 4 (already on top)"));
    CHECK(run.stack.topId() == LevelFlowMode::kId);
    CHECK(run.flow->levelFlow().levelRequests() == std::vector<std::string>{"level1", "level1"});
    CHECK(run.starts.empty());
}

TEST_CASE("story: the level flow pushes gameplay, whose level script places player 1 for the checkpoint",
          "[story_start]") {
    StoryRun run(storyScript(), true);
    run.frames(420);
    run.untilTopLeaves(coney::ProfileManagerMode::kId);
    run.frames(2);
    // The level flow finished the front end (a fresh Lua state) and pushed gameplay with level1 selected.
    CHECK(run.stack.topId() == GameplayMode::kId);
    CHECK(run.stack.size() == 2);
    CHECK(run.flow->scripts().generation() == 2);
    CHECK(run.flow->state().currentLevel == 1);
    CHECK_FALSE(run.flow->levelFlow().frontEndLoaded());
    CHECK(run.flow->levelFlow().chosenLevel() == LevelFlowMode::kNoLevel);

    // Gameplay's enter ran level1.lua and loaded the level with Rembrandt, player 1, where the script made him.
    run.frames(1);
    REQUIRE(run.starts.size() == 1);
    const coney::LevelStart& start = run.starts.front();
    CHECK(start.level == "level1");
    CHECK(start.checkpoint == 2);
    REQUIRE(start.player.has_value());
    const coney::HumanCreation player = start.player.value_or(coney::HumanCreation{});
    CHECK(player.name == "Rembrandt");
    CHECK(player.type == 32);
    CHECK(player.playerIndex == 1);
    CHECK(player.headingDegrees == 90.0F);
    CHECK(player.position == std::optional(std::array<float, 3>{2.0F, 20.0F, 30.0F}));
    CHECK(run.flow->humans().all().size() == 2);
    CHECK(run.flow->gameplay().level() != nullptr);
    // Player 1's cameras for the level, which its script's camera calls drive, handed to the level for its player.
    CHECK(run.castCameras != nullptr);
    CHECK(run.flow->gameplay().cameras() == run.castCameras);
    CHECK(run.levelUpdates == 1);
    // And it keeps playing: the level updates every frame.
    run.frames(10);
    CHECK(run.levelUpdates == 11);
    CHECK(run.flow->scripts().errors() == 0);
    CHECK(run.flow->scripts().skippedCalls() == 0);
}

TEST_CASE("story: gameplay makes each level's scene system and hands it to the level", "[story_start][scenes]") {
    StoryRun run(storyScript(), true);
    const coney::scenes::SceneList list;
    int made = 0;
    run.flow->gameplay().setSceneMaker([&list, &made] {
        ++made;
        return std::make_unique<coney::scenes::SceneSystem>(
            list,
            [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                return coney::fail(coney::ErrorCode::NotFound, "no scenes in this test");
            },
            coney::scenes::SceneSystem::ScriptCall{});
    });
    run.frames(420);
    run.untilTopLeaves(coney::ProfileManagerMode::kId);
    run.frames(3);
    REQUIRE(run.stack.topId() == GameplayMode::kId);
    CHECK(made == 1);
    REQUIRE(run.castScenes.size() == 1);
    CHECK(run.castScenes.front() != nullptr);

    // The level ends (mission complete, kind 1) and its scenes with it; the next level makes its own.
    run.flow->missionComplete().launch(MissionCompleteMode::kKindCheckpointOne);
    run.frames(1);
    CHECK(run.flow->gameplay().level() == nullptr);
    CHECK(made == 1);
}

TEST_CASE("mission complete: kind 1 puts the checkpoint back and ends gameplay below it", "[story_start]") {
    // Without UnlockAndLoad, the launch's own kind stands.
    StoryRun run(storyScript(), false);
    run.frames(420);
    run.untilTopLeaves(coney::ProfileManagerMode::kId);
    run.frames(3);
    REQUIRE(run.stack.topId() == GameplayMode::kId);
    REQUIRE(run.flow->state().checkPoint == 2.0);

    // A mission ends: kind 1 over gameplay. The next frame enters and updates it; it leaves, the checkpoint is 1 again
    // and gameplay below goes too (a fresh Lua state). No level is chosen, so the level flow, resumed with "load the
    // front end on resume" still set from its enter, brings the front end back
    // (docs/research/frontend.md#mode-8-fields).
    run.flow->missionComplete().launch(MissionCompleteMode::kKindCheckpointOne);
    CHECK(run.stack.topId() == MissionCompleteMode::kId);
    run.frames(1);
    CHECK(run.flow->state().checkPoint == 1.0);
    CHECK(run.flow->gameplay().level() == nullptr);
    CHECK(run.flow->missionComplete().kind() == 0);
    CHECK(run.flow->levelFlow().frontEndLoaded());
    CHECK(run.stack.topId() == coney::ProfileManagerMode::kId);
    CHECK(run.stack.size() == 2);
    CHECK(run.flow->scripts().generation() == 3);
}

TEST_CASE("mission complete: the autosave after a mission writes the game state into the profile", "[story_start]") {
    const coney::test::TempDir folder;
    StoryRun run(storyScript(), false, folder.path());
    run.frames(420);
    run.untilTopLeaves(coney::ProfileManagerMode::kId);
    run.frames(3);
    REQUIRE(run.stack.topId() == GameplayMode::kId);
    REQUIRE(run.flow->profiles().loaded() == 0);

    // The mission banks money; mode 0xb's autosave stores it.
    run.flow->state().saved.addToBank(250);
    run.flow->missionComplete().launch(MissionCompleteMode::kKindCheckpointOne);
    run.frames(1);
    coney::GameState reread;
    coney::DiskProfileStore store(folder.path(), reread);
    REQUIRE(store.load(0));
    CHECK(reread.saved.bankedMoney == 250);
}

TEST_CASE("mission complete: kind 2 reloads the current level", "[story_start]") {
    StoryRun run(storyScript(), false);
    run.frames(420);
    run.untilTopLeaves(coney::ProfileManagerMode::kId);
    run.frames(3);
    REQUIRE(run.starts.size() == 1);
    run.flow->missionComplete().launch(MissionCompleteMode::kKindReloadLevel);
    // The mode pops itself and gameplay, choosing level1 again; the level flow starts it; gameplay loads it.
    run.frames(3);
    CHECK(run.stack.topId() == GameplayMode::kId);
    CHECK(run.starts.size() == 2);
    CHECK(run.starts.back().level == "level1");
}

TEST_CASE("a level's scripts run alone give player 1's start for the checkpoint asked for", "[story_start]") {
    const auto files = storyScripts(false);
    const coney::script::ScriptSource source =
        [&files](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
        const auto found = files.find(name);
        if (found == files.end()) {
            return coney::fail(coney::ErrorCode::NotFound, "no such script");
        }
        return found->second;
    };
    const coney::LevelScriptRun run = coney::runLevelScriptAlone(source, "level1", 3, {});
    CHECK(run.start.level == "level1");
    CHECK(run.start.checkpoint == 3);
    CHECK(run.humans == 2);
    CHECK(run.scriptErrors == 0);
    REQUIRE(run.start.player.has_value());
    const coney::HumanCreation player = run.start.player.value_or(coney::HumanCreation{});
    CHECK(player.name == "Rembrandt");
    CHECK(player.position == std::optional(std::array<float, 3>{3.0F, 20.0F, 30.0F}));

    // A level whose script makes no player 1 (the front end's) has no start.
    const coney::LevelScriptRun front = coney::runLevelScriptAlone(source, "level100", 1, {});
    CHECK_FALSE(front.start.player.has_value());
    CHECK(front.humans == 0);
}
