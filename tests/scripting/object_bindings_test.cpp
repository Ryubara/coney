// SPDX-License-Identifier: GPL-3.0-or-later
// The glass and door bindings (docs/research/objects.md): SpawnDoor reading its type's CfgObj, the door commands and
// queries, the glass type table, SpawnBreakableGlass and BreakGlassInRadius, the lock-pick handlers, and the spawns
// still handing out handles with no level objects.
#include "scripting/object_bindings.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/object_fixtures.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/glass.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Table;
using coney::script::Value;

namespace {

// A host that ignores every request.
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

// A script system with Coney's bindings over a synthetic level's objects (or none).
struct Harness {
    coney::test::ObjectWorldFixture fixture;
    coney::world_objects::LevelObjects objects;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    coney::world_objects::ObjectTypes types;
    coney::world_objects::SpawnRecords records;
    coney::script::BindingContext context;
    ScriptSystem scripts;

    explicit Harness(bool withObjects = true)
        : context{.state = &state,
                  .strings = &strings,
                  .host = &host,
                  .recorded = &recorded,
                  .humans = &humans,
                  .flags = &flags,
                  .objectTypes = &types,
                  .spawnRecords = &records,
                  .objects = withObjects ? &objects : nullptr},
          scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        objects.world = fixture.world;
        scripts.create();
    }

    // The first result of `name(args)`, REQUIRing success; nil when there is none.
    Value first(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result && !result->empty() ? (*result)[0] : Value();
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

// A table of numbers t[1]..t[n].
Value list(const std::vector<double>& values) {
    auto table = std::make_shared<Table>();
    for (std::size_t i = 0; i < values.size(); ++i) {
        REQUIRE(table->set(Value(static_cast<double>(i + 1)), Value(values[i])).has_value());
    }
    return Value(table);
}

// `CfgObj(name, class, hitpoints, ...)` with a box 2 m wide and material 9 (22 arguments, the rest 0).
std::vector<Value> cfgObj(const char* name, const char* className, double hitpoints) {
    std::vector<Value> args(22, Value(0.0));
    args[0] = str(name);
    args[1] = str(className);
    args[2] = Value(hitpoints);
    args[7] = list({2.0, 0.1, 2.2});
    args[12] = Value(9.0);
    return args;
}

// `CfgObj(name, class, hitpoints, hits, ...)`: cfgObj() with the hits byte (`+0x5a`) set.
std::vector<Value> cfgObjWithHits(const char* name, const char* className, double hitpoints, double hits) {
    std::vector<Value> args = cfgObj(name, className, hitpoints);
    args[3] = Value(hits);
    return args;
}

// `ObjSpawn(type, {0, 0, 0})`: a spawn record's handle.
double spawnAtOrigin(Harness& h, const char* type) {
    return h.first("ObjSpawn", {str(type), list({0, 0, 0}), list({0, 0, 0, -1})}).number().value_or(0.0);
}

} // namespace

TEST_CASE("objectTypeFromCfgObj reads the first CfgObj of a type", "[object_bindings]") {
    Harness h;
    h.first("CfgObj", cfgObj("dyn_door_wood", "dyn_door_swinging", 100));
    h.first("CfgObj", cfgObj("dyn_door_wood", "dyn_door_fence", 5));
    const std::optional<coney::world_objects::ObjectTypeInfo> found =
        coney::script::objectTypeFromCfgObj(&h.recorded, "dyn_door_wood");
    REQUIRE(found.has_value());
    const coney::world_objects::ObjectTypeInfo info = found.value_or(coney::world_objects::ObjectTypeInfo{});
    CHECK(info.className == "dyn_door_swinging");
    CHECK(info.hitpoints == 100);
    CHECK(info.size.x == 2.0F);
    CHECK(info.material == 9);
    CHECK_FALSE(coney::script::objectTypeFromCfgObj(&h.recorded, "dyn_door_none").has_value());
    CHECK_FALSE(coney::script::objectTypeFromCfgObj(nullptr, "dyn_door_wood").has_value());
}

TEST_CASE("SpawnDoor, OpenDoor, IsDoorOpen, the leaves and the hitpoints", "[object_bindings]") {
    Harness h;
    h.first("CfgObj", cfgObj("dyn_door_wood", "dyn_door_swinging", 100));
    const double door =
        h.first("SpawnDoor", {str("dyn_door_wood"), list({4, 2, 0}), list({0, 0, 0, 1}), list({0, 1}), Value(7.0)})
            .number()
            .value_or(0.0);
    REQUIRE(h.objects.doors.find(door) != nullptr);
    CHECK(h.objects.doors.find(door)->number == 7);
    CHECK(h.first("GetHitpoints", {Value(door)}).number() == 100.0);
    const double left = h.first("GetLeftDoorHandle", {Value(door)}).number().value_or(0.0);
    const double right = h.first("GetRightDoorHandle", {Value(door)}).number().value_or(0.0);
    CHECK(left > door);
    CHECK(right == left + 1.0);
    CHECK(h.first("IsDoorOpen", {Value(door)}).isNil());
    h.first("OpenDoor", {Value(door)});
    for (int tick = 0; tick < 29; ++tick) {
        h.objects.tick();
    }
    CHECK(h.first("IsDoorOpen", {Value(door)}).number() == 1.0);
    h.first("CloseDoor", {Value(door)});
    CHECK(h.first("IsDoorOpen", {Value(door)}).isNil());
    h.first("SetDoorPickable", {Value(door)});
    CHECK(h.objects.doors.find(door)->pickable);
    h.first("SetDoorPickable", {Value(door), Value()});
    CHECK_FALSE(h.objects.doors.find(door)->pickable);
}

TEST_CASE("GetHitpoints reads any world object's hitpoints word", "[object_bindings]") {
    // Expected: objects.md#hitpoints-word, confirmed (runtime)
    Harness h;
    h.first("CfgObj", cfgObjWithHits("dyn_trashcan", "overhead_weapon", 150, 1));
    h.first("CfgObj", cfgObjWithHits("dyn_speaker", "overhead_weapon", 100, 1));
    h.first("CfgObj", cfgObjWithHits("dyn_newsstand_a", "dyn_masks", 0, 1));
    h.first("CfgObj", cfgObjWithHits("dyn_parkbench_a", "dyn_masks", 10, 2));
    h.first("CfgObj", cfgObj("dyn_ctrl_box", "dyn_ctrl_box", 100));
    // WorldObject_Init's word: the type's hitpoints.
    CHECK(h.first("GetHitpoints", {Value(spawnAtOrigin(h, "dyn_trashcan"))}).number() == 150.0);
    CHECK(h.first("GetHitpoints", {Value(spawnAtOrigin(h, "dyn_speaker"))}).number() == 100.0);
    // A dyn_masks prop's hit mode: 1 with hits and no hitpoints, 0 with hitpoints.
    CHECK(h.first("GetHitpoints", {Value(spawnAtOrigin(h, "dyn_newsstand_a"))}).number() == 1.0);
    CHECK(h.first("GetHitpoints", {Value(spawnAtOrigin(h, "dyn_parkbench_a"))}).number() == 0.0);
    // dyn_ctrl_box's init writes 15 whatever its CfgObj says.
    CHECK(h.first("GetHitpoints", {Value(spawnAtOrigin(h, "dyn_ctrl_box"))}).number() == 15.0);
    // No world object: NilHandle, and an object destroyed for good.
    CHECK(h.first("GetHitpoints", {Value(0.0)}).number() == 0.0);
    const double gone = spawnAtOrigin(h, "dyn_trashcan");
    h.first("ObjDestroy", {Value(gone)});
    CHECK(h.first("GetHitpoints", {Value(gone)}).number() == 0.0);
}

TEST_CASE("GetHitpoints keeps a cabin door's type hitpoints through its hits; a splintering door's follow them",
          "[object_bindings]") {
    // Expected: objects.md#hitpoints-word, confirmed (runtime)
    Harness h;
    h.first("CfgObj", cfgObj("dyn_door_cabin_a", "dyn_door_swinging", 12));
    h.first("CfgObj", cfgObj("dyn_door_liz", "dyn_door_swinging", 100));
    const double cabin =
        h.first("SpawnDoor", {str("dyn_door_cabin_a"), list({4, 2, 0}), list({0, 0, 0, 1}), list({0, 1}), Value(7.0)})
            .number()
            .value_or(0.0);
    const double liz =
        h.first("SpawnDoor", {str("dyn_door_liz"), list({40, 2, 0}), list({0, 0, 0, 1}), list({2, 3}), Value(8.0)})
            .number()
            .value_or(0.0);
    // The destroy message makes each door hit itself once, for 4 points.
    h.first("BreakObjectsInRadius", {Value(cabin), Value(0.5)});
    h.first("BreakObjectsInRadius", {Value(liz), Value(0.5)});
    for (int tick = 0; tick < 10; ++tick) {
        h.objects.tick();
    }
    REQUIRE(h.objects.doors.find(cabin)->hitpoints == 8);
    REQUIRE(h.objects.doors.find(liz)->hitpoints == 96);
    CHECK(h.first("GetHitpoints", {Value(cabin)}).number() == 12.0);
    CHECK(h.first("GetHitpoints", {Value(liz)}).number() == 96.0);
}

TEST_CASE("GetHitpoints answers 0 for a broken cabin door and its leaves", "[object_bindings]") {
    // Expected: objects.md#hitpoints-word, confirmed (runtime)
    Harness h;
    h.first("CfgObj", cfgObj("dyn_door_cabin_a", "dyn_door_swinging", 12));
    h.first("CfgObj", cfgObj("dyn_dr_cabin_a", "sub_swinging_door", 100));
    const double cabin =
        h.first("SpawnDoor", {str("dyn_door_cabin_a"), list({4, 2, 0}), list({0, 0, 0, 1}), list({0, 1}), Value(7.0)})
            .number()
            .value_or(0.0);
    const double left = h.first("GetLeftDoorHandle", {Value(cabin)}).number().value_or(0.0);
    CHECK(h.first("GetHitpoints", {Value(left)}).number() == 100.0);
    // Three destroy messages, each a self-hit of 4 points: the third breaks it; its next update removes it.
    for (int message = 0; message < 3; ++message) {
        h.first("BreakObjectsInRadius", {Value(cabin), Value(0.5)});
        for (int tick = 0; tick < 10; ++tick) {
            h.objects.tick();
        }
    }
    for (int tick = 0; tick < 400; ++tick) {
        h.objects.tick();
    }
    CHECK(h.first("GetHitpoints", {Value(cabin)}).number() == 0.0);
    CHECK(h.first("GetHitpoints", {Value(left)}).number() == 0.0);
}

TEST_CASE("ObjDestroy of a swinging door removes its leaves, but not dyn_door_stall's", "[object_bindings]") {
    // Expected: objects.md#door-removal, confirmed (runtime)
    Harness h;
    h.first("CfgObj", cfgObj("dyn_door_wood", "dyn_door_swinging", 100));
    h.first("CfgObj", cfgObj("dyn_door_stall", "dyn_door_swinging", 10));
    h.first("CfgObj", cfgObj("dyn_dr_wood", "sub_swinging_door", 100));
    h.first("CfgObj", cfgObj("dyn_dr_stall", "sub_swinging_door", 100));
    const double wood =
        h.first("SpawnDoor", {str("dyn_door_wood"), list({4, 2, 0}), list({0, 0, 0, 1}), list({0, 1}), Value(7.0)})
            .number()
            .value_or(0.0);
    const double left = h.first("GetLeftDoorHandle", {Value(wood)}).number().value_or(0.0);
    const double right = h.first("GetRightDoorHandle", {Value(wood)}).number().value_or(0.0);
    // A leaf is a world object of its own type.
    CHECK(h.first("GetHitpoints", {Value(left)}).number() == 100.0);
    h.first("ObjDestroy", {Value(wood)});
    CHECK(h.first("GetHitpoints", {Value(wood)}).number() == 0.0);
    CHECK(h.first("GetHitpoints", {Value(left)}).number() == 0.0);
    CHECK(h.first("GetHitpoints", {Value(right)}).number() == 0.0);
    CHECK_FALSE(h.objects.positionOf(left).has_value());

    const double stall =
        h.first("SpawnDoor", {str("dyn_door_stall"), list({8, 2, 0}), list({0, 0, 0, 1}), list({2, 3}), Value(8.0)})
            .number()
            .value_or(0.0);
    const double leaf = h.first("GetLeftDoorHandle", {Value(stall)}).number().value_or(0.0);
    h.first("ObjDestroy", {Value(stall)});
    CHECK(h.first("GetHitpoints", {Value(stall)}).number() == 0.0);
    CHECK(h.first("GetHitpoints", {Value(leaf)}).number() == 100.0);
    CHECK(h.objects.positionOf(leaf).has_value());
}

TEST_CASE("CfgSetGlassProperties, SpawnBreakableGlass and BreakGlassInRadius", "[object_bindings]") {
    Harness h;
    h.first("CfgSetGlassProperties", {Value(2.0), Value(), Value(1.0), Value(5.0), Value(6.0)});
    CHECK(h.recorded.count("CfgSetGlassProperties") == 1);
    REQUIRE(h.objects.glass.type(2) != nullptr);
    CHECK(h.objects.glass.type(2)->alarm);
    CHECK_FALSE(h.objects.glass.type(2)->windowLink);
    const double pane =
        h.first("SpawnBreakableGlass", {Value(2.0), list({4, 11, 0}), list({4, 13, 0}), list({4, 11, 2}), list({0, 0}),
                                        list({1, 1}), Value(0.0), Value(2.0), Value(3.0)})
            .number()
            .value_or(0.0);
    REQUIRE(h.objects.glass.find(pane) != nullptr);
    // The radius is round an object: the pane itself.
    h.first("BreakGlassInRadius", {Value(pane), Value(1.0)});
    CHECK(h.objects.glass.find(pane)->broken);
}

TEST_CASE("the lock-pick handlers are kept by name", "[object_bindings]") {
    Harness h;
    h.first("CfgSetLockPickHandler", {str("Start"), str("Stop"), str("Win")});
    h.first("CfgSetLockPickStageFailHandler", {str("Fail")});
    CHECK(h.objects.lockPick.start == "Start");
    CHECK(h.objects.lockPick.stop == "Stop");
    CHECK(h.objects.lockPick.success == "Win");
    CHECK(h.objects.lockPick.stageFail == "Fail");
}

TEST_CASE("with no level objects the spawns still give handles", "[object_bindings]") {
    Harness h(false);
    const double first = h.first("SpawnDoor", {str("dyn_door_wood")}).number().value_or(0.0);
    const double second = h.first("SpawnBreakableGlass", {Value(1.0)}).number().value_or(0.0);
    CHECK(first >= 1.0);
    CHECK(second == first + 1.0);
    CHECK(h.first("IsDoorOpen", {Value(first)}).isNil());
    CHECK(h.first("GetHitpoints", {Value(first)}).number() == 0.0);
}

TEST_CASE("the recorded glass types are applied to a level's objects", "[object_bindings]") {
    Harness h(false);
    h.first("CfgSetGlassProperties", {Value(2.0), Value(1.0), Value(), Value(5.0), Value(6.0)});
    h.first("CfgSetGlassProperties", {Value(2.0), Value(), Value(1.0), Value(7.0), Value(8.0)});
    coney::world_objects::GlassPanes glass;
    coney::script::applyRecordedGlassTypes(h.recorded, glass);
    const coney::world_objects::GlassType* type = glass.type(2);
    REQUIRE(type != nullptr);
    // The later call wins, as the table keeps the last write.
    CHECK_FALSE(type->windowLink);
    CHECK(type->alarm);
    CHECK(type->sprite == 7);
    CHECK(type->brokenSprite == 8);
}

TEST_CASE("the lock pick's difficulty is the Warrior class's byte +0x0a less 1", "[object_bindings]") {
    Harness h;
    const auto warriorClass = [&h](double classId, double lockPick) {
        std::vector<Value> args(14, Value(1.0));
        args[0] = Value(classId);
        args[10] = Value(lockPick);
        h.first("CfgWarriorClass", args);
    };
    warriorClass(3, 2.0);
    warriorClass(4, 9.0);
    warriorClass(5, 0.0);
    warriorClass(5, 3.0);
    CHECK(coney::script::lockPickDifficulty(&h.recorded, 3) == 1);
    CHECK(coney::script::lockPickDifficulty(&h.recorded, 4) == 2); // kept to 0-2
    CHECK(coney::script::lockPickDifficulty(&h.recorded, 5) == 2); // the last call wins
    CHECK(coney::script::lockPickDifficulty(&h.recorded, 6) == 0); // none recorded
    CHECK(coney::script::lockPickDifficulty(nullptr, 3) == 0);
}

TEST_CASE("CfgObj keeps a type's body, settle axes and restitution", "[object_bindings][physics]") {
    Harness h;
    std::vector<Value> args = cfgObj("dyn_test_brick", "simple_object", 10);
    args[6] = list({0.0, 0.01, 0.02});
    args[7] = list({0.07, 0.21, 0.1});
    args[8] = Value(1.0);  // PHYS_OBB
    args[9] = Value(13.0); // AXIS_XZ_ROUND
    args[10] = Value(0.1); // the restitution the scripts call mass
    h.first("CfgObj", args);
    const coney::world_objects::ObjectType* type = h.types.find("dyn_test_brick");
    REQUIRE(type != nullptr);
    CHECK(type->bodyShape == coney::world_objects::kBodyBox);
    CHECK(type->axis == 13);
    CHECK(type->bodySize[1] == 0.21F);
    CHECK(type->bodyCentre[2] == 0.02F);
    CHECK(type->restitution == 0.1F);
}
