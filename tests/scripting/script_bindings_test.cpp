// SPDX-License-Identifier: GPL-3.0-or-later
// The game's bindings in Coney: the binding table, the tolua result conventions (true is 1, false is nil), the getters
// over the game state, CfgLevelName's level table, the script-system bindings and what reaches the front end
// (docs/research/scripting.md#bindings).
#include "scripting/script_bindings.h"

#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/language.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"

using coney::Error;
using coney::script::BindingKind;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A host that records the requests.
class RecordingHost final : public coney::script::BindingHost {
  public:
    std::vector<std::string> requests;

    void showProfileManager(std::string_view onRumble, std::string_view onStartGame) override {
        requests.push_back(std::string("menus ").append(onRumble).append(" ").append(onStartGame));
    }
    void showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double /*players*/) override {
        requests.push_back(std::string("rumble ").append(onCancel).append(" ").append(onStart));
    }
    void menuLoadLevel(std::string_view level) override { requests.push_back("level " + std::string(level)); }
    void playMovie(std::string_view name) override { requests.push_back("movie " + std::string(name)); }
    void playMusic(std::string_view track) override { requests.push_back("music " + std::string(track)); }
    void stopMusic() override { requests.emplace_back("stop"); }
    void queueScreenEffect(int type, double seconds) override {
        requests.push_back("fade " + std::to_string(type) + " " + std::to_string(static_cast<int>(seconds * 10)));
    }
    void launchMissionComplete(int kind) override { requests.push_back("mission " + std::to_string(kind)); }
};

// A script system with Coney's bindings over a game state, strings and a recording host.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    RecordingHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans};
    std::vector<std::string> sourced;
    std::vector<std::string> log;
    ScriptSystem scripts;

    Harness()
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
                  sourced.emplace_back(name);
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); },
              [this](std::string_view line) { log.emplace_back(line); }) {
        scripts.create();
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns the results.
    std::vector<Value> call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result ? *result : std::vector<Value>{};
    }

    // The first result of `name(args)`; nil when there is none.
    Value first(std::string_view name, const std::vector<Value>& args = {}) {
        const std::vector<Value> results = call(name, args);
        return results.empty() ? Value() : results[0];
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

// CfgLevelName's 18 arguments for record `index` named `name` with level number `number`.
std::vector<Value> levelArgs(double index, const char* name, double number) {
    std::vector<Value> args{Value(index), str(name), str("second"), str(name), str(""), Value(number)};
    for (int i = 0; i < 12; ++i) {
        args.emplace_back(static_cast<double>(i));
    }
    return args;
}

} // namespace

TEST_CASE("the binding table names each binding once and has a function for each", "[script_bindings]") {
    Harness h;
    std::set<std::string_view> names;
    for (const coney::script::BindingInfo& info : coney::script::bindingTable()) {
        INFO(std::string(info.name));
        CHECK(names.insert(info.name).second);
        CHECK(h.scripts.vm().global(info.name).function() != nullptr);
    }
    CHECK(coney::script::bindingCount(BindingKind::Real) + coney::script::bindingCount(BindingKind::Routed) +
              coney::script::bindingCount(BindingKind::Stub) ==
          coney::script::bindingTable().size());
    CHECK(coney::script::bindingCount(BindingKind::Real) > 0);
    // The tolua support globals.
    CHECK(h.scripts.vm().global("tolua").table() != nullptr);
    CHECK(h.scripts.vm().global("NilHandle").number() == 0.0);
}

TEST_CASE("the getters follow tolua's conventions: true is the number 1, false is nil", "[script_bindings]") {
    Harness h;
    h.state.language = coney::Language::French;
    CHECK(h.first("GetPlatform").number() == 1.0);
    CHECK(h.first("isRelease").number() == 1.0);
    CHECK(h.first("UM_IsLevelComplete", {Value(84.0)}).isNil());
    CHECK(h.first("GetLanguage").number() == 2.0);
    CHECK(h.first("GetCurrentLevelIndex").number() == 0.0);
    CHECK(h.first("GetDifficulty").number() == 1.0);
    CHECK(h.first("GetProfileDifficulty").number() == 1.0);
    CHECK(h.first("ToInt", {Value(-2.7)}).number() == -2.0);
    CHECK(h.first("ToInt", {str("3.9")}).number() == 3.0);
    h.call("SetCheckPoint", {Value(2.0)});
    CHECK(h.first("GetCheckPoint").number() == 2.0);
    // A missing argument reads as 0.
    CHECK(h.first("GetLevelId").number() == 0.0);
}

TEST_CASE("CfgLevelName fills the level table that GetLevelId reads", "[script_bindings]") {
    Harness h;
    h.call("CfgLevelName", levelArgs(0, "level100", 100));
    h.call("CfgLevelName", levelArgs(1, "level99", 99));
    REQUIRE(h.state.levels.count() == 2);
    const coney::LevelRecord* frontEnd = h.state.levels.at(0);
    REQUIRE(frontEnd != nullptr);
    CHECK(frontEnd->name == "level100");
    CHECK(frontEnd->secondName == "second");
    CHECK(frontEnd->worldName == "level100");
    CHECK(frontEnd->number == 100.0);
    CHECK(frontEnd->values[11] == 11.0);
    CHECK(h.first("GetLevelId", {Value(1.0)}).number() == 99.0);
    CHECK(h.first("GetLevelId", {Value(5.0)}).number() == 0.0);
    // An index outside the table is logged and ignored.
    h.call("CfgLevelName", levelArgs(200, "levelX", 1));
    CHECK(h.state.levels.count() == 2);
    CHECK(!h.log.empty());
}

TEST_CASE("doFile runs name.lua; ScheduleFunc and FlushScheduledFuncs use the schedule", "[script_bindings]") {
    Harness h;
    h.call("doFile", {str("config_strings_en")});
    CHECK(h.sourced == std::vector<std::string>{"config_strings_en.lua"});
    h.scripts.setTime(1000);
    h.call("ScheduleFunc", {str("Menu.launchRMI"), Value(500.0)});
    h.call("ScheduleFuncArg1", {str("Menu.other"), Value(100.0), Value(3.0)});
    CHECK(h.scripts.scheduled() == 2);
    h.call("FlushScheduledFuncs", {str("Menu.other")});
    CHECK(h.scripts.scheduled() == 1);
    h.call("FlushScheduledFuncs");
    CHECK(h.scripts.scheduled() == 0);
}

TEST_CASE("the front-end bindings reach the host", "[script_bindings]") {
    Harness h;
    h.call("ShowProfileManager", {str("Menu.fadeToRMI"), str("Menu.startGame")});
    h.call("ShowRumbleModeInterface", {str("Menu.cancelRumbleMode"), str("Menu.startRumbleMode"), Value(1.0)});
    h.call("MenuLoadLevel", {str("level99")});
    h.call("PlayMovie", {str("TRAILER")});
    h.call("SoundLoopMusicTrack", {str("music/track")});
    h.call("SoundStopMusicTrack");
    h.call("ScreenQueueEffect", {Value(1.0), Value(0.7)});
    h.call("HUDLaunchMissionComplete", {Value(4.0)});
    CHECK(h.host.requests == std::vector<std::string>{"menus Menu.fadeToRMI Menu.startGame",
                                                      "rumble Menu.cancelRumbleMode Menu.startRumbleMode",
                                                      "level level99", "movie TRAILER", "music music/track", "stop",
                                                      "fade 1 7", "mission 4"});
}

TEST_CASE("stubs return their defaults; recording stubs keep their arguments; strings reach the table",
          "[script_bindings]") {
    Harness h;
    const double firstHandle = h.first("ScenePreload", {str("scene")}).number().value_or(-1.0);
    const double secondHandle = h.first("GetPTank", {str("tank")}).number().value_or(-1.0);
    CHECK(firstHandle >= 1.0);
    CHECK(secondHandle == firstHandle + 1.0);
    CHECK(h.first("SceneIsPreloaded", {Value(firstHandle)}).isNil());
    CHECK(h.call("SetLight").empty());
    h.call("CfgObj", {str("object"), Value(2.0), Value(std::make_shared<coney::script::Table>())});
    REQUIRE(h.recorded.count("CfgObj") == 1);
    const std::vector<Value>& kept = h.recorded.calls("CfgObj")[0];
    REQUIRE(kept.size() == 3);
    CHECK(kept[0].string() == "object");
    CHECK(kept[1].number() == 2.0);
    CHECK(kept[2].isNil()); // a table is not kept
    h.call("CfgHUDMessage", {Value(0x76), str("PRESS THE START BUTTON")});
    CHECK(h.strings.get(0x76) == "PRESS THE START BUTTON");
}

TEST_CASE("HuCreate keeps the human it makes and returns a handle; player 1 is found by index", "[script_bindings]") {
    Harness h;
    // A position table {x, y, z}, as the level scripts pass one.
    const auto position = std::make_shared<coney::script::Table>();
    REQUIRE(position->set(Value(1.0), Value(-284.4)).has_value());
    REQUIRE(position->set(Value(2.0), Value(120.4)).has_value());
    REQUIRE(position->set(Value(3.0), Value(0.3)).has_value());
    // level99's checkpoint 1: Rembrandt as player 1, then Ash as player 2 (docs/research/scripting.md#level99).
    const double rembrandt = h.first("HuCreate", {str("Rembrandt"), Value(32.0), Value(position), Value(0.0),
                                                  str("warr_sw"), Value(1.0), Value(1.0)})
                                 .number()
                                 .value_or(0.0);
    const double ash = h.first("HuCreate", {str("Ash"), Value(40.0), Value(position), Value(235.0), str("warr_sw"),
                                            Value(2.0), Value(1.0)})
                           .number()
                           .value_or(0.0);
    CHECK(rembrandt >= 1.0);
    CHECK(ash == rembrandt + 1.0);
    REQUIRE(h.humans.all().size() == 2);
    const coney::HumanCreation* player = h.humans.player(1);
    REQUIRE(player != nullptr);
    CHECK(player->name == "Rembrandt");
    CHECK(player->type == 32);
    CHECK(player->handle == rembrandt);
    REQUIRE(player->position.has_value());
    CHECK(player->position.value_or(std::array<float, 3>{}) == std::array<float, 3>{-284.4F, 120.4F, 0.3F});
    CHECK(h.humans.player(2)->headingDegrees == 235.0F);
    CHECK(h.humans.player(3) == nullptr);
    // A position from a binding Coney lacks is nil: the human is kept without one.
    h.call("HuCreate", {str("P11"), Value(1.0), Value(), Value(270.0), Value(), Value(1.0)});
    CHECK(h.humans.all().back().name == "P11");
    CHECK(!h.humans.all().back().position.has_value());
}

TEST_CASE("HuCreate returns NilHandle once every human slot is taken", "[script_bindings]") {
    Harness h;
    for (std::size_t i = 0; i < coney::CreatedHumans::kCapacity; ++i) {
        CHECK(h.first("HuCreate", {str("extra"), Value(1.0)}).number().value_or(0.0) >= 1.0);
    }
    CHECK(h.first("HuCreate", {str("one too many"), Value(1.0)}).number() == 0.0);
    CHECK(h.humans.all().size() == coney::CreatedHumans::kCapacity);
}
