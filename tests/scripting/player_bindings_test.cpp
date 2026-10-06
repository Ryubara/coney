// SPDX-License-Identifier: GPL-3.0-or-later
// The bindings over the players' state (docs/references/bindings/level.md, config.md, input.md): the inventory and
// money, the statistics, the unlockables, the stopwatch, crime reporting, the pad handlers and the store colour, run
// through a script system with Coney's bindings. Synthetic values; a native `Record` keeps its arguments.
#include "scripting/player_bindings.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/error.h"
#include "core/pad.h"
#include "core/pads.h"
#include "gamemodes/level_object_services.h"
#include "gamemodes/level_start.h"
#include "gamemodes/player_frame.h"
#include "gui/global_strings.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Table;
using coney::script::Value;
namespace item = coney::item;

namespace {

// A host that ignores every request: these bindings ask nothing of it.
class QuietHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// A human the level script made: `player` 1 or 2 for a player's, 0 for an AI human.
coney::HumanCreation human(const char* name, int player, double handle) {
    coney::HumanCreation made;
    made.name = name;
    made.playerIndex = player;
    made.handle = handle;
    return made;
}

// A script system with Coney's bindings over a game state and humans, and a native `Record`.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans};
    ScriptSystem scripts;
    std::vector<std::vector<Value>> recordedArgs;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        scripts.create();
        scripts.vm().registerFunction("Record", [this](std::span<const Value> args) -> coney::script::binding::Results {
            recordedArgs.emplace_back(args.begin(), args.end());
            return std::vector<Value>{};
        });
        // Player 1's human is handle 10, an AI human handle 11.
        REQUIRE(humans.add(human("Cleon", 1, 10)));
        REQUIRE(humans.add(human("Civilian", 0, 11)));
    }

    // The first result of the binding `name` with `args`; nil when there is none. REQUIREs success.
    Value first(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result && !result->empty() ? (*result)[0] : Value();
    }
    // The first result as a number; -1 for nil.
    double number(std::string_view name, const std::vector<Value>& args = {}) {
        return first(name, args).number().value_or(-1.0);
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

} // namespace

TEST_CASE("every player binding is real in the table", "[player_bindings]") {
    for (const std::string_view name : coney::script::kPlayerBindings) {
        const auto table = coney::script::bindingTable();
        const auto found = std::ranges::find(table, name, &coney::script::BindingInfo::name);
        REQUIRE(found != table.end());
        CHECK(found->kind == coney::script::BindingKind::Real);
    }
}

TEST_CASE("money: give, take, set and get by player, 1 by default", "[player_bindings]") {
    Harness h;
    h.first("GiveMoney", {Value(15.0)});
    h.first("GiveMoney", {Value(7.9), Value(2.0)});
    h.first("GiveMoney", {Value(100.0), Value(3.0)}); // no player 3
    CHECK(h.number("InvGetMoney") == 15);
    CHECK(h.number("InvGetMoney", {Value(2.0)}) == 7);
    h.first("TakeMoney", {Value(20.0)});
    CHECK(h.number("InvGetMoney", {Value(1.0)}) == 0);
    h.first("InvSetMoney", {Value(250.0), Value(1.0)});
    CHECK(h.state.player.inventory.count(0, item::kMoney) == 250);
    CHECK(h.state.player.inventory.moneySets(0) == 1);
}

TEST_CASE("GiveMoney runs the money callback with the 0-based player and the amount", "[player_bindings]") {
    Harness h;
    h.state.player.moneyCallback = "Record";
    h.first("GiveMoney", {Value(5.0), Value(2.0)});
    REQUIRE(h.recordedArgs.size() == 1);
    CHECK(h.recordedArgs[0][0].number() == 1.0);
    CHECK(h.recordedArgs[0][1].number() == 5.0);
}

TEST_CASE("an item added with notify calls the money, item and human callbacks, in that order", "[player_bindings]") {
    Harness h;
    h.scripts.vm().registerFunction("Money", [&h](std::span<const Value> args) -> coney::script::binding::Results {
        h.recordedArgs.push_back({str("money"), args[0], args[1]});
        return std::vector<Value>{};
    });
    h.scripts.vm().registerFunction("Item", [&h](std::span<const Value> args) -> coney::script::binding::Results {
        h.recordedArgs.push_back({str("item"), args[0]});
        return std::vector<Value>{};
    });
    h.first("CfgMoneyCallback", {str("Money")});
    h.first("CfgInventoryCallback", {str("Item")});
    h.first("CfgHuInventoryCallback", {str("Record")});
    // Loot with notify: the item callbacks; then its money without: the money callback alone.
    coney::script::addInventoryItem(h.scripts, h.state, 0, item::kStolenLoot, 1, true);
    coney::script::addInventoryItem(h.scripts, h.state, 0, item::kMoney, 7, false);
    REQUIRE(h.recordedArgs.size() == 3);
    CHECK(h.recordedArgs[0][0].string() == "item");
    CHECK(h.recordedArgs[0][1].number() == item::kStolenLoot);
    CHECK(h.recordedArgs[1][0].number() == 0.0); // Record(player, item)
    CHECK(h.recordedArgs[1][1].number() == item::kStolenLoot);
    CHECK(h.recordedArgs[2][0].string() == "money");
    CHECK(h.recordedArgs[2][2].number() == 7.0);
    CHECK(h.state.player.inventory.count(0, item::kStolenLoot) == 1);
    CHECK(h.state.player.inventory.count(0, item::kMoney) == 7);
    // A bad player or item changes nothing and calls nothing; a callback naming no function is skipped.
    coney::script::addInventoryItem(h.scripts, h.state, 2, item::kMoney, 7, true);
    coney::script::addInventoryItem(h.scripts, h.state, 0, 23, 1, true);
    h.first("CfgInventoryCallback", {str("NoSuchFunction")});
    h.first("CfgHuInventoryCallback", {str("NoSuchFunction")});
    coney::script::addInventoryItem(h.scripts, h.state, 1, item::kSprayPaint, 2, true);
    CHECK(h.recordedArgs.size() == 3);
    CHECK(h.state.player.inventory.count(1, item::kSprayPaint) == 2);
    CHECK(h.scripts.errors() == 0);
}

TEST_CASE("items, revives, keys and spray paint", "[player_bindings]") {
    Harness h;
    h.first("CfgInventoryItem",
            {str("dyn_revival"), Value(1.0), Value(0.0), str("vags/interface/powerup"), Value(30000.0)});
    CHECK(h.state.player.inventory.slot(1, 1)->objectName == "dyn_revival");
    h.first("InvGiveRevive", {Value(5.0)});
    CHECK(h.number("InvNumberRevives") == 3);
    h.first("InvGiveSkeletonKey", {Value(2.0), Value(2.0)});
    CHECK(h.number("InvNumberSkeletonKeys", {Value(2.0)}) == 2);
    CHECK(h.number("InvNumberOf", {Value(6.0), Value(2.0)}) == 2);
    h.first("InvGiveItem", {Value(11.0), Value(1.0)});
    CHECK(h.first("InvPlayerHasItem", {Value(11.0)}).number() == 1.0);
    CHECK(h.first("InvPlayerHasItem", {Value(12.0)}).isNil());
    h.first("InvSetSpraycanCharges", {Value(4.0)});
    CHECK(h.number("InvGetSpraycanCharges") == 4);
    CHECK(h.state.player.inventory.sprayHintPending());
    h.first("CfgInventoryCallback", {str("P2.CountLoot")});
    CHECK(h.state.player.pickupCallback == "P2.CountLoot");
}

TEST_CASE("the revive upgrade, once unlocked, lets a player carry four", "[player_bindings]") {
    Harness h;
    h.first("UM_SetNumUnlockables", {Value(2.0)});
    h.first("UM_SetUnlockable", {Value(0.0), Value(99.0), Value(0.0), Value(0.0), Value(6.0), Value(0.0), Value(7.0)});
    h.first("UM_Unlock", {Value(99.0), Value(0.0), Value(0.0)});
    CHECK(h.first("UM_IsDataUnlocked", {Value(6.0), Value(7.0)}).number() == 1.0);
    CHECK(h.first("UM_IsLevelComplete", {Value(99.0)}).number() == 1.0);
    CHECK(h.number("UM_GetRecordData", {Value(0.0), Value(5.0)}) == 7);
    h.first("InvGiveRevive", {Value(9.0)});
    CHECK(h.number("InvNumberRevives") == 4);
    // The unlock is in the profile's bits, which a save carries.
    CHECK_FALSE(h.state.saved.isLocked(0));
    CHECK(h.first("UM_IsTypeDirty", {Value(6.0)}).number() == 1.0);
    CHECK(h.first("UM_IsTypeDirty", {Value(6.0)}).isNil());
}

TEST_CASE("statistics by a player's human; other handles do nothing", "[player_bindings]") {
    Harness h;
    h.first("CfgSetStatValue", {Value(0.0), Value(0.0), Value(1000.0)});
    h.first("CfgSetStatValue", {Value(3.0), Value(2.0), Value(1.0)});
    h.first("StatAdd", {Value(10.0), Value(0.0), Value(0.0)});
    h.first("StatAdd", {Value(10.0), Value(3.0), Value(2.0), Value(12.0)});
    h.first("StatAdd", {Value(11.0), Value(0.0), Value(0.0)}); // an AI human
    CHECK(h.number("StatGetScore", {Value(10.0)}) == 1012);
    CHECK(h.number("StatGetScore", {Value(11.0)}) == 0);
    h.first("CfgSetStatTypeMax", {Value(5000.0), Value(3000.0), Value(4000.0), Value(2000.0), Value(10000.0)});
    CHECK(h.state.player.stats.maximum(coney::StatCategory::Harmony) == 4000);
    CHECK(h.state.player.stats.maximum(coney::StatCategory::Mission) == 10000);
    h.first("StatResetPlayer", {Value(10.0)});
    CHECK(h.number("StatGetScore", {Value(10.0)}) == 0);
}

TEST_CASE("the stopwatch runs in the player frame and calls its callback at the target", "[player_bindings]") {
    Harness h;
    const coney::Pads pads;
    h.scripts.setTime(1000);
    h.first("W_SetStopWatch", {Value(500.0), Value(0.0), str("Record")});
    h.first("W_StartStopWatch", {Value(1.0)});
    coney::runPlayerFrame(h.state, h.scripts, pads, 1400);
    CHECK(h.number("W_GetStopWatchTime") == 100);
    CHECK(h.recordedArgs.empty());
    coney::runPlayerFrame(h.state, h.scripts, pads, 1600);
    CHECK(h.number("W_GetStopWatchTime") == 0);
    CHECK(h.recordedArgs.size() == 1);
}

TEST_CASE("pad handlers fire for buttons pressed on their port", "[player_bindings]") {
    Harness h;
    h.first("PadSetHandler", {Value(0.0), Value(static_cast<double>(coney::pad::kCross)), str("Record")});
    h.first("PadSetHandler", {Value(1.0), Value(static_cast<double>(coney::pad::kCircle)), str("Record")});
    h.first("PadSetHandlerEx", {str("P1.OnTarget")});
    CHECK(h.state.player.pads.targetHandler() == "P1.OnTarget");

    coney::Pads pads;
    coney::PortSamples samples{};
    samples[0].connected = true;
    samples[0].buttons = coney::pad::kCross;
    pads.update(samples);
    coney::runPlayerFrame(h.state, h.scripts, pads, 0);
    CHECK(h.recordedArgs.size() == 1);
    // Still held: not pressed again. Circle on port 1 has no handler there.
    samples[0].buttons = coney::pad::kCross | coney::pad::kCircle;
    pads.update(samples);
    coney::runPlayerFrame(h.state, h.scripts, pads, 0);
    CHECK(h.recordedArgs.size() == 1);
}

TEST_CASE("crime reporting, the stereo handler, joining and the store colour", "[player_bindings]") {
    Harness h;
    CHECK(h.state.player.crimes.reporting());
    h.first("ReportCrime", {Value()});
    CHECK_FALSE(h.state.player.crimes.reporting());
    h.first("CfgSetSteroTheftHandler", {str("P2.StereoStolen")});
    CHECK(h.state.player.stereoTheftHandler == "P2.StereoStolen");
    h.first("CfgMultiplayerJoin", {Value(1.0)});
    CHECK(h.state.player.multiplayerJoin);

    auto colour = std::make_shared<Table>();
    for (int i = 1; i <= 4; ++i) {
        REQUIRE(colour->set(Value(static_cast<double>(i)), Value(0.25 * i)).has_value());
    }
    h.first("EnterStore", {Value(colour)});
    CHECK(h.state.player.storeTint.preset == coney::StoreTint::kInStore);
    CHECK(h.state.player.storeTint.colour[3] == 1.0F);
    h.first("ExitStore");
    CHECK(h.state.player.storeTint.preset == coney::StoreTint::kOutside);
}

TEST_CASE("SetCheckPoint takes the checkpoint copy a restart puts back", "[player_bindings]") {
    Harness h;
    h.first("GiveMoney", {Value(30.0)});
    h.first("SetCheckPoint", {Value(2.0)});
    h.first("GiveMoney", {Value(70.0)});
    h.state.player.restoreCheckpoint();
    CHECK(h.number("InvGetMoney") == 30);
}

TEST_CASE("the level's objects report crimes and score statistics through the players' state", "[player_bindings]") {
    Harness h;
    coney::world_objects::WorldFlags flags;
    flags.createPool(4);
    const double scene = flags.add(50.0, coney::kCrimeSceneFlag, {0.0F, 0.0F, 0.0F}, 0.0F).handle;
    coney::LevelObjectServices services(h.scripts, flags, nullptr);
    // Without the players, nothing is reported.
    services.reportCrime(1, coney::anim::Vec3{1.0F, 2.0F, 3.0F}, 10.0);
    CHECK(flags.find(scene)->position[0] == 0.0F);

    services.setPlayers(&h.state, &h.humans);
    h.state.player.crimes.setCallback("Record");
    services.reportCrime(1, coney::anim::Vec3{1.0F, 2.0F, 3.0F}, 10.0);
    CHECK(flags.find(scene)->position[2] == 3.0F); // the CrimeScene flag follows the crime
    CHECK(h.recordedArgs.empty());                 // no gang in Coney's levels yet: no callback

    h.state.player.stats.setPoints(4, 10, 50);
    services.countPaneBroken(10.0);
    services.countPaneBroken(11.0); // an AI human scores nothing
    services.scoreEvent(10.0, 1, 3);
    CHECK(h.state.player.stats.count(0, 4, 10) == 1);
    CHECK(h.state.player.stats.count(0, 1, 3) == 1);
    CHECK(h.state.player.stats.score(0) == 50);
}
