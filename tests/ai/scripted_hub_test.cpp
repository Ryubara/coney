// SPDX-License-Identifier: GPL-3.0-or-later
// The hub's bindings from Lua to the level's humans and brains (scripting/hub_bindings.h, ai/scripted_hub.h): each
// binding called by name in a script state whose AI host is the scripted brains over a synthetic scene, then the
// humans and the workouts checked. Handles: 1 the player, 2 and up the AI humans the tests add.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_hub.h"
#include "animation/anim_clip.h"
#include "combat/commands.h"
#include "core/error.h"
#include "gui/global_strings.h"
#include "human/human.h"
#include "human/human_animator.h"
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

// A clip of `duration` seconds that stands still.
coney::anim::AnimClip stillClip(float duration) {
    coney::anim::AnimClip clip;
    clip.duration = duration;
    clip.rootVelocity = {{.frame = 0, .value = coney::anim::Vec3{}}};
    clip.rootTranslation = {{.frame = 0, .value = coney::anim::Vec3{}}};
    return clip;
}

// The workout's five clips by name, as a level's clip source answers them: enter 0.5 s, exit 0.5 s, slow 2 s, mid
// 1.5 s, fast 1 s.
struct WorkoutClips {
    std::map<std::string, coney::anim::AnimClip, std::less<>> clips{{"w_enter", stillClip(0.5F)},
                                                                    {"w_exit", stillClip(0.5F)},
                                                                    {"w_slow", stillClip(2.0F)},
                                                                    {"w_mid", stillClip(1.5F)},
                                                                    {"w_fast", stillClip(1.0F)}};

    // The source over these clips.
    coney::ai::ScriptedBrains::ClipSource source() {
        return [this](std::string_view name) -> const coney::anim::AnimClip* {
            const auto found = clips.find(name);
            return found != clips.end() ? &found->second : nullptr;
        };
    }
};

// HuWorkout's arguments for `human` on `equipment` with the workout clips (or `enter` in place of the enter clip).
std::vector<Value> workoutArgs(double human, double equipment, std::string enter = "w_enter") {
    return {Value(human),
            Value(equipment),
            Value(std::move(enter)),
            Value(std::string("w_exit")),
            Value(std::string("w_slow")),
            Value(std::string("w_mid")),
            Value(std::string("w_fast"))};
}

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

TEST_CASE("A workout begins with its five clips, repeats on each wrap of its loop and ends with its callbacks",
          "[ai][hub][workout]") {
    // docs/research/workouts.md#engine: the enter clip, then the loop blend; an AI's effort is what HuSetWorkoutBlend
    // wrote; a repetition is each wrap of the blend's leading clip; HuStopWorkout ends it on the next update.
    Level level;
    WorkoutClips clips;
    level.scripted->setClipSource(clips.source());
    Brain& athlete = level.add({10.0F, 10.0F, 0.0F});
    Brain& bench = level.add({11.0F, 10.0F, 0.0F});
    const Value handle(athlete.handle());
    level.call("HuSetWorkoutCallbacks",
               {Value(std::string("Note")), Value(std::string("Note")), Value(std::string("Note"))});
    level.call("HuWorkout", workoutArgs(athlete.handle(), bench.handle()));
    coney::ai::ScriptedHub& hub = level.scripted->hubHost();
    REQUIRE(hub.workoutOf(athlete.handle()) != nullptr);
    CHECK_FALSE(hub.workoutOf(athlete.handle())->begun);

    std::uint64_t now = 0;
    const auto step = [&](int updates) {
        for (int i = 0; i < updates; ++i) {
            level.scene.run(1);
            now += 33;
            hub.update(now);
        }
    };
    step(1);
    REQUIRE(hub.workoutOf(athlete.handle()) != nullptr);
    CHECK(hub.workoutOf(athlete.handle())->begun);
    CHECK(athlete.human().script().workingOut);
    CHECK(level.notes == std::vector<double>{athlete.handle()});
    CHECK(athlete.human().animator().animId() == coney::human::kAnimWorkoutEnter);
    // An effort of 3 holds the loop on the fast clip (target 2): after the 0.5 s enter clip, one repetition a second.
    level.call("HuSetWorkoutBlend", {handle, Value(3.0)});
    step(20);
    REQUIRE(athlete.human().workoutLoop() != nullptr);
    CHECK(level.notes.size() == 1);
    step(70);
    CHECK(level.notes.size() == 3);

    level.call("HuStopWorkout", {handle});
    step(1);
    CHECK(hub.workoutOf(athlete.handle()) == nullptr);
    CHECK_FALSE(athlete.human().script().workingOut);
    CHECK(level.notes.size() == 4);
    CHECK(athlete.human().animator().animId() == coney::human::kAnimWorkoutExit);
}

TEST_CASE("A workout out of reach of its equipment, or with a clip missing, gives up without a callback",
          "[ai][hub][workout]") {
    Level level;
    WorkoutClips clips;
    level.scripted->setClipSource(clips.source());
    Brain& athlete = level.add({10.0F, 10.0F, 0.0F});
    Brain& far = level.add({20.0F, 10.0F, 0.0F});
    Brain& near = level.add({11.0F, 10.0F, 0.0F});
    level.call("HuSetWorkoutCallbacks",
               {Value(std::string("Note")), Value(std::string("Note")), Value(std::string("Note"))});
    level.call("HuWorkout", workoutArgs(athlete.handle(), far.handle()));
    level.scripted->hubHost().update(0);
    CHECK(level.scripted->hubHost().workoutOf(athlete.handle()) == nullptr);
    level.call("HuWorkout", workoutArgs(athlete.handle(), near.handle(), "no_such_clip"));
    level.scripted->hubHost().update(0);
    CHECK(level.scripted->hubHost().workoutOf(athlete.handle()) == nullptr);
    CHECK_FALSE(athlete.human().script().workingOut);
    CHECK(level.notes.empty());
}

TEST_CASE("The player's workout on himself: cross pumps it, left alone it runs down, triangle quits",
          "[ai][hub][workout]") {
    // The scripts make the player his own target (the reach always passes) with the chin-ups' tables
    // (docs/research/workouts.md#stations); his pad's cross presses are command 0x12, triangle 10.
    Level level;
    WorkoutClips clips;
    level.scripted->setClipSource(clips.source());
    coney::WorkoutSettings& settings = level.state.hub.workout;
    settings.rates = {{{0.04F, 0.04F, 0.03F}, {0.03F, 0.03F, 0.03F}, {3.0F, 2.0F, 2.0F}}};
    settings.factor = 1.0F;
    level.call("HuSetWorkoutCallbacks",
               {Value(std::string("Note")), Value(std::string("Note")), Value(std::string("Note"))});
    coney::ai::ScriptedHub& hub = level.scripted->hubHost();
    Brain& player = level.scene.player();
    coney::human::PlayerRecord& record = player.human().record();
    std::uint64_t now = 0;
    // `updates` steps with cross pressed every `every` updates (0: never) and triangle on the last when `quit`.
    const auto play = [&](int updates, int every, bool quit = false) {
        for (int i = 0; i < updates; ++i) {
            const bool pump = every > 0 && i % every == 0;
            record.command = quit && i == updates - 1 ? coney::combat::command::kTrianglePressed
                             : pump                   ? coney::combat::command::kCrossPressed
                                                      : coney::combat::command::kNone;
            level.scene.run(1);
            now += 33;
            hub.update(now);
        }
        record.command = coney::combat::command::kNone;
    };

    SECTION("pumped every 4 updates it keeps repeating; left alone it runs down and ends without one") {
        level.call("HuWorkout", workoutArgs(1.0, 1.0));
        play(1, 0);
        REQUIRE(hub.workoutOf(1.0) != nullptr);
        CHECK(hub.workoutOf(1.0)->begun);
        level.call("HuSetWorkoutBlend", {Value(1.0), Value(1.5)});
        play(165, 4);
        const std::size_t reps = level.notes.size() - 1;
        CHECK(reps >= 3);
        REQUIRE(hub.workoutOf(1.0) != nullptr);
        CHECK(hub.workoutOf(1.0)->effort.effort() >= 0.9F);
        // No more presses: the effort runs down and the workout ends within about 3 s, with no repetition after
        // the first second.
        int updates = 0;
        while (hub.workoutOf(1.0) != nullptr && updates < 300) {
            play(1, 0);
            ++updates;
        }
        CHECK(hub.workoutOf(1.0) == nullptr);
        CHECK(updates <= 100);
        CHECK(level.notes.back() == 1.0);
        CHECK_FALSE(player.human().script().workingOut);
    }
    SECTION("a clip played over the loop does not stop the workout: triangle still ends it") {
        // Human_EndWorkout stops the blend only if it is on top: the update goes on without it.
        level.call("HuWorkout", workoutArgs(1.0, 1.0));
        play(30, 4);
        REQUIRE(player.human().workoutLoop() != nullptr);
        player.human().playScripted(clips.clips.at("w_slow"), 999, 1.0F, 0.1F, coney::human::HeldFlags{});
        play(1, 0);
        REQUIRE(player.human().workoutLoop() == nullptr);
        REQUIRE(hub.workoutOf(1.0) != nullptr);
        const std::size_t notes = level.notes.size();
        play(1, 0, true);
        CHECK(hub.workoutOf(1.0) == nullptr);
        CHECK(level.notes.size() == notes + 1);
        CHECK_FALSE(player.human().script().workingOut);
    }
    SECTION("triangle quits at once") {
        level.call("HuWorkout", workoutArgs(1.0, 1.0));
        play(30, 4);
        REQUIRE(hub.workoutOf(1.0) != nullptr);
        const std::size_t notes = level.notes.size();
        play(1, 0, true);
        CHECK(hub.workoutOf(1.0) == nullptr);
        CHECK(level.notes.size() == notes + 1);
    }
}
