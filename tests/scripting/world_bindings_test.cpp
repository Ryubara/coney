// SPDX-License-Identifier: GPL-3.0-or-later
// The world bindings (scripting/world_bindings.h): the subtitle switch, the dynamic objects' zones, show, hide and
// destroy, the trigger spheres and their radius, the flag network and its traversal, a flag's switch, the collision
// inside a box, the throw error, and the three that do nothing. Synthetic scripts.
#include "scripting/world_bindings.h"

#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "raycast/collision_mesh.h"
#include "scripting/ai_bindings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/collision_fixtures.h"
#include "warriors/game_state.h"
#include "world_objects/flag_net.h"
#include "world_objects/flags.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"
#include "world_objects/trigger_spheres.h"
#include "world_objects/volume_boxes.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
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

// An AI host that keeps the traversals it is asked for.
class KeepingAi final : public coney::script::AiBindingHost {
  public:
    void goalMoveToFlag(const coney::script::MoveToFlagCall& /*call*/) override {}
    void actLookAt(const coney::script::LookAtCall& /*call*/) override {}
    void flagNetTraverse(const coney::script::FlagNetTraverseCall& call) override { traversals.push_back(call); }
    std::vector<coney::script::FlagNetTraverseCall> traversals;
};

// A script system with Coney's bindings over spawn records, spheres, a flag network and an AI host.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::world_objects::SpawnRecords records;
    coney::world_objects::ObjectTypes types;
    coney::world_objects::TriggerSpheres spheres;
    coney::world_objects::FlagNet net;
    KeepingAi ai;
    coney::script::BindingContext context{&state, &strings, &host, &recorded};
    ScriptSystem scripts;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.spawnRecords = &records;
        context.objectTypes = &types;
        context.spheres = &spheres;
        context.flagNet = &net;
        context.ai = &ai;
        scripts.create();
    }

    // Calls the binding `name` with `args`. REQUIREs success.
    void call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
    }
};

} // namespace

TEST_CASE("CfgSubtitles sets the game state's switch; DoorCRCCheck and EnableShadow do nothing", "[world_bindings]") {
    Harness h;
    h.call("CfgSubtitles", {Value(1.0)});
    CHECK(h.state.subtitles);
    h.call("CfgSubtitles", {Value()});
    CHECK_FALSE(h.state.subtitles);
    h.call("DoorCRCCheck", {Value(116.0), Value(82.0), Value(109.0), Value(208.0)});
    h.call("EnableShadow", {Value(5.0), Value()});
}

TEST_CASE("ObjEnableZone switches a zone; ObjShow, ObjHide and ObjDestroy act on a spawn record", "[world_bindings]") {
    Harness h;
    h.call("ObjEnableZone", {Value(26.0), Value(1.0)});
    CHECK(h.records.zoneEnabled(26));
    h.call("ObjEnableZone", {Value(26.0), Value()});
    CHECK_FALSE(h.records.zoneEnabled(26));

    REQUIRE(h.records.add(coney::world_objects::SpawnRecord{.handle = 9,
                                                            .typeName = "dyn_s_glow",
                                                            .shownMessage = std::nullopt,
                                                            .model = std::nullopt,
                                                            .physicsBody = std::nullopt,
                                                            .breakIn = std::nullopt}) != nullptr);
    h.call("ObjHide", {Value(9.0)});
    CHECK(h.records.find(9)->live); // resolving the handle spawns it
    CHECK(h.records.find(9)->hidden);
    h.call("ObjShow", {Value(9.0), Value(20.0)});
    CHECK_FALSE(h.records.find(9)->hidden);
    CHECK(h.records.find(9)->fadeInDistance == 20.0F);
    h.call("ObjDestroy", {Value(9.0)});
    CHECK(h.records.find(9)->removed);
    h.call("ObjShow", {Value(77.0)}); // no such object: nothing happens
}

TEST_CASE("ObjShow and ObjHide leave the show message; ObjDestroy with its message makes a marker die",
          "[world_bindings]") {
    Harness h;
    static_cast<void>(h.types.add("dyn_w_mission", "dyn_objective", 0));
    static_cast<void>(h.types.add("dyn_bat_tuff", "melee_weapon", 50));
    REQUIRE(h.records.add(coney::world_objects::SpawnRecord{.handle = 5,
                                                            .typeName = "dyn_w_mission",
                                                            .shownMessage = std::nullopt,
                                                            .model = std::nullopt,
                                                            .physicsBody = std::nullopt,
                                                            .breakIn = std::nullopt}) != nullptr);
    REQUIRE(h.records.add(coney::world_objects::SpawnRecord{.handle = 6,
                                                            .typeName = "dyn_w_mission",
                                                            .shownMessage = std::nullopt,
                                                            .model = std::nullopt,
                                                            .physicsBody = std::nullopt,
                                                            .breakIn = std::nullopt}) != nullptr);
    REQUIRE(h.records.add(coney::world_objects::SpawnRecord{.handle = 7,
                                                            .typeName = "dyn_bat_tuff",
                                                            .shownMessage = std::nullopt,
                                                            .model = std::nullopt,
                                                            .physicsBody = std::nullopt,
                                                            .breakIn = std::nullopt}) != nullptr);
    CHECK_FALSE(h.records.find(5)->shownMessage.has_value());
    h.call("ObjShow", {Value(5.0)});
    CHECK(h.records.find(5)->shownMessage == true);
    h.call("ObjHide", {Value(5.0)});
    CHECK(h.records.find(5)->shownMessage == false);
    // With its message a marker fades out first: dying, not yet removed; without, it goes at once.
    h.call("ObjDestroy", {Value(5.0), Value(true)});
    CHECK(h.records.find(5)->dying);
    CHECK_FALSE(h.records.find(5)->removed);
    h.call("ObjDestroy", {Value(6.0)});
    CHECK(h.records.find(6)->removed);
    // Another class has no handling of its own: removed at once either way.
    h.call("ObjDestroy", {Value(7.0), Value(true)});
    CHECK(h.records.find(7)->removed);
}

TEST_CASE("TriggerSphereCfg configures the object's sphere, a true mode being 1", "[world_bindings]") {
    Harness h;
    h.call("TriggerSphereCfg", {Value(12.0), Value(1.0), Value(4.0), Value(2.0), Value(500.0)});
    const coney::world_objects::TriggerSphere* sphere = h.spheres.find(12);
    REQUIRE(sphere != nullptr);
    CHECK(sphere->armed);
    CHECK(sphere->radius == 4.0F);
    CHECK(sphere->mode == 2);
    CHECK(sphere->handlerPeriodMs == 500);
    CHECK(sphere->stayPeriodMs == 1000);
    h.call("TriggerSphereCfg", {Value(12.0), Value(), Value(3.0), Value(std::string("yes")), Value(100.0)});
    CHECK_FALSE(sphere->armed);
    CHECK(sphere->mode == 1);
    CHECK(h.spheres.all().size() == 1);
}

TEST_CASE("FlagNetAddLink builds the network and FlagNetTraverse reaches the AI host", "[world_bindings]") {
    Harness h;
    h.call("FlagNetAddLink", {Value(20.0), Value(21.0), Value(0.0), Value(22.0), Value(0.0)});
    CHECK(h.net.neighbours(20) == std::vector<double>{21, 22});
    h.call("FlagNetTraverse", {Value(5.0), Value(7.0), Value(30.0), Value(1.0), Value()});
    REQUIRE(h.ai.traversals.size() == 1);
    CHECK(h.ai.traversals[0].human == 5);
    CHECK(h.ai.traversals[0].mode == 7);
    CHECK(h.ai.traversals[0].chance == 30);
    CHECK(h.ai.traversals[0].flagA);
    CHECK_FALSE(h.ai.traversals[0].flagB);
    // The short form: (human, flag, n) starts it with the defaults.
    h.call("FlagNetTraverse", {Value(6.0), Value(std::string("x")), Value(3.0)});
    REQUIRE(h.ai.traversals.size() == 2);
    CHECK(h.ai.traversals[1].mode == 0);
}

TEST_CASE("TriggerSphereSetRadius sets a sphere's radius, making one unarmed when there is none", "[world_bindings]") {
    Harness h;
    h.call("TriggerSphereSetRadius", {Value(40.0), Value(3.5)});
    const coney::world_objects::TriggerSphere* sphere = h.spheres.find(40.0);
    REQUIRE(sphere != nullptr);
    CHECK(sphere->radius == 3.5F);
    CHECK_FALSE(sphere->armed);
    CHECK(sphere->mode == 1);
    h.call("TriggerSphereEnable", {Value(40.0), Value(1.0)});
    h.call("TriggerSphereSetRadius", {Value(40.0), Value(6.0)});
    CHECK(h.spheres.find(40.0)->radius == 6.0F);
    CHECK(h.spheres.find(40.0)->armed);
}

TEST_CASE("FlagEnable switches a flag; ChangeCollision switches the triangles inside a box", "[world_bindings]") {
    Harness h;
    coney::world_objects::WorldFlags flags;
    flags.add(100.0, "fGuard", {1.0F, 2.0F, 0.0F}, 0.0F);
    coney::world_objects::VolumeBoxes boxes;
    boxes.add(200.0, "vGap", 0, {25.0F, 25.0F, 4.0F}, {30.0F, 30.0F, 2.0F}, true);
    coney::world_objects::LevelObjects objects;
    auto mesh = coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                        coney::test::floorAt(5.0F, 30.0F, 50.0F, 30.0F, 50.0F)));
    objects.world.collision = mesh.get();
    h.context.flags = &flags;
    h.context.boxes = &boxes;
    h.context.objects = &objects;

    h.call("FlagEnable", {Value(100.0), Value(0.0)});
    CHECK_FALSE(flags.find(100.0)->enabled);
    h.call("FlagEnable", {Value(100.0), Value(1.0)});
    CHECK(flags.find(100.0)->enabled);
    h.call("FlagEnable", {Value(999.0), Value(0.0)}); // no flag: nothing

    // The box holds the upper floor whole: it goes off, the lower floor (only partly inside) stays.
    const coney::raycast::Ray down{.origin = {40.0F, 40.0F, 20.0F}, .direction = {0.0F, 0.0F, -1.0F}, .length = 50.0F};
    h.call("ChangeCollision", {Value(200.0), Value(0.0)});
    CHECK(mesh->rayCast(down, {}, 0).value_or(coney::raycast::RayHit{}).t == 20.0F);
    h.call("ChangeCollision", {Value(200.0), Value(1.0)});
    CHECK(mesh->rayCast(down, {}, 0).value_or(coney::raycast::RayHit{}).t == 15.0F);
}

TEST_CASE("CfgSetMaxThrowError keeps radians; KillHumans does nothing", "[world_bindings]") {
    Harness h;
    h.call("CfgSetMaxThrowError", {Value(5.0), Value(10.0)});
    REQUIRE(h.state.characters.maxThrowError.has_value());
    const std::pair<float, float> error = h.state.characters.maxThrowError.value_or(std::pair{0.0F, 0.0F});
    CHECK(std::abs(error.first - 0.0872665F) < 1e-5F);
    CHECK(std::abs(error.second - 0.174533F) < 1e-5F);
    h.call("KillHumans", {Value(5.0)});
}
