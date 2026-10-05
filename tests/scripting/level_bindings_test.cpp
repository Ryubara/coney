// SPDX-License-Identifier: GPL-3.0-or-later
// The level scripts' bindings (docs/research/flags.md, docs/research/scripting.md#errors-in-a-fresh-state): the flags
// and their handles, GetFlagPos's M_Vector4, TeleportToFlag with and without the flag's heading, the saved script
// numbers, the start callback, the Rumble set-up, the game's random numbers and the model HuCreate's type names.
#include "scripting/level_bindings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_random.h"
#include "gui/global_strings.h"
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

// A script system with Coney's bindings over a game state, humans and flags.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans, &flags};
    ScriptSystem scripts;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
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

// A position table {x, y, z}.
Value position(double x, double y, double z) {
    auto table = std::make_shared<Table>();
    REQUIRE(table->set(Value(1.0), Value(x)).has_value());
    REQUIRE(table->set(Value(2.0), Value(y)).has_value());
    REQUIRE(table->set(Value(3.0), Value(z)).has_value());
    return Value(table);
}

// Field `name` of a table value as a number; NaN-free 0 when absent.
double field(const Value& table, const char* name) {
    REQUIRE(table.table() != nullptr);
    return table.table() != nullptr ? table.table()->field(name).number().value_or(-12345.0) : 0.0;
}

} // namespace

TEST_CASE("AddFlag returns a handle from the world objects' counter, and FindFlag and GetFlagPos find the flag",
          "[level_bindings]") {
    Harness h;
    h.call("CfgSetDatabaseSizes", {Value(10.0), Value(5.0), Value()});
    CHECK(h.flags.capacity() == 9);
    const double flag =
        h.first("AddFlag", {str("fWchiefStart_1"), position(-188.6, 95, -194.3), Value(89.0), Value(0.0), Value(0.0)})
            .number()
            .value_or(0.0);
    CHECK(flag >= 1.0);
    // A human made next gets the following handle: one handle space.
    const double human = h.first("HuCreate", {str("Cleon"), Value(1.0), position(0, 0, 0)}).number().value_or(0.0);
    CHECK(human == flag + 1.0);
    CHECK(h.first("FindFlag", {str("fWchiefStart_1")}).number() == flag);
    CHECK(h.first("FindFlag", {str("fwchiefstart_1")}).number() == 0.0);
    const Value where = h.first("GetFlagPos", {Value(flag)});
    CHECK(static_cast<float>(field(where, "x")) == -188.6F);
    CHECK(static_cast<float>(field(where, "y")) == 95.0F);
    CHECK(static_cast<float>(field(where, "z")) == -194.3F);
    CHECK(field(where, "w") == 1.0);
    // A handle that names no flag: nil.
    CHECK(h.first("GetFlagPos", {Value(999.0)}).isNil());
    // GetPosition answers for a human, a flag, and the origin for anything else.
    CHECK(field(h.first("GetPosition", {Value(flag)}), "y") == 95.0);
    CHECK(field(h.first("GetPosition", {Value(999.0)}), "x") == 0.0);
}

TEST_CASE("TeleportToFlag puts a human on the flag with the flag's heading for -1, or the one given",
          "[level_bindings]") {
    Harness h;
    const double flag =
        h.first("AddFlag", {str("fP1_01"), position(-9.1, 8.9, -11.1), Value(128.0)}).number().value_or(0.0);
    const double human =
        h.first("HuCreate", {str("P11"), Value(1.0), position(0, 0, 0), Value(270.0), Value(), Value(1.0)})
            .number()
            .value_or(0.0);
    h.call("TeleportToFlag", {Value(human), Value(flag), Value(-1.0)});
    const coney::HumanCreation* player = h.humans.player(1);
    REQUIRE(player != nullptr);
    if (player == nullptr) {
        return;
    }
    // Where the last teleport put the player; a placement far away when there was none.
    const auto placed = [player] {
        return player->teleported.value_or(coney::world_objects::Placement{.position = {}, .headingDegrees = -1.0F});
    };
    CHECK(placed().headingDegrees == 128.0F);
    CHECK(placed().position == std::array<float, 3>{-9.1F, 8.9F, -11.1F});
    CHECK(player->teleports == 1);
    // A given heading is used as whole degrees; no heading is the flag's.
    h.call("TeleportToFlag", {Value(human), Value(flag), Value(45.7)});
    CHECK(placed().headingDegrees == 45.0F);
    h.call("TeleportToFlag", {Value(human), Value(flag)});
    CHECK(placed().headingDegrees == 128.0F);
    CHECK(player->teleports == 3);
    // Anything but a human and a flag is left alone.
    h.call("TeleportToFlag", {Value(flag), Value(human)});
    CHECK(player->teleports == 3);
}

TEST_CASE("the saved script numbers read 0 until written, and only slots 1 to 8 exist", "[level_bindings]") {
    Harness h;
    CHECK(h.first("GetLUASaveDataFloat", {Value(1.0)}).number() == 0.0);
    h.call("SetLUASaveDataFloat", {Value(1.0), Value(1234.5)});
    h.call("SetLUASaveDataFloat", {Value(9.0), Value(7.0)});
    CHECK(h.first("GetLUASaveDataFloat", {Value(1.0)}).number() == 1234.5);
    CHECK(h.state.luaSaveFloats.at(0) == 1234.5F);
    CHECK(h.first("GetLUASaveDataFloat", {Value(9.0)}).number() == 0.0);
    CHECK(h.first("GetLUASaveDataFloat", {Value(0.0)}).number() == 0.0);
}

TEST_CASE("SetStartGameCallback keeps 31 characters of the name, and GetRumbleModeData fills 23 values",
          "[level_bindings]") {
    Harness h;
    h.call("SetStartGameCallback", {str("DoRules")});
    CHECK(h.state.startGameCallback == "DoRules");
    h.call("SetStartGameCallback", {str("A_start_callback_name_of_forty_chars____")});
    CHECK(h.state.startGameCallback.size() == 31);
    h.call("SetStartGameCallback", {Value()});
    CHECK(h.state.startGameCallback.empty());

    for (std::size_t i = 0; i < h.state.rumble.values.size(); ++i) {
        h.state.rumble.values.at(i) = static_cast<std::uint16_t>(i * 3);
    }
    auto data = std::make_shared<Table>();
    REQUIRE(data->set(Value(1.0), str("overwritten")).has_value());
    h.call("GetRumbleModeData", {Value(data)});
    CHECK(data->size() == 23);
    CHECK(data->get(Value(1.0)).number() == 0.0);
    CHECK(data->get(Value(23.0)).number() == 66.0);
}

TEST_CASE("GetRumbleModeGangName returns side 1's gang for 1 and side 2's for any other side", "[level_bindings]") {
    Harness h;
    // Empty until the gang screen is confirmed.
    CHECK(h.first("GetRumbleModeGangName", {Value(1.0)}).string() == "");
    h.state.rumble.gangNames = {"BASEBALL FURIES", "ORPHANS"};
    CHECK(h.first("GetRumbleModeGangName", {Value(1.0)}).string() == "BASEBALL FURIES");
    CHECK(h.first("GetRumbleModeGangName", {Value(1.5)}).string() == "BASEBALL FURIES");
    CHECK(h.first("GetRumbleModeGangName", {Value(2.0)}).string() == "ORPHANS");
    CHECK(h.first("GetRumbleModeGangName", {Value(0.0)}).string() == "ORPHANS");
}

TEST_CASE("random draws from the game state's generator, shared by every Lua state", "[level_bindings]") {
    Harness h;
    std::vector<std::uint32_t> table(coney::GameRandom::kTableSize, 0);
    table[1] = 7; // 1 + 7 mod 5 = 3
    table[2] = 9; // the second state's draw: 1 + 9 mod 5 = 5
    h.state.random.setTable(table);
    CHECK(h.first("random", {Value(1.0), Value(5.0)}).number() == 3.0);
    h.scripts.create();
    CHECK(h.first("random", {Value(1.0), Value(5.0)}).number() == 5.0);
}

TEST_CASE("HuCreate resolves the model its type is drawn as from the recorded CfgChar calls", "[level_bindings]") {
    Harness h;
    // CfgChar(type, ...) with the model name as its tenth argument; levels 0 and 1 numbered 5 and 62.
    for (const auto& [type, model] : {std::pair{1.0, "warr_cl"}, std::pair{2.0, "warr_cl_gen"}}) {
        std::vector<Value> args(12);
        args[0] = Value(type);
        args[9] = str(model);
        h.recorded.add("CfgChar", args);
    }
    coney::LevelRecord level;
    level.id = 0;
    level.name = "level5";
    level.number = 5;
    REQUIRE(h.state.levels.set(level));
    h.call("HuCreate", {str("Cleon"), Value(2.0), position(0, 0, 0), Value(0.0), Value(), Value(1.0)});
    h.call("HuCreate", {str("Gen"), Value(2.0), position(0, 0, 0), Value(0.0), Value(), Value(0.0)});
    h.call("HuCreate", {str("Nobody"), Value(500.0), position(0, 0, 0), Value(0.0), Value(), Value(0.0)});
    REQUIRE(h.humans.all().size() == 3);
    CHECK(h.humans.all()[0].model == "warr_cl");
    CHECK(h.humans.all()[1].model == "warr_cl_gen");
    CHECK(h.humans.all()[2].model.empty());
}
