// SPDX-License-Identifier: GPL-3.0-or-later
// The hub's bindings (scripting/hub_bindings.h, scripting/hub_world_bindings.h): the human getters and switches handed
// to the hub host, the cuffs, names and workout settings, the save flags, the sprite batches, the configuration and
// the zone, store and object queries. Synthetic scripts and objects.
#include "scripting/hub_bindings.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "human/locomotion.h"
#include "scripting/ai_bindings.h"
#include "scripting/hub_world_bindings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "warriors/inventory.h"
#include "world_objects/spawn_records.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A host that keeps the screens and saves the hub asks for.
class KeepingHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
    void showMissionSelect(std::string_view onCancel, std::string_view onChoose) override {
        missionSelect = std::string(onCancel) + "|" + std::string(onChoose);
    }
    void startSaveSequence() override { ++saves; }
    std::string missionSelect;
    int saves = 0;
};

// A hub host with one player (handle 1) and one AI human (handle 2), which keeps what it is asked.
class KeepingHub final : public coney::script::HubBindingHost {
  public:
    [[nodiscard]] std::optional<coney::script::HubHumanStatus> status(double handle) const override {
        if (handle == 1.0) {
            return coney::script::HubHumanStatus{
                .money = 40, .position = {1.0F, 2.0F, 3.0F}, .dead = false, .player = true, .playerIndex = 0};
        }
        if (handle == 2.0) {
            return coney::script::HubHumanStatus{
                .money = 7, .position = {}, .dead = true, .player = false, .playerIndex = -1};
        }
        return std::nullopt;
    }
    [[nodiscard]] bool alive(double handle) const override { return handle == 1.0 || handle == 2.0; }
    void addCuffs(double human, int count) override { cuffs.push_back({human, count}); }
    void setScale(double /*human*/, float value) override { scale = value; }
    void attachGear(double /*human*/, bool on, bool /*knuckles*/, bool /*boots*/) override { gear = on; }
    void setPedReaction(double /*human*/, int value) override { reaction = value; }
    void workout(const coney::script::WorkoutCall& call) override { workouts.push_back(call); }
    void canUseWorldFlags(double /*human*/, bool on, int value) override {
        worldFlags = on;
        chance = value;
    }

    struct Cuffs {
        double human;
        int count;
    };
    std::vector<Cuffs> cuffs;
    float scale = 0.0F;
    bool gear = false;
    int reaction = 0;
    std::vector<coney::script::WorkoutCall> workouts;
    bool worldFlags = false;
    int chance = 0;
};

// An AI host whose hub is KeepingHub.
class HubAi final : public coney::script::AiBindingHost {
  public:
    void goalMoveToFlag(const coney::script::MoveToFlagCall& /*call*/) override {}
    void actLookAt(const coney::script::LookAtCall& /*call*/) override {}
    coney::script::HubBindingHost* hub() override { return &keeping; }
    KeepingHub keeping;
};

// A script system with Coney's bindings over created humans, spawn records and the hub host.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    KeepingHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::SpawnRecords records;
    HubAi ai;
    coney::script::BindingContext context{&state, &strings, &host, &recorded};
    ScriptSystem scripts;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.humans = &humans;
        context.spawnRecords = &records;
        context.ai = &ai;
        scripts.create();
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns its results.
    std::vector<Value> call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return *result;
    }

    // Calls the binding `name` and returns its first result as a number.
    double number(std::string_view name, const std::vector<Value>& args = {}) {
        const std::vector<Value> results = call(name, args);
        REQUIRE_FALSE(results.empty());
        return results.front().number().value_or(-1.0);
    }

    // Calls the binding `name` and returns its first result as a boolean.
    bool boolean(std::string_view name, const std::vector<Value>& args = {}) {
        const std::vector<Value> results = call(name, args);
        REQUIRE_FALSE(results.empty());
        return !results.front().isNil();
    }
};

} // namespace

TEST_CASE("The hub's human getters read the hub host; an unknown handle reads as dead with no money",
          "[hub_bindings]") {
    Harness h;
    CHECK(h.number("HuGetMoney", {Value(1.0)}) == 40.0);
    CHECK(h.number("HuGetMoney", {Value(9.0)}) == 0.0);
    CHECK_FALSE(h.boolean("HuIsDead", {Value(1.0)}));
    CHECK(h.boolean("HuIsDead", {Value(2.0)}));
    CHECK(h.boolean("HuIsDead", {Value(9.0)}));
    CHECK(h.boolean("ObjIsAlive", {Value(2.0)}));
    CHECK_FALSE(h.boolean("ObjIsAlive", {Value(9.0)}));
    // An unknown human has no voice set: -1 read as unsigned.
    CHECK(h.number("HuGetVoiceIndex", {Value(9.0)}) == 4294967295.0);
}

TEST_CASE("HuGiveCuffs gives a player inventory item 5 and an AI human its own count", "[hub_bindings]") {
    Harness h;
    h.call("HuGiveCuffs", {Value(1.0), Value(2.0)});
    CHECK(h.state.player.inventory.count(0, coney::item::kHandcuffs) == 2);
    h.call("HuGiveCuffs", {Value(2.0), Value(3.0)});
    REQUIRE(h.ai.keeping.cuffs.size() == 1);
    CHECK(h.ai.keeping.cuffs.front().count == 3);
    h.call("HuGiveCuffs", {Value(9.0), Value(3.0)}); // no such human: nothing
    CHECK(h.ai.keeping.cuffs.size() == 1);
}

TEST_CASE("HuSetName keeps 15 characters; the switches reach the hub host", "[hub_bindings]") {
    Harness h;
    coney::HumanCreation bum;
    bum.name = "Bum";
    bum.type = 3;
    bum.handle = 5.0;
    REQUIRE(h.humans.add(bum));
    h.call("HuSetName", {Value(5.0), Value(std::string("AVeryLongNameIndeed"))});
    CHECK(h.humans.find(5.0)->name == "AVeryLongNameIn");

    h.call("HuSetScale", {Value(2.0), Value(1.25)});
    CHECK(h.ai.keeping.scale == 1.25F);
    // Only exactly 1 attaches the gear.
    h.call("HuAttachGear", {Value(1.0), Value(1.0)});
    CHECK(h.ai.keeping.gear);
    h.call("HuAttachGear", {Value(1.0), Value(2.0)});
    CHECK_FALSE(h.ai.keeping.gear);
    // The reaction is read as 16 bits, sign-extended.
    h.call("HuSetPedReaction", {Value(2.0), Value(65535.0)});
    CHECK(h.ai.keeping.reaction == -1);
    // BrCanUseWorldFlags' chance defaults when it is left out.
    h.call("BrCanUseWorldFlags", {Value(2.0), Value(1.0), Value(30.0)});
    CHECK(h.ai.keeping.worldFlags);
    CHECK(h.ai.keeping.chance == 30);
}

TEST_CASE("HuWorkout passes its five clips; the workout settings are kept in the game state", "[hub_bindings]") {
    Harness h;
    h.call("HuWorkout", {Value(1.0), Value(8.0), Value(std::string("a")), Value(std::string("b")),
                         Value(std::string("c")), Value(std::string("d")), Value(std::string("e"))});
    REQUIRE(h.ai.keeping.workouts.size() == 1);
    CHECK(h.ai.keeping.workouts.front().equipment == 8.0);
    CHECK(h.ai.keeping.workouts.front().clips.at(4) == "e");

    h.call("HuSetWorkoutCallbacks",
           {Value(std::string("onStart")), Value(std::string("onRep")), Value(std::string("onEnd"))});
    CHECK(h.state.hub.workout.onRep == "onRep");
}

TEST_CASE("SetLUASaveDataBool keeps flags 1-128 in the saved progress and others unsaved", "[hub_bindings]") {
    Harness h;
    h.call("SetLUASaveDataBool", {Value(12.0), Value(1.0)});
    CHECK(h.state.saved.scriptFlag(12));
    CHECK(h.boolean("GetLUASaveDataBool", {Value(12.0)}));
    h.call("SetLUASaveDataBool", {Value(200.0), Value(1.0)});
    CHECK(h.state.hub.unsavedFlags.at(200));
    CHECK(h.boolean("GetLUASaveDataBool", {Value(200.0)}));
    CHECK_FALSE(h.boolean("GetLUASaveDataBool", {Value(201.0)}));
}

TEST_CASE("GetPTank hands out slots from 1, shifted by 16; ReleasePTank frees a slot for reuse", "[hub_bindings]") {
    Harness h;
    const double first = h.number("GetPTank");
    const double second = h.number("GetPTank");
    CHECK(first == 65536.0);
    CHECK(second == 131072.0);
    h.call("ReleasePTank", {Value(first)});
    CHECK(h.number("GetPTank") == first);
}

TEST_CASE("The hub's configuration bindings set the game state", "[hub_bindings]") {
    Harness h;
    h.call("CfgActionDistance", {Value(1.0), Value(3.0)});
    CHECK(h.state.hub.actionDistanceSquared.at(1) == 9.0F);
    h.call("CfgActionDistance", {Value(9.0), Value(3.0)}); // no such action: ignored
    h.call("CfgEnableTurfInvasion", {Value()});
    CHECK_FALSE(h.state.hub.turfInvasion);
    h.call("CfgPlayerCombatWalkOnly", {Value(1.0)});
    CHECK(h.state.hub.playerCombatWalkOnly);
    h.call("CfgObjectValueMod", {Value(0.5)});
    CHECK(h.state.hub.objectValueFactor == 0.5F);

    // The run threshold is game-wide: put it back after.
    const float before = coney::human::locomotionTuning().runThreshold;
    h.call("CfgStickDeflection", {Value(0.2), Value(0.75)});
    CHECK(coney::human::locomotionTuning().runThreshold == 0.75F);
    coney::human::locomotionTuning().runThreshold = before;
}

TEST_CASE("The mission select keeps 31 characters of each callback; the save sequence reaches the host",
          "[hub_bindings]") {
    Harness h;
    const std::string longName(40, 'x');
    h.call("HUDShowMissionSelect", {Value(std::string("Cancel")), Value(longName)});
    CHECK(h.host.missionSelect == "Cancel|" + std::string(31, 'x'));
    h.call("SSMC_StartSaveSequence");
    CHECK(h.host.saves == 1);
}

TEST_CASE("ObjMarkZone removes a zone's records; GetObjectName names a spawn record", "[hub_bindings]") {
    Harness h;
    coney::world_objects::SpawnRecord crate;
    crate.handle = 9;
    crate.typeName = "dyn_crate";
    crate.zone = 4;
    REQUIRE(h.records.add(crate) != nullptr);
    const std::vector<Value> name = h.call("GetObjectName", {Value(9.0)});
    REQUIRE_FALSE(name.empty());
    CHECK(name.front().string() == "dyn_crate");
    const std::vector<Value> none = h.call("GetObjectName", {Value(77.0)});
    REQUIRE_FALSE(none.empty());
    CHECK(none.front().string() == "<null>");
    h.call("ObjMarkZone", {Value(4.0), Value(0.0), Value(1.0)});
    CHECK(h.records.find(9)->removed);
}
