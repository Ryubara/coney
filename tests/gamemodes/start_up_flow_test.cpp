// SPDX-License-Identifier: GPL-3.0-or-later
// The start-up path from the legal screen to the main menu (modes 5, 6, 8 and 0x12), run on the game-mode stack with
// synthetic sheets and a scripted pad (docs/research/frontend.md#mode-flow).
#include "gamemodes/start_up_flow.h"

#include <algorithm>
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

#include "animation/anim_math.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "effects/screen_tint.h"
#include "gamemodes/front_end_scene.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "gamemodes/memory_card_mode.h"
#include "gamemodes/profile_manager_mode.h"
#include "graphics/render_device.h"
#include "gui/global_strings.h"
#include "gui/profile_management_gui/pm_new_game_screens.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "support/fixtures.h"
#include "support/font_fixtures.h"
#include "support/lua_fixtures.h"
#include "support/recording_device.h"
#include "support/scene_fixtures.h"
#include "warriors/disk_profile_store.h"
#include "warriors/game_state.h"
#include "warriors/profile_record.h"

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

    explicit Run(std::string_view script, std::uint64_t cardCheckingMs = 0) {
        strings.set(0x76, "PRESS START");
        strings.set(MemoryCardMode::kCheckingString, "CHECKING");
        input = std::make_unique<coney::ScriptedInput>(coney::parseInputScript(script).value());
        stack.setInput(input.get());
        timer.setFixedStep(true);
        flow = std::make_unique<StartUpFlow>(
            device, stack, sheets.loader(), strings, coney::LegalScreenSettings{},
            [this](std::string_view line) { log.emplace_back(line); }, coney::script::ScriptSource{},
            coney::GameplayMode::LevelLoader{}, std::nullopt, cardCheckingMs);
        flow->start();
    }

    // Runs `count` more frames of the main loop in test mode (a step and a render each), the frame index carrying on
    // from the last.
    void frames(std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); }
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
    CHECK(run.flow->services().bank() == "menu");
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.flow->profileManager().onRumble() == "Menu.fadeToRMI");
    CHECK(run.flow->profileManager().onStartGame() == "Menu.startGame");

    // Frame 152: the profile manager enters at PM_Greet, loading the menu sheet and the two fonts (the memory-card
    // check loaded big_font for its message).
    run.frames(1);
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Greet");
    CHECK(run.sheets.requested ==
          std::vector<std::string>{"legal_screen", "big_font", "menu_system", "part_page0", "big_font"});
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
    // level100.lua: Menu = {}; Menu.onStart = function() ShowProfileManager("Menu.fadeToRMI", "Menu.startGame");
    // ScreenQueueEffect(0, 1) end; Menu.startGame = function() MenuLoadLevel("level1") end; Menu.reloadProfiles =
    // function() SSMC_StartLoadSequence() end (the game's fades out first and calls it 700 ms later);
    // Menu.deleteProfile = function() ScreenQueueEffect(1, 0) SSMC_StartDeleteSequence() end (the game's)
    LuaAsm onStart;
    onStart.getGlobal("ShowProfileManager").pushString("Menu.fadeToRMI").pushString("Menu.startGame").call(2);
    onStart.getGlobal("ScreenQueueEffect").pushInt(0).pushInt(1).call(2);
    LuaAsm startGame;
    startGame.getGlobal("MenuLoadLevel").pushString("level1").call(1);
    LuaAsm reloadProfiles;
    reloadProfiles.getGlobal("SSMC_StartLoadSequence").call(0);
    LuaAsm deleteProfile;
    deleteProfile.getGlobal("ScreenQueueEffect").pushInt(1).pushInt(0).call(2);
    deleteProfile.getGlobal("SSMC_StartDeleteSequence").call(0);
    LuaAsm level;
    level.spec.protos = {onStart.end(), startGame.end(), reloadProfiles.end(), deleteProfile.end()};
    level.newTable().setGlobal("Menu");
    level.getGlobal("Menu").pushString("onStart").closure(0).setTable();
    level.getGlobal("Menu").pushString("startGame").closure(1).setTable();
    level.getGlobal("Menu").pushString("reloadProfiles").closure(2).setTable();
    level.getGlobal("Menu").pushString("deleteProfile").closure(3).setTable();
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

    explicit ScriptedRun(std::string_view script)
        : ScriptedRun(coney::parseInputScript(script).value_or(std::vector<coney::InputEvent>{})) {}

    // `profiles`: the folder of saved profiles; nothing keeps them for the run only.
    explicit ScriptedRun(std::vector<coney::InputEvent> script,
                         const std::optional<std::filesystem::path>& profiles = std::nullopt) {
        // PM_Create's keyboard (global string 0x97); the synthetic preloads set no strings.
        strings.set(coney::gui::PmCreate::kCharactersString, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.  !?#&");
        input = std::make_unique<coney::ScriptedInput>(std::move(script));
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
            },
            coney::GameplayMode::LevelLoader{}, profiles);
        flow->start();
    }

    // Runs `count` more frames of the main loop in test mode, the frame index carrying on from the last.
    void frames(std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); }

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

// A mode standing for whatever is over the level flow when the debug menus jump: a level in play, its pause.
class StandInMode final : public coney::GameMode {
  public:
    explicit StandInMode(std::uint32_t id) : m_id(id) {}
    [[nodiscard]] std::uint32_t id() const override { return m_id; }
    coney::ModeResult update(GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        return coney::ModeResult::Stay;
    }
    void exit() override { exited = true; }
    bool exited = false;

  private:
    std::uint32_t m_id;
};

TEST_CASE("start-up with scripts: the debug menus' jump starts a level at its checkpoint from the menus or a level",
          "[start_up][debug]") {
    ScriptedRun run("");
    run.frames(152);
    REQUIRE(run.stack.topId() == ProfileManagerMode::kId);

    // A name the level table does not have changes nothing.
    CHECK_FALSE(run.flow->jumpToLevel("level77", 2));
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.logged("jump to level77: not in the level table"));

    // From the front end: the menus go, the checkpoint is set and the level flow starts the level on its next step.
    CHECK(run.flow->jumpToLevel("level1", 3));
    CHECK(run.stack.topId() == LevelFlowMode::kId);
    CHECK(run.flow->state().checkPoint == 3.0);
    CHECK(run.flow->levelFlow().chosenLevel() != LevelFlowMode::kNoLevel);
    run.frames(1);
    CHECK(run.flow->levelFlow().levelRequests() == std::vector<std::string>{"level1"});
    CHECK(run.flow->levelFlow().chosenLevel() == LevelFlowMode::kNoLevel);

    // From a level in play with its pause over it: both go, the level flow below starts the new choice.
    StandInMode level(1);
    StandInMode pause(0xa);
    run.stack.push(level);
    run.stack.push(pause);
    run.frames(1);
    CHECK(run.flow->jumpToLevel("level1", 2));
    CHECK(pause.exited);
    CHECK(run.stack.topId() == LevelFlowMode::kId);
    CHECK_FALSE(run.stack.contains(level));
    CHECK(run.flow->state().checkPoint == 2.0);
    CHECK(run.flow->levelFlow().levelRequests().size() == 2);
}

TEST_CASE("start-up with scripts: story reaches Menu.startGame, the level request, and back to the menus",
          "[start_up]") {
    // STORY with a new profile (tests/support/story_new_profile.txt): the menus are done on frame 419, then fade out.
    ScriptedRun run(coney::loadInputScript(std::string(CONEY_TEST_SUPPORT_DIR) + "/story_new_profile.txt").value());
    run.frames(420);
    CHECK(run.flow->profileManager().session().done);
    CHECK(run.flow->levelFlow().levelRequests().empty());
    run.frames(31);
    CHECK(run.logged("profile manager: profile \"A\" created in slot 0"));
    CHECK(run.flow->profiles().profile(0) != nullptr);
    CHECK(run.flow->levelFlow().levelRequests() == std::vector<std::string>{"level1"});
    CHECK(run.logged("level start requested: level1"));
    // The front-end level was unloaded (a fresh Lua state) and started again: the menus are back at PM_Greet.
    CHECK(run.flow->scripts().generation() == 2);
    run.frames(2);
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Greet");
    CHECK(run.flow->scripts().errors() == 0);
}

TEST_CASE("start-up with scripts: a new story profile is saved to the profile folder and read back", "[start_up]") {
    const coney::test::TempDir folder;
    {
        ScriptedRun run(coney::loadInputScript(std::string(CONEY_TEST_SUPPORT_DIR) + "/story_new_profile.txt").value(),
                        folder.path());
        CHECK(run.flow->profiles().count() == 0);
        run.frames(446);
        CHECK(run.logged("profile manager: profile \"A\" created in slot 0"));
        CHECK(std::filesystem::file_size(folder.path() / "profile-1.sav") == coney::ProfileRecord::kSize);
        CHECK(run.flow->profiles().inUse());
    }
    // A later run over the same folder lists it, and the store loads it.
    ScriptedRun again(std::vector<coney::InputEvent>{}, folder.path());
    REQUIRE(again.flow->profiles().profile(0) != nullptr);
    CHECK(again.flow->profiles().profile(0)->name == "A");
    CHECK(again.flow->profiles().load(0));
}

namespace {

// A front-end scene that counts its steps and frames and draws the menus' overlay through itself.
struct CountingScene final : coney::FrontEndScene {
    int* updates;
    int* renders;
    int* destroyed;
    CountingScene(int* u, int* r, int* d) : updates(u), renders(r), destroyed(d) {}
    ~CountingScene() override { ++*destroyed; }
    CountingScene(const CountingScene&) = delete;
    CountingScene& operator=(const CountingScene&) = delete;
    CountingScene(CountingScene&&) = delete;
    CountingScene& operator=(CountingScene&&) = delete;
    void update(std::uint64_t /*nowMs*/) override { ++*updates; }
    void render(const coney::RenderTime& /*time*/, const std::function<void()>& overlay) override {
        ++*renders;
        if (overlay) {
            overlay();
        }
    }
};

} // namespace

TEST_CASE("start-up: the front end loads level100's scene, the menus step and draw it, and it goes with the level",
          "[frontend]") {
    // Declared before the run: the flow still owns a scene when the run is torn down, and the scene's destructor
    // counts into these.
    int updates = 0;
    int renders = 0;
    int destroyed = 0;
    std::vector<std::string> loaded;
    Run run("200 tap start\n");
    run.flow->levelFlow().setSceneLoader(
        [&](std::string_view level) -> std::expected<std::unique_ptr<coney::FrontEndScene>, coney::Error> {
            loaded.emplace_back(level);
            return std::make_unique<CountingScene>(&updates, &renders, &destroyed);
        });
    run.frames(160);
    REQUIRE(loaded == std::vector<std::string>{"level100"});
    CHECK(run.flow->levelFlow().scene() != nullptr);
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    // From the frame the menus are on top, the scene steps and draws once a frame under them.
    const int before = updates;
    run.frames(10);
    CHECK(updates == before + 10);
    CHECK(renders >= 10);
    CHECK(destroyed == 0);
    // A level chosen and the menus gone: the level flow finishes the front end and its scene is released.
    run.flow->menuLoadLevel("level100");
    run.stack.pop();
    run.frames(2);
    CHECK(destroyed >= 1);
}

namespace {

// A front-end scene with a screen tint, drawing the menus' overlay through itself.
struct TintScene final : coney::FrontEndScene {
    coney::effects::ScreenTint screenTint;
    void update(std::uint64_t /*nowMs*/) override {}
    void render(const coney::RenderTime& /*time*/, const std::function<void()>& overlay) override {
        if (overlay) {
            overlay();
        }
    }
    [[nodiscard]] coney::effects::ScreenTint* tint() override { return &screenTint; }
};

} // namespace

TEST_CASE("start-up: the front end's tint reaches the bindings and is drawn over the menus' text", "[frontend]") {
    Run run("");
    TintScene* scene = nullptr;
    int loads = 0;
    run.flow->levelFlow().setScenes({}, &run.flow->context());
    run.flow->levelFlow().setSceneLoader(
        [&scene,
         &loads](std::string_view /*level*/) -> std::expected<std::unique_ptr<coney::FrontEndScene>, coney::Error> {
            auto made = std::make_unique<TintScene>();
            scene = made.get();
            ++loads;
            return made;
        });
    run.frames(160);
    REQUIRE(scene != nullptr);
    REQUIRE(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.flow->context().tint == &scene->screenTint);

    // level100's overlay, as global.lua's SetLevelColour stores it: (7, 20, 30) at 43, drawn at 21 / 128.
    scene->screenTint.setLevelColour({7, 20, 30, 43});
    run.device.draws.clear();
    run.frames(1);
    const coney::graphics::Rgba wash{7, 20, 30, coney::effects::ScreenTint::deviceAlphaOf(43)};
    CHECK(wash.a == 42);
    const auto isTint = [&wash](const coney::test::RecordedDraw& draw) {
        return draw.texture == nullptr && draw.quads.size() == 1 && draw.quads[0].colour == wash;
    };
    const auto tint = std::ranges::find_if(run.device.draws, isTint);
    REQUIRE(tint != run.device.draws.end());
    // After the menus' text (a textured draw), as the original's title frame draws it last.
    CHECK(std::any_of(run.device.draws.begin(), tint,
                      [](const coney::test::RecordedDraw& draw) { return draw.texture != nullptr; }));

    // The front end finished: its tint leaves the bindings with the scene. Without gameplay the level flow starts the
    // front end again, whose new scene's tint the bindings then set.
    REQUIRE(loads == 1);
    run.flow->menuLoadLevel("level100");
    run.stack.pop();
    run.frames(2);
    REQUIRE(loads == 2);
    CHECK(run.flow->context().tint == &scene->screenTint);
}

TEST_CASE("start-up: a scene that fails to load leaves a black background and the menus", "[frontend]") {
    Run run("");
    run.flow->levelFlow().setSceneLoader(
        [](std::string_view /*level*/) -> std::expected<std::unique_ptr<coney::FrontEndScene>, coney::Error> {
            return std::unexpected(coney::Error{coney::ErrorCode::NotFound, "no world"});
        });
    run.frames(160);
    CHECK(run.flow->levelFlow().scene() == nullptr);
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(std::ranges::any_of(run.log,
                              [](const std::string& line) { return line.find("no scene") != std::string::npos; }));
}

TEST_CASE("start-up: the memory-card check shows its message for the original's 3 s in the game", "[start_up]") {
    Run run("", MemoryCardMode::kCheckingMessageMs);
    // Frame 150 (5,000 ms): the check shows "checking memory card", centred, until 8,000 ms.
    run.frames(151);
    REQUIRE(run.stack.topId() == MemoryCardMode::kId);
    const coney::gui::MessageBox& box = run.flow->memoryCard().messageBox();
    CHECK(box.open());
    CHECK_FALSE(box.dialog());
    CHECK(box.message().text() == "CHECKING");
    CHECK(run.flow->memoryCard().bootCheck() == MemoryCardMode::BootCheck::Pending);

    // Frames 151-239 still show it; frame 240 (8,000 ms) ends it and the mode leaves (frames 150-240 in all).
    run.frames(89);
    CHECK(run.stack.topId() == MemoryCardMode::kId);
    run.frames(1);
    CHECK(run.stack.topId() == LevelFlowMode::kId);
    CHECK(run.flow->memoryCard().bootCheck() == MemoryCardMode::BootCheck::Done);
    CHECK_FALSE(run.flow->levelFlow().loadFrontEndOnResume());
}

TEST_CASE("start-up: an input script goes PM_Greet, PM_Mode, EXTRAS and back, with the original's cues",
          "[start_up][frontend]") {
    // START once the 1.5 s fade in is over; right to EXTRAS; cross opens PM_Extras; triangle backs out to PM_Mode.
    Run run("200 tap start\n210 tap right\n220 tap cross\n235 tap triangle\n");
    run.frames(205);
    const coney::gui::PmController& menus = run.flow->profileManager().controller();
    CHECK(menus.currentName() == "PM_Mode");
    run.frames(20);
    CHECK(menus.currentName() == "PM_Extras");
    run.frames(20);
    CHECK(menus.currentName() == "PM_Mode");
    // Accept 9 on START, move 5, accept 9 on EXTRAS, back 0xf.
    CHECK(run.flow->services().cues() == std::vector<int>{9, 5, 9, 0xf});
    // Each screen is logged once, in order.
    std::vector<std::string> screens;
    for (const std::string& line : run.log) {
        if (line.starts_with("profile manager: PM_")) {
            screens.push_back(line);
        }
    }
    CHECK(screens == std::vector<std::string>{"profile manager: PM_Greet\n", "profile manager: PM_Mode\n",
                                              "profile manager: PM_Extras\n", "profile manager: PM_Mode\n"});
}

TEST_CASE("start-up with scripts: the boot's memory-card mode loads the profiles, RELOAD PROFILES again",
          "[start_up]") {
    const coney::test::TempDir folder;
    // START on PM_Greet, cross on STORY (PM_Profile: CREATE NEW PROFILE, RELOAD PROFILES), down, then cross on RELOAD.
    auto script = coney::parseInputScript("200 tap start\n215 tap cross\n240 tap down\n250 tap cross\n");
    REQUIRE(script.has_value());
    ScriptedRun run(std::move(script).value(), folder.path());
    run.frames(152);
    CHECK(run.flow->memoryCard().loads() == 1);
    run.frames(90);
    REQUIRE(run.flow->profileManager().controller().currentName() == "PM_Profile");
    CHECK(run.flow->profiles().count() == 0);

    // A profile appears in the folder while the menus are up (another run, or a copied file).
    {
        coney::GameState other;
        coney::DiskProfileStore writer(folder.path(), other);
        REQUIRE(writer.create(0, coney::Profile{.name = "SWAN"}));
    }
    CHECK(run.flow->profiles().count() == 0);

    // RELOAD PROFILES: Menu.reloadProfiles pushes mode 6, which reads the folder and leaves; the menus fade in with
    // PM_Profile opened again over the profile read.
    run.frames(12);
    CHECK(run.flow->memoryCard().loads() == 2);
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    REQUIRE(run.flow->profiles().profile(0) != nullptr);
    CHECK(run.flow->profiles().profile(0)->name == "SWAN");
    CHECK(run.logged("profile manager: 1 profile(s) after the memory-card mode"));
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Profile");
    CHECK(run.flow->scripts().errors() == 0);
}

TEST_CASE("start-up with scripts: deleting a profile removes its file and the menus fade back in", "[start_up]") {
    const coney::test::TempDir folder;
    {
        coney::GameState other;
        coney::DiskProfileStore writer(folder.path(), other);
        REQUIRE(writer.create(0, coney::Profile{.name = "SWAN"}));
    }
    // START, STORY, down twice to DELETE PROFILE, cross; cross on SWAN in PM_Load; left to YES on PM_Delete, cross.
    auto script = coney::parseInputScript(
        "200 tap start\n215 tap cross\n240 tap down\n250 tap down\n260 tap cross\n270 tap cross\n280 tap left\n"
        "290 tap cross\n");
    REQUIRE(script.has_value());
    ScriptedRun run(std::move(script).value(), folder.path());
    run.frames(152);
    REQUIRE(run.flow->profiles().count() == 1);
    run.frames(185);

    // Menu.deleteProfile blacked the screen and pushed mode 6, which left; the menus faded in over PM_Profile.
    CHECK_FALSE(std::filesystem::exists(folder.path() / "profile-1.sav"));
    CHECK(run.flow->profiles().count() == 0);
    CHECK(run.logged("profile manager: 0 profile(s) after the memory-card mode"));
    CHECK(run.flow->memoryCard().loads() == 1);
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    CHECK(run.flow->profileManager().controller().currentName() == "PM_Profile");
    CHECK(run.flow->fade().level() == 0.0F);
    CHECK(run.flow->scripts().errors() == 0);
}

namespace {

// A front-end world that hosts the front end's scenes and keeps what they did to it.
struct HostingScene final : coney::FrontEndScene, coney::scenes::SceneHost {
    int cameraBegins = 0;
    int cameraEnds = 0;
    std::map<double, coney::anim::Vec3> poses; // the newest position the scene gave each object
    float lowestX = 1e9F;                      // the smallest x any object was given, to see the loop restart
    float highestX = -1e9F;
    void update(std::uint64_t /*nowMs*/) override {}
    void render(const coney::RenderTime& /*time*/, const std::function<void()>& overlay) override {
        if (overlay) {
            overlay();
        }
    }
    coney::scenes::SceneHost* sceneHost() override { return this; }
    void cameraBegin(const coney::scenes::ScenePose& /*pose*/, const coney::scenes::SceneLens& /*lens*/) override {
        ++cameraBegins;
    }
    void cameraEnd(float /*blendSeconds*/) override { ++cameraEnds; }
    void objectPose(double object, const coney::scenes::ScenePose& pose) override {
        poses[object] = pose.position;
        lowestX = std::min(lowestX, pose.position.x);
        highestX = std::max(highestX, pose.position.x);
    }
};

// level100.lua written like the game's WonderWheelAnim (synthetic bytecode, nothing from the disc): an object spawned
// by the main chunk, Menu.onStart shows the menus, fades in and preloads `tst_wheel` with WheelStart as its callback;
// WheelStart(id) keeps the id, binds the object to slot 0 and plays the scene as level100.lua does
// (`ScenePlayCinematic(id, 0, nil, false, false, true, false)`: no bars, not skippable, looping, not frozen);
// Menu.startGame stops it (SceneStop(id)) and asks for level 1.
std::vector<std::byte> wheelLevelScript() {
    using coney::test::LuaAsm;
    LuaAsm onStart;
    onStart.getGlobal("ShowProfileManager").pushString("Menu.fadeToRMI").pushString("Menu.startGame").call(2);
    onStart.getGlobal("ScreenQueueEffect").pushInt(0).pushInt(1).call(2);
    onStart.getGlobal("ScenePreload").pushString("tst_wheel").pushString("WheelStart").call(2);
    LuaAsm startGame;
    startGame.getGlobal("SceneStop").getGlobal("WheelId").call(1);
    startGame.getGlobal("MenuLoadLevel").pushString("level1").call(1);
    LuaAsm wheelStart;
    wheelStart.params(1);
    wheelStart.getLocal(0).setGlobal("WheelId");
    wheelStart.getGlobal("SceneAddObject").getLocal(0).getGlobal("WheelObj").pushInt(0).call(3);
    wheelStart.getGlobal("ScenePlayCinematic").getLocal(0).pushInt(0).pushNil().pushInt(0).pushInt(0).pushInt(1);
    wheelStart.pushInt(0).call(7);
    LuaAsm level;
    level.spec.protos = {onStart.end(), startGame.end(), wheelStart.end()};
    level.newTable().setGlobal("Menu");
    level.getGlobal("Menu").pushString("onStart").closure(0).setTable();
    level.getGlobal("Menu").pushString("startGame").closure(1).setTable();
    level.closure(2).setGlobal("WheelStart");
    level.getGlobal("ObjSpawn").pushString("dyn_test").call(1, 1).setGlobal("WheelObj");
    return coney::test::luaChunk(level.end());
}

// The scene `tst_wheel`: no roles, one object sliding from x 0 to 3 over its 1 s part, and a camera.
coney::test::SceneSpec wheelSceneSpec() {
    coney::test::SceneSpec spec;
    spec.name = "tst_wheel";
    spec.frames = 30;
    spec.objects = {coney::test::RoleSpec{"wheel", {0.0F, 0.0F, 0.0F}, 0.0F, {3.0F, 0.0F, 0.0F}, 0.0F}};
    spec.camera = coney::test::RoleSpec{"camera", {0.0F, -5.0F, 2.0F}, 0.0F, {0.0F, -5.0F, 2.0F}, 0.0F};
    spec.part.objects = {coney::test::TrackSpec{.duration = 1.0F,
                                                .positions = {{0, {0.0F, 0.0F, 0.0F}}, {30, {3.0F, 0.0F, 0.0F}}},
                                                .rotations = {},
                                                .events = {}}};
    spec.part.camera = coney::test::TrackSpec{
        .duration = 1.0F, .positions = {{0, {0.0F, -5.0F, 2.0F}}}, .rotations = {{0, {0, 0, 0}}}, .events = {}};
    return spec;
}

} // namespace

TEST_CASE("start-up with scripts: level100's scene plays looping on the front end's scenes, its object live",
          "[frontend][scenes]") {
    HostingScene* hosting = nullptr;
    const coney::scenes::SceneList list{
        std::vector<coney::scenes::SceneListEntry>{{0, 0, "tst_other"}, {1, 0, "tst_wheel"}}};
    std::vector<coney::scenes::SceneSystem*> made;
    ScriptedRun run("");
    run.files["level100.lua"] = wheelLevelScript();
    LevelFlowMode& levelFlow = run.flow->levelFlow();
    levelFlow.setSceneLoader(
        [&hosting](std::string_view /*level*/) -> std::expected<std::unique_ptr<coney::FrontEndScene>, coney::Error> {
            auto scene = std::make_unique<HostingScene>();
            hosting = scene.get();
            return scene;
        });
    levelFlow.setScenes(
        [&list, &made]() -> std::unique_ptr<coney::scenes::SceneSystem> {
            auto system = std::make_unique<coney::scenes::SceneSystem>(
                list,
                [](std::string_view /*name*/) -> std::expected<std::vector<std::byte>, coney::Error> {
                    return coney::test::sceneHeaderRecord(wheelSceneSpec()).data();
                },
                coney::scenes::SceneSystem::ScriptCall{});
            made.push_back(system.get());
            return system;
        },
        &run.flow->context());

    // Frame 151 starts the front end: its scene system made before the scripts run and handed to the bindings.
    run.frames(160);
    REQUIRE(levelFlow.scenes() != nullptr);
    REQUIRE(hosting != nullptr);
    CHECK(made.size() == 1);
    CHECK(run.flow->context().scenes == levelFlow.scenes());
    CHECK(run.stack.topId() == ProfileManagerMode::kId);
    // The menus step it: the preload's callback bound the object, which SceneAddObject resolved (live) and pinned,
    // and the scene plays with its camera current.
    CHECK(levelFlow.scenes()->state(1) == coney::scenes::SceneState::Playing);
    const double object = run.flow->spawnRecords().all().at(0).handle;
    CHECK(run.flow->spawnRecords().all().at(0).live);
    CHECK(run.flow->spawnRecords().all().at(0).pinned);
    CHECK(hosting->cameraBegins == 1);
    CHECK(hosting->poses.contains(object));

    // It loops: three seconds later it still plays, its object back near the start more than once, and the camera
    // never ended.
    run.frames(90);
    CHECK(levelFlow.scenes()->playing());
    CHECK(levelFlow.scenes()->stats().ended == 0);
    CHECK(hosting->cameraEnds == 0);
    CHECK(hosting->lowestX < 0.5F);
    CHECK(hosting->highestX > 2.5F);
    CHECK(run.flow->scripts().errors() == 0);

    // Menu.startGame stops it and asks for a level: the front end finishes and takes its scenes back from the
    // bindings; without gameplay it starts again with a fresh system that plays the scene again.
    run.flow->scripts().call("Menu.startGame");
    run.stack.pop(); // the menus gone, as their fade out ends
    run.frames(2);
    CHECK(run.logged("level start requested: level1"));
    CHECK(made.size() == 2);
    REQUIRE(levelFlow.scenes() != nullptr);
    CHECK(run.flow->context().scenes == levelFlow.scenes());
    run.frames(10);
    CHECK(levelFlow.scenes()->state(1) == coney::scenes::SceneState::Playing);
}
