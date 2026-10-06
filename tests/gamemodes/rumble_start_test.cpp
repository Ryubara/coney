// SPDX-License-Identifier: GPL-3.0-or-later
// QUICK RUMBLE from the main menu into an arena, through the original's states: the profile manager's quick rumble
// callback opens the Rumble menu (mode 0x11); its exit calls the start callback with the arena's level number, which
// sets the checkpoint and chooses the level; the level flow (8) pushes gameplay (1), whose level script reads the
// set-up (GetRumbleModeData), adds a flag and sets the start callback, which makes player 1 and teleports him onto the
// flag (docs/research/frontend.md#quick-rumble, docs/research/flags.md#player-starts). Synthetic scripts and a fake
// level; nothing from the disc.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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
#include "gamemodes/profile_manager_mode.h"
#include "gamemodes/rumble_menu_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "support/font_fixtures.h"
#include "support/lua_fixtures.h"
#include "support/recording_device.h"
#include "support/rumble_fixtures.h"

namespace {

using coney::test::LuaAsm;

// The set-up every screen's first entry gives over the synthetic Rumble chunks (rumble_fixtures.h): mode 12, one
// player, gang 5 (pack 4) against gang 3 (pack 2) with the stand-in types.
constexpr std::array<std::uint16_t, coney::RumbleSetup::kValues> kDefaultValues{
    3, 12, 1, 4, 2, 91, 94, 91, 92, 93, 94, 91, 92, 93, 225, 226, 224, 225, 226, 227, 228, 225, 226};

// The scripts of a quick rumble, written like the game's (Lua 4.0 bytecode built by hand): a level table of level100
// and level102; level100.lua's Menu.fadeToRMI opens the Rumble menu at once, Menu.startRumbleMode(n) sets checkpoint 1
// and chooses "level" .. n, Menu.cancelRumbleMode does nothing; level102.lua reads the set-up, sizes the pool, adds
// the flag fP1_01 at (5, 6, 7) facing 128 and sets the start callback DoRules, which creates P11 as the set-up's first
// gang 1 type at (1, 2, 3) facing 270 and teleports him onto the flag.
std::map<std::string, std::vector<std::byte>, std::less<>> rumbleScripts() {
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    LuaAsm levels;
    for (const auto& [index, name, number] : {std::tuple{0, "level100", 100}, std::tuple{1, "level102", 102}}) {
        levels.getGlobal("CfgLevelName").pushInt(index).pushString(name).pushString("").pushString(name);
        levels.pushString("").pushInt(number).call(6);
    }
    files["config_preload3.lua"] = coney::test::luaChunk(levels.end());
    for (const char* empty : {"enum_preload.lua", "config_preload.lua", "config_preload2.lua", "global.lua"}) {
        files[empty] = coney::test::luaChunk(LuaAsm().end());
    }

    LuaAsm onStart;
    onStart.getGlobal("ShowProfileManager").pushString("Menu.fadeToRMI").pushString("Menu.startGame").call(2);
    LuaAsm fadeToRmi;
    fadeToRmi.getGlobal("ShowRumbleModeInterface").pushString("Menu.cancelRumbleMode");
    fadeToRmi.pushString("Menu.startRumbleMode").pushInt(1).call(3);
    LuaAsm startRumble;
    startRumble.params(1);
    startRumble.getGlobal("SetCheckPoint").pushInt(1).call(1);
    startRumble.getGlobal("MenuLoadLevel").pushString("level").getLocal(0).concat(2).call(1);
    LuaAsm cancelRumble;
    LuaAsm front;
    front.spec.protos = {onStart.end(), fadeToRmi.end(), startRumble.end(), cancelRumble.end()};
    front.newTable().setGlobal("Menu");
    front.getGlobal("Menu").pushString("onStart").closure(0).setTable();
    front.getGlobal("Menu").pushString("fadeToRMI").closure(1).setTable();
    front.getGlobal("Menu").pushString("startRumbleMode").closure(2).setTable();
    front.getGlobal("Menu").pushString("cancelRumbleMode").closure(3).setTable();
    files["level100.lua"] = coney::test::luaChunk(front.end());

    LuaAsm doRules;
    doRules.getGlobal("HuCreate").pushString("P11").getGlobal("data").pushInt(6).getTable().getGlobal("start");
    doRules.pushInt(270).pushNil().pushInt(1).call(6, 1).setGlobal("P11");
    doRules.getGlobal("TeleportToFlag").getGlobal("P11").getGlobal("fP1").pushInt(-1).call(3);
    LuaAsm arena;
    arena.spec.protos = {doRules.end()};
    arena.newTable().setGlobal("data");
    arena.getGlobal("GetRumbleModeData").getGlobal("data").call(1);
    arena.getGlobal("CfgSetDatabaseSizes").pushInt(10).pushInt(2).newTable().call(3);
    for (const auto& [table, x, y, z] : {std::tuple{"flagPos", 5, 6, 7}, std::tuple{"start", 1, 2, 3}}) {
        arena.newTable().setGlobal(table);
        arena.getGlobal(table).pushInt(1).pushInt(x).setTable();
        arena.getGlobal(table).pushInt(2).pushInt(y).setTable();
        arena.getGlobal(table).pushInt(3).pushInt(z).setTable();
    }
    arena.getGlobal("AddFlag").pushString("fP1_01").getGlobal("flagPos").pushInt(128).pushInt(0).pushInt(0);
    arena.call(5, 1).setGlobal("fP1");
    arena.closure(0).setGlobal("DoRules");
    arena.getGlobal("SetStartGameCallback").pushString("DoRules").call(1);
    files["level102.lua"] = coney::test::luaChunk(arena.end());
    coney::test::addRumbleChunks(files);
    return files;
}

// A loaded level that does nothing.
class FakeLevel final : public coney::GameMode {
  public:
    [[nodiscard]] std::uint32_t id() const override { return 0x1ff; }
    coney::ModeResult update(coney::GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        return coney::ModeResult::Stay;
    }
};

// The start-up flow with the quick rumble scripts, a scripted pad and a level loader that records each start.
struct RumbleRun {
    coney::test::RecordingDevice device;
    coney::GameModeStack stack;
    coney::gui::GlobalStrings strings;
    std::vector<std::string> log;
    std::map<std::string, std::vector<std::byte>, std::less<>> files = rumbleScripts();
    std::vector<coney::LevelStart> starts;
    std::unique_ptr<coney::StartUpFlow> flow;
    std::unique_ptr<coney::ScriptedInput> input;
    coney::GameTimer timer;

    explicit RumbleRun(std::string_view script) {
        input = std::make_unique<coney::ScriptedInput>(coney::parseInputScript(script).value());
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
            [this](const coney::LevelStart& start, const coney::ScriptedCast& /*cast*/)
                -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
                starts.push_back(start);
                return std::make_unique<FakeLevel>();
            });
        flow->start();
    }

    // Runs `count` more frames of the main loop in test mode.
    void frames(std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); }
};

// START, the stick up most of the way (wrapping to quick rumble), cross: the Rumble menu opens.
constexpr std::string_view kQuickRumble = "200 tap start\n212 stick left 0 70\n214 stick left 0 0\n225 tap cross\n";

} // namespace

TEST_CASE("quick rumble: the Rumble menu starts the arena and the start callback teleports player 1 onto its flag",
          "[rumble_start]") {
    // Cross on each screen (on the gang screen once for each side), keeping each screen's first entry.
    RumbleRun run(std::string(kQuickRumble) +
                  "240 tap cross\n250 tap cross\n260 tap cross\n270 tap cross\n280 tap cross\n");
    run.frames(230);
    // The profile manager's quick rumble callback opened mode 0x11 over it, at its Game Mode screen.
    REQUIRE(run.stack.topId() == coney::RumbleMenuMode::kId);
    CHECK(run.flow->rumbleMenu().fromFrontEnd());
    CHECK(run.flow->rumbleMenu().menu().screen() == coney::gui::RumbleScreen::GameMode);

    // The screens write the default set-up; the area's cross leaves calling Menu.startRumbleMode(102), the profile
    // manager goes too, and the level flow starts level102, whose start callback places P11 on the flag with the
    // flag's heading.
    run.frames(60);
    for (const std::string& line : run.log) {
        UNSCOPED_INFO(line);
    }
    CHECK(run.flow->state().rumble.values == kDefaultValues);
    CHECK(run.flow->state().rumble.levelNumber == coney::kDefaultRumbleArena);
    CHECK(run.flow->rumbleMenu().started());
    CHECK(run.stack.topId() == coney::GameplayMode::kId);
    REQUIRE(run.starts.size() == 1);
    const coney::LevelStart& start = run.starts.front();
    CHECK(start.level == "level102");
    CHECK(start.checkpoint == 1);
    REQUIRE(start.player.has_value());
    const coney::HumanCreation player = start.player.value_or(coney::HumanCreation{});
    CHECK(player.name == "P11");
    CHECK(player.type == kDefaultValues.at(5));
    REQUIRE(player.teleported.has_value());
    const coney::world_objects::Placement placed = player.teleported.value_or(coney::world_objects::Placement{});
    CHECK(placed.position == std::array<float, 3>{5.0F, 6.0F, 7.0F});
    CHECK(placed.headingDegrees == 128.0F);
    // The level's flag and InitLevel's two; the callback was called once and cleared.
    CHECK(run.flow->flags().all().size() == 3);
    CHECK(run.flow->state().startGameCallback.empty());
    CHECK(run.flow->scripts().errors() == 0);
}

TEST_CASE("quick rumble: backing out of the Rumble menu calls the cancel callback and keeps the menus",
          "[rumble_start]") {
    RumbleRun run(std::string(kQuickRumble) + "240 tap triangle\n");
    run.frames(250);
    CHECK(run.flow->rumbleMenu().cancelled());
    CHECK(run.stack.topId() == coney::ProfileManagerMode::kId);
    CHECK(run.starts.empty());
    CHECK(run.flow->levelFlow().chosenLevel() == coney::LevelFlowMode::kNoLevel);
}

TEST_CASE("a Rumble arena's level number is known for each Rumble level only", "[rumble_start]") {
    CHECK(coney::rumbleArenaOf("level117") == 117);
    CHECK(coney::rumbleArenaOf("level102") == coney::kDefaultRumbleArena);
    CHECK(!coney::rumbleArenaOf("level99").has_value());
    CHECK(!coney::rumbleArenaOf("level102s").has_value());
    CHECK(!coney::rumbleArenaOf("sandbox").has_value());
}
