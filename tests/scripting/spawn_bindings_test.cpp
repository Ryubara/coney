// SPDX-License-Identifier: GPL-3.0-or-later
// The object database and the dynamic objects' spawn records (docs/research/objects.md#dynamic-objects): CfgObj's
// types and their model hashes, ObjSpawn's records and handles, the suppressed keys, the pool CfgSetDatabaseSizes
// makes, resolving and pinning a record, and a fresh state's empty records but kept types.
#include "scripting/spawn_bindings.h"

#include <array>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/name_hash.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Table;
using coney::script::Value;
using coney::world_objects::SpawnRecord;

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

// A script system with Coney's bindings over an object database and spawn records.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::world_objects::ObjectTypes types;
    coney::world_objects::SpawnRecords records;
    coney::script::BindingContext context;
    ScriptSystem scripts;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.recorded = &recorded;
        context.objectTypes = &types;
        context.spawnRecords = &records;
        scripts.create();
    }

    // The first result of `name(args)`; REQUIREs success, nil when there is none.
    Value first(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result && !result->empty() ? (*result)[0] : Value();
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

// A table of numbers at 1, 2, ...
Value list(std::initializer_list<double> numbers) {
    auto table = std::make_shared<Table>();
    double key = 1;
    for (const double n : numbers) {
        REQUIRE(table->set(Value(key), Value(n)).has_value());
        key += 1;
    }
    return Value(table);
}

} // namespace

TEST_CASE("CfgObj adds a type found by name, its model hash the CRC-32 of the name", "[spawn_bindings]") {
    Harness h;
    h.first("CfgObj", {str("dyn_s_wwheel_a"), str("simple_object"), Value(50.0)});
    const coney::world_objects::ObjectType* wheel = h.types.find("dyn_s_wwheel_a");
    REQUIRE(wheel != nullptr);
    if (wheel == nullptr) {
        return;
    }
    CHECK(wheel->className == "simple_object");
    CHECK(wheel->hitpoints == 50);
    CHECK(wheel->modelHash == coney::crc32(std::string_view("dyn_s_wwheel_a")));
    CHECK(h.types.find("dyn_s_wwcart_simple_a") == nullptr);
    // Still recorded, for what reads the other arguments.
    CHECK(h.recorded.count("CfgObj") == 1);
}

TEST_CASE("the object database keeps the first type of a name and cuts long names", "[spawn_bindings]") {
    coney::world_objects::ObjectTypes types;
    types.add("crate", "simple_object", 1);
    types.add("crate", "other_object", 2);
    REQUIRE(types.find("crate") != nullptr);
    CHECK(types.find("crate")->hitpoints == 1);
    CHECK(types.all().size() == 2);
    const std::string longName(40, 'x');
    CHECK(types.add(longName, "simple_object", 0).name.size() == coney::world_objects::ObjectTypes::kMaxName);
}

TEST_CASE("ObjSpawn adds a spawn record and returns its handle; the object is not live yet", "[spawn_bindings]") {
    Harness h;
    const Value handle = h.first("ObjSpawn", {str("dyn_s_wwheel_a"), list({515.51, -68.88, -188.65}),
                                              list({0, 0, 0.707107, -0.707107}), Value(-1.0), Value(3.0), Value(64.0),
                                              Value(static_cast<double>(0x888888FFU)), str("fWheel")});
    REQUIRE(handle.number().has_value());
    const SpawnRecord* record = h.records.find(handle.number().value_or(0));
    REQUIRE(record != nullptr);
    if (record == nullptr) {
        return;
    }
    CHECK(record->typeName == "dyn_s_wwheel_a");
    CHECK(record->position == std::array<float, 3>{515.51F, -68.88F, -188.65F});
    CHECK(record->rotation == std::array<float, 4>{0.0F, 0.0F, 0.707107F, -0.707107F});
    CHECK(record->zone == 3);
    CHECK(record->flags == 64);
    CHECK(record->flagName == "fWheel");
    CHECK(record->tint == 0x888888FFU);
    CHECK_FALSE(record->live);
    CHECK_FALSE(record->pinned);
}

TEST_CASE("ObjSpawn's tint is the seventh argument, white without one", "[spawn_bindings]") {
    Harness h;
    const double tinted = h.first("ObjSpawn", {str("dyn_s_wwheel_a"), list({0, 0, 0}), list({0, 0, 0, 1}), Value(-1.0),
                                               Value(0.0), Value(0.0), Value(static_cast<double>(0x474542FFU))})
                              .number()
                              .value_or(0);
    const double plain =
        h.first("ObjSpawn", {str("dyn_s_neon_a"), list({0, 0, 0}), list({0, 0, 0, 1})}).number().value_or(0);
    REQUIRE(h.records.find(tinted) != nullptr);
    REQUIRE(h.records.find(plain) != nullptr);
    CHECK(h.records.find(tinted)->tint == 0x474542FFU);
    CHECK(h.records.find(plain)->tint == 0xFFFFFFFFU);
    CHECK(plain == tinted + 1);
}

TEST_CASE("ObjSpawn suppresses keys and the power cuffs", "[spawn_bindings]") {
    Harness h;
    CHECK(h.first("ObjSpawn", {str("dyn_key_door"), list({0, 0, 0}), list({0, 0, 0, 1})}) == Value(0.0));
    CHECK(h.first("ObjSpawn", {str("dyn_powercuffs"), list({0, 0, 0}), list({0, 0, 0, 1})}) == Value(0.0));
    CHECK(h.records.all().empty());
    CHECK(coney::world_objects::spawnAllowed("dyn_s_wwheel_a"));
}

TEST_CASE("CfgSetDatabaseSizes makes room for its object count plus 500 records", "[spawn_bindings]") {
    Harness h;
    h.first("CfgSetDatabaseSizes", {Value(0.0), Value(0.0), Value()});
    for (std::size_t i = 0; i < coney::world_objects::SpawnRecords::kPoolExtra; ++i) {
        CHECK(h.first("ObjSpawn", {str("crate"), list({0, 0, 0}), list({0, 0, 0, 1})}) != Value(0.0));
    }
    CHECK(h.first("ObjSpawn", {str("crate"), list({0, 0, 0}), list({0, 0, 0, 1})}) == Value(0.0));
    CHECK(h.records.all().size() == coney::world_objects::SpawnRecords::kPoolExtra);
}

TEST_CASE("resolving a record makes it live, pinning keeps it, a removed one stays gone", "[spawn_bindings]") {
    coney::world_objects::SpawnRecords records;
    SpawnRecord wheel;
    wheel.handle = 7;
    REQUIRE(records.add(wheel) != nullptr);
    SpawnRecord gone;
    gone.handle = 8;
    gone.removed = true;
    REQUIRE(records.add(gone) != nullptr);

    REQUIRE(records.resolve(7) != nullptr);
    CHECK(records.find(7)->live);
    records.setPinned(7, true);
    CHECK(records.find(7)->pinned);
    records.setPinned(99, true); // unknown: ignored
    CHECK(records.resolve(8) == nullptr);
    CHECK(records.resolve(99) == nullptr);
}

TEST_CASE("a fresh script state starts with no records but keeps the types", "[spawn_bindings]") {
    Harness h;
    h.first("CfgObj", {str("crate"), str("simple_object"), Value(0.0)});
    h.first("ObjSpawn", {str("crate"), list({0, 0, 0}), list({0, 0, 0, 1})});
    REQUIRE(h.types.all().size() == 1);
    REQUIRE(h.records.all().size() == 1);
    h.scripts.create();
    // The preloads' types outlive the state that ran them: a level's state never configures them again.
    CHECK(h.types.all().size() == 1);
    CHECK(h.records.all().empty());
}
