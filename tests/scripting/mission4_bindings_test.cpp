// SPDX-License-Identifier: GPL-3.0-or-later
// The fourth mission's bindings (scripting/mission4_bindings.h): the object type index and type bits, the
// configuration they keep, the brain and wound calls handed to the story host and the blocking of a walkable area.
// Synthetic scripts and objects.
#include "scripting/mission4_bindings.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/ai_bindings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "scripting/story_bindings.h"
#include "support/path_fixtures.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world/path_map.h"
#include "world_objects/flags.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A host with no screens.
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

// A story host that keeps the pedestrian types and wounds it is given.
class KeepingStory final : public coney::script::StoryBindingHost {
  public:
    void setPedType(double human, std::uint16_t type) override { pedTypes.emplace_back(human, type); }
    void setWounded(double human, bool wounded) override { wounds.emplace_back(human, wounded); }
    std::vector<std::pair<double, std::uint16_t>> pedTypes;
    std::vector<std::pair<double, bool>> wounds;
};

// An AI host whose story host is KeepingStory.
class StoryAi final : public coney::script::AiBindingHost {
  public:
    void goalMoveToFlag(const coney::script::MoveToFlagCall& /*call*/) override {}
    void actLookAt(const coney::script::LookAtCall& /*call*/) override {}
    coney::script::StoryBindingHost* story() override { return &keeping; }
    KeepingStory keeping;
};

// A script system with Coney's bindings over created humans, flags, spawn records, an object database, level objects
// on `paths` and the story host.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    coney::world_objects::SpawnRecords records;
    coney::world_objects::ObjectTypes types;
    coney::world_objects::LevelObjects objects;
    coney::world::PathMap paths;
    StoryAi ai;
    coney::script::BindingContext context{&state, &strings, &host, &recorded};
    ScriptSystem scripts;

    explicit Harness(coney::world::PathMap map)
        : paths(std::move(map)),
          scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.humans = &humans;
        context.flags = &flags;
        context.spawnRecords = &records;
        context.objectTypes = &types;
        objects.world.paths = &paths;
        context.objects = &objects;
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
        return results.front().number().value_or(-99.0);
    }
};

// Two overlapping squares: polygon 0 over [0, 10] × [0, 10] and polygon 1 over [4, 14] × [0, 10].
coney::world::PathMap overlapping() {
    coney::test::PathBuilder builder;
    builder.rectangle(0.0F, 10.0F, 0.0F, 10.0F);
    builder.rectangle(4.0F, 14.0F, 0.0F, 10.0F);
    return builder.build();
}

// A Lua position table {x, y, z}.
Value point(float x, float y, float z) {
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(static_cast<double>(x))));
    REQUIRE(table->set(Value(2.0), Value(static_cast<double>(y))));
    REQUIRE(table->set(Value(3.0), Value(static_cast<double>(z))));
    return Value(std::move(table));
}

} // namespace

TEST_CASE("ObjGetIndex gives a type's database index and -1 for an unknown or nil name", "[mission4_bindings]") {
    Harness h(overlapping());
    static_cast<void>(h.types.add("dyn_cbox", "simple_object", 10));
    static_cast<void>(h.types.add("dyn_beerbottle", "thrown_weapon", 1));
    CHECK(h.number("ObjGetIndex", {Value(std::string("dyn_beerbottle"))}) == 1.0);
    CHECK(h.number("ObjGetIndex", {Value(std::string("dyn_cbox"))}) == 0.0);
    // The lookup is case-sensitive.
    CHECK(h.number("ObjGetIndex", {Value(std::string("DYN_CBOX"))}) == -1.0);
    CHECK(h.number("ObjGetIndex", {Value()}) == -1.0);
}

TEST_CASE("GetRTTI gives a human's, a flag's and a prop's bit, and 0 for anything else", "[mission4_bindings]") {
    Harness h(overlapping());
    coney::HumanCreation human;
    human.name = "Cyrus";
    human.handle = 5.0;
    REQUIRE(h.humans.add(human));
    const double flag = 7.0;
    static_cast<void>(h.flags.add(flag, "Exit", {1.0F, 2.0F, 3.0F}, 0.0F));
    CHECK(h.number("GetRTTI", {Value(5.0)}) == static_cast<double>(coney::script::rtti::kHuman));
    CHECK(h.number("GetRTTI", {Value(flag)}) == static_cast<double>(coney::script::rtti::kFlag));
    CHECK(h.number("GetRTTI", {Value(999.0)}) == 0.0);
}

TEST_CASE("The fourth mission's configuration bindings keep their values", "[mission4_bindings]") {
    Harness h(overlapping());
    h.call("CfgChanceToGetHelp", {Value(35.0)});
    CHECK(h.state.story.chanceToGetHelp == 35);
    h.call("CfgChanceToGetHelp", {Value(250.0)});
    CHECK(h.state.story.chanceToGetHelp == 100);
    // ForceCrimeLevel is on when called with nothing.
    h.call("ForceCrimeLevel");
    CHECK(h.state.player.crimes.forced());
    h.call("ForceCrimeLevel", {Value()});
    CHECK_FALSE(h.state.player.crimes.forced());
}

TEST_CASE("BrSetPedType and HuSetWounded reach the story host", "[mission4_bindings]") {
    Harness h(overlapping());
    h.call("BrSetPedType", {Value(5.0), Value(65539.0)});
    REQUIRE(h.ai.keeping.pedTypes.size() == 1);
    // The low 16 bits are kept.
    CHECK(h.ai.keeping.pedTypes.front() == std::pair<double, std::uint16_t>{5.0, 3});
    h.call("HuSetWounded", {Value(5.0), Value(1.0)});
    h.call("HuSetWounded", {Value(5.0)});
    CHECK(h.ai.keeping.wounds == std::vector<std::pair<double, bool>>{{5.0, true}, {5.0, false}});
}

TEST_CASE("ChangeBlocker blocks the area whose centre is nearest the point, and opens it again",
          "[mission4_bindings]") {
    Harness h(overlapping());
    // (6, 5) lies in both; polygon 0's centre (5, 5) is 1 m off, polygon 1's (9, 5) 3 m.
    h.call("ChangeBlocker", {point(6.0F, 5.0F, 0.0F)});
    CHECK((h.paths.polygons()[0].flags & coney::world::kPathPolygonExcluded) != 0);
    CHECK((h.paths.polygons()[1].flags & coney::world::kPathPolygonExcluded) == 0);
    h.call("ChangeBlocker", {point(6.0F, 5.0F, 0.0F), Value(1.0)});
    CHECK((h.paths.polygons()[0].flags & coney::world::kPathPolygonExcluded) == 0);
    // Outside every polygon: nothing.
    h.call("ChangeBlocker", {point(50.0F, 5.0F, 0.0F)});
    CHECK((h.paths.polygons()[0].flags & coney::world::kPathPolygonExcluded) == 0);
    CHECK((h.paths.polygons()[1].flags & coney::world::kPathPolygonExcluded) == 0);
}
