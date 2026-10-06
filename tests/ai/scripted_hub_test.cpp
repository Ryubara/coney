// SPDX-License-Identifier: GPL-3.0-or-later
// The hub's bindings from Lua to the level's humans and brains (scripting/hub_bindings.h, ai/scripted_hub.h): each
// binding called by name in a script state whose AI host is the scripted brains over a synthetic scene, then the
// humans and the workouts checked. Handles: 1 the player, 2 and up the AI humans the tests add.
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_hub.h"
#include "core/error.h"
#include "gui/global_strings.h"
#include "human/human_flags.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/ai_fixtures.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using coney::ai::Brain;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A front-end host that ignores every request: these bindings ask nothing of it.
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

// A synthetic scene whose brains a script state's hub bindings drive, with a Lua function `Note` that keeps the first
// argument of each call.
struct Level {
    coney::test::AiScene scene;
    coney::world_objects::WorldFlags flags;
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::BindingContext context;
    std::unique_ptr<ScriptSystem> scripts;
    std::vector<double> notes;

    Level() {
        flags.createPool(8);
        scripted = std::make_unique<coney::ai::ScriptedBrains>(scene.brains, flags);
        scripted->bind(1.0, scene.player());
        scripted->setPlayer(&scene.player());
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.ai = scripted.get();
        scripts = std::make_unique<ScriptSystem>(
            [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
            },
            [this](ScriptSystem& system, LuaVm& vm) {
                coney::script::installBindings(system, vm, context);
                vm.registerFunction("Note", [this](std::span<const Value> args) {
                    notes.push_back(args.empty() ? -1.0 : args.front().number().value_or(-1.0));
                    return coney::script::binding::none();
                });
            },
            ScriptSystem::Log{});
        scripts->create();
        scripted->setScripts(scripts.get());
        coney::ai::HubLookups lookups;
        lookups.workout = &state.hub.workout;
        scripted->hubHost().setLookups(std::move(lookups));
    }

    // An AI human at `feet`, bound to the next handle.
    Brain& add(coney::anim::Vec3 feet) {
        Brain& brain = scene.add(feet, 0.0F);
        scripted->bind(brain.handle(), brain);
        return brain;
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns its first result (nil for none).
    Value call(std::string_view name, const std::vector<Value>& args) {
        auto result = scripts->vm().call(scripts->vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
    }
};

} // namespace

TEST_CASE("The hub's human switches reach the humans", "[ai][hub]") {
    Level level;
    Brain& extra = level.add({44.0F, 40.0F, 0.0F});
    const Value handle(extra.handle());
    level.call("HuSetScale", {handle, Value(1.5)});
    CHECK(extra.human().scale() == 1.5F);
    level.call("HuSetMug", {handle, Value()});
    CHECK_FALSE(extra.human().script().muggable);
    level.call("HuSetUnarrestable", {handle, Value(1.0)});
    CHECK(extra.human().hasFlag(coney::human::flag::kUnarrestable));
    level.call("HuSetCombatMode", {handle, Value(1.0)});
    CHECK(extra.human().script().combatMode);
    // An AI human's cuffs are its own, kept to 0-9.
    level.call("HuGiveCuffs", {handle, Value(12.0)});
    CHECK(extra.human().script().cuffs == 9);
    CHECK(level.call("HuIsDead", {handle}).isNil());
    CHECK_FALSE(level.call("HuIsDead", {Value(99.0)}).isNil());
    CHECK(level.call("ObjIsAlive", {handle}).number() == 1.0);
}

TEST_CASE("A workout begins within reach of its equipment, repeats and ends with its callbacks", "[ai][hub]") {
    Level level;
    Brain& athlete = level.add({10.0F, 10.0F, 0.0F});
    Brain& bench = level.add({11.0F, 10.0F, 0.0F});
    const Value handle(athlete.handle());
    level.call("HuSetWorkoutCallbacks",
               {Value(std::string("Note")), Value(std::string("Note")), Value(std::string("Note"))});
    level.call("HuWorkout", {handle, Value(bench.handle()), Value(std::string("start")), Value(std::string("end")),
                             Value(std::string("l1")), Value(std::string("l2")), Value(std::string("l3"))});
    coney::ai::ScriptedHub& hub = level.scripted->hubHost();
    REQUIRE(hub.workoutOf(athlete.handle()) != nullptr);
    CHECK_FALSE(hub.workoutOf(athlete.handle())->begun);

    hub.update(0);
    CHECK(hub.workoutOf(athlete.handle())->begun);
    CHECK(athlete.human().script().workingOut);
    CHECK(level.notes == std::vector<double>{athlete.handle()});

    // One repetition per kRepMs at an effort of 1.
    hub.update(coney::ai::ScriptedHub::kRepMs - 1);
    CHECK(level.notes.size() == 1);
    hub.update(coney::ai::ScriptedHub::kRepMs);
    CHECK(level.notes.size() == 2);

    level.call("HuStopWorkout", {handle});
    CHECK(hub.workoutOf(athlete.handle()) == nullptr);
    CHECK_FALSE(athlete.human().script().workingOut);
    CHECK(level.notes.size() == 3);
}

TEST_CASE("A workout out of reach of its equipment gives up without a callback", "[ai][hub]") {
    Level level;
    Brain& athlete = level.add({10.0F, 10.0F, 0.0F});
    Brain& bench = level.add({20.0F, 10.0F, 0.0F});
    level.call("HuSetWorkoutCallbacks",
               {Value(std::string("Note")), Value(std::string("Note")), Value(std::string("Note"))});
    level.call("HuWorkout",
               {Value(athlete.handle()), Value(bench.handle()), Value(std::string("start")), Value(std::string("end")),
                Value(std::string("l1")), Value(std::string("l2")), Value(std::string("l3"))});
    level.scripted->hubHost().update(0);
    CHECK(level.scripted->hubHost().workoutOf(athlete.handle()) == nullptr);
    CHECK(level.notes.empty());
}
