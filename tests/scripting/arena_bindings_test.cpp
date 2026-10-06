// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble arenas' bindings (scripting/arena_bindings.h, docs/research/rumble.md#bindings): each called by name in a
// script state whose AI host is the scripted brains over a synthetic scene, then the game state, the humans, their
// brains and the humans the scripts made checked. Handles: 1 the player, 2 and up the AI humans the tests add.
#include "scripting/arena_bindings.h"

#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "core/error.h"
#include "gui/global_strings.h"
#include "hud/hud.h"
#include "human/human_flags.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/ai_fixtures.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using coney::ai::Brain;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A front-end host that takes nothing.
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

// A synthetic scene whose brains a script state's bindings drive through the scripted brains, with the humans the
// scripts made and a HUD.
struct Level {
    coney::test::AiScene scene;
    coney::world_objects::WorldFlags flags;
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::CreatedHumans humans;
    coney::hud::Hud hud;
    coney::script::BindingContext context;
    std::unique_ptr<ScriptSystem> scripts;

    Level() {
        flags.createPool(4);
        scripted = std::make_unique<coney::ai::ScriptedBrains>(scene.brains, flags);
        scripted->bind(1.0, scene.player());
        scripted->setPlayer(&scene.player());
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.ai = scripted.get();
        context.humans = &humans;
        context.hud = &hud;
        scripts = std::make_unique<ScriptSystem>(
            [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
            },
            [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); },
            ScriptSystem::Log{});
        scripts->create();
        scripted->setScripts(scripts.get());
    }

    // An AI human at `feet`, bound to the next handle and kept as the scripts' human of that handle.
    Brain& add(coney::anim::Vec3 feet) {
        Brain& brain = scene.add(feet, 0.0F);
        scripted->bind(brain.handle(), brain);
        REQUIRE(humans.add(coney::HumanCreation{.name = "Thug",
                                                .type = 0,
                                                .position = std::array<float, 3>{feet.x, feet.y, feet.z},
                                                .headingDegrees = 0.0F,
                                                .playerIndex = 0,
                                                .gang = -1,
                                                .handle = brain.handle(),
                                                .model = {},
                                                .teleported = {},
                                                .teleports = 0}));
        return brain;
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns its first result (nil for none).
    Value call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts->vm().call(scripts->vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
    }
};

Value str(std::string_view text) { return Value(std::string(text)); }

// A position table {x, y, z}.
Value position(double x, double y, double z) {
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(x)).has_value());
    REQUIRE(table->set(Value(2.0), Value(y)).has_value());
    REQUIRE(table->set(Value(3.0), Value(z)).has_value());
    return Value(std::move(table));
}

} // namespace

TEST_CASE("SetGameMode keeps the mode and its parameters; modes 1 and 2 set the versus flag", "[scripting][rumble]") {
    Level level;
    CHECK(level.call("GetGameMode").number() == 0.0);
    level.call("SetGameMode", {Value(2.0), Value(3.0), Value(19.0), Value(4.0)});
    CHECK(level.call("GetGameMode").number() == 2.0);
    CHECK(level.state.gameMode.a == 3);
    CHECK(level.state.gameMode.b == 19);
    CHECK(level.state.gameMode.gangSize == 4);
    CHECK(level.state.gameMode.versus);
    level.call("SetGameMode", {Value(3.0), Value(3.0), Value(19.0), Value(1.0)});
    CHECK_FALSE(level.state.gameMode.versus);
}

TEST_CASE("the switches and the precache queue are kept in the game state", "[scripting][rumble]") {
    Level level;
    level.call("CNSEnableMissionInfo", {Value(1.0)});
    CHECK(level.state.missionInfo);
    level.call("CNSEnableMissionInfo", {Value()});
    CHECK_FALSE(level.state.missionInfo);
    level.call("WCEnableAutomaticSwitching", {Value(0.0)});
    CHECK_FALSE(level.state.autoSwitch);
    level.call("WCEnableAutomaticSwitching");
    CHECK(level.state.autoSwitch);
    level.call("QueueFileToPrecache", {str("gang_a.pak")});
    level.call("QueueFileToPrecache", {str("gang_b.pak")});
    CHECK(level.state.precacheQueue == std::vector<std::string>{"gang_a.pak", "gang_b.pak"});
    level.call("PrecacheWorld", {Value(10000.0), Value(50.0), Value()});
    CHECK(level.state.precacheQueue.empty());
}

TEST_CASE("the humans' lock, speech switch, pocket and damage response reach the human and its brain",
          "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    Brain& mate = level.add({46.0F, 40.0F, 0.0F});
    const double handle = thug.handle();
    level.call("HuLockMovement", {Value(handle), Value(1.0)});
    CHECK(thug.human().hasFlag(coney::human::flag::kMovementLocked));
    level.call("HuLockMovement", {Value(handle), Value(0.0)});
    CHECK_FALSE(thug.human().hasFlag(coney::human::flag::kMovementLocked));

    level.call("HuEnableSoundCommands", {Value(handle), Value(0.0)});
    CHECK_FALSE(thug.human().script().soundCommands);
    level.call("HuEnableSoundCommands", {Value(handle)});
    CHECK(thug.human().script().soundCommands);

    // The count defaults to 1 and is a byte; item 0 empties the pocket whatever the count.
    level.call("HuPutItemInPocket", {Value(handle), Value(12.0)});
    CHECK(thug.human().script().pocketItem == 12);
    CHECK(thug.human().script().pocketCount == 1);
    level.call("HuPutItemInPocket", {Value(handle), Value(3.0), Value(260.0)});
    CHECK(thug.human().script().pocketCount == 4);
    level.call("HuRemoveItemInPocket", {Value(handle)});
    CHECK(thug.human().script().pocketItem == 0);
    CHECK(thug.human().script().pocketCount == 0);

    // One brain, then every member of a gang; -1 is no gang.
    CHECK(thug.senses().damageResponse == 1);
    level.call("BrSetDamageResponse", {Value(handle), Value(3.0)});
    CHECK(thug.senses().damageResponse == 3);
    const int gang = level.scene.brains.gangs().create(3, "Gang1");
    level.scene.brains.gangs().addMember(gang, thug);
    level.scene.brains.gangs().addMember(gang, mate);
    level.call("GangSetDamageResponse", {Value(static_cast<double>(gang)), Value(4.0)});
    CHECK(thug.senses().damageResponse == 4);
    CHECK(mate.senses().damageResponse == 4);
    level.call("GangSetDamageResponse", {Value(-1.0), Value(0.0)});
    CHECK(mate.senses().damageResponse == 4);
}

TEST_CASE("Teleport moves a human the scripts made, keeping its facing for -1", "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    level.call("Teleport", {Value(thug.handle()), position(10.0, 12.0, 0.0), Value(90.0)});
    CHECK(thug.human().position().x == 10.0F);
    CHECK(thug.human().position().y == 12.0F);
    const coney::HumanCreation* made = level.humans.find(thug.handle());
    REQUIRE(made != nullptr);
    REQUIRE(made->teleported.has_value());
    CHECK(made->teleported->headingDegrees == 90.0F);
    CHECK(made->teleports == 1);
    // The default heading keeps the facing it has now.
    const float facing = level.scripted->humanPlacement(thug.handle())->headingDegrees;
    level.call("Teleport", {Value(thug.handle()), position(20.0, 22.0, 0.0)});
    CHECK(thug.human().position().x == 20.0F);
    CHECK(made->teleported->headingDegrees == facing);
    CHECK(made->teleports == 2);
    // A handle no human has does nothing.
    level.call("Teleport", {Value(99.0), position(0.0, 0.0, 0.0)});
}

TEST_CASE("HUDSetNumIndicator shows a gang's count on an indicator; no gang turns it off", "[scripting][rumble]") {
    Level level;
    level.call("HUDSetNumIndicator", {Value(2.0), Value(1.0), Value(5.0)});
    CHECK(level.hud.numIndicator(2).on);
    CHECK(level.hud.numIndicator(2).gang == 5);
    level.call("HUDSetNumIndicator", {Value(0.0), Value(1.0)});
    CHECK_FALSE(level.hud.numIndicator(0).on);
    level.call("HUDSetNumIndicator", {Value(3.0), Value(1.0), Value(5.0)});
}
