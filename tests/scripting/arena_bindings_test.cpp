// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble arenas' bindings (scripting/arena_bindings.h, docs/research/rumble.md#bindings): each called by name in a
// script state whose AI host is the scripted brains over a synthetic scene, then the game state, the humans, their
// brains and the humans the scripts made checked. Handles: 1 the player, 2 and up the AI humans the tests add.
#include "scripting/arena_bindings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "camera/cameras.h"
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
#include "world_objects/spawn_records.h"

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
    CHECK(made->teleported.value_or(coney::world_objects::Placement{}).headingDegrees == 90.0F);
    CHECK(made->teleports == 1);
    // The default heading keeps the facing it has now.
    const std::optional<coney::world_objects::Placement> before = level.scripted->humanPlacement(thug.handle());
    REQUIRE(before.has_value());
    const float facing = before.value_or(coney::world_objects::Placement{}).headingDegrees;
    level.call("Teleport", {Value(thug.handle()), position(20.0, 22.0, 0.0)});
    CHECK(thug.human().position().x == 20.0F);
    CHECK(made->teleported.value_or(coney::world_objects::Placement{}).headingDegrees == facing);
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

TEST_CASE("the text scoreboard shows its labels and sorts by score, ties keeping their order", "[scripting][rumble]") {
    Level level;
    auto labels = std::make_shared<coney::script::Table>();
    REQUIRE(labels->set(Value(1.0), str("FURIES")).has_value());
    REQUIRE(labels->set(Value(2.0), str("ORPHANS")).has_value());
    REQUIRE(labels->set(Value(3.0), str("RIFFS")).has_value());
    level.call("HUDEnableTextProgress", {Value(1.0), Value(labels), Value(3.0)});
    const auto& rows = level.hud.textProgress();
    CHECK(rows[0].label == "FURIES");
    CHECK(rows[2].label == "RIFFS");
    CHECK_FALSE(rows[3].active);
    // ORPHANS ahead; then RIFFS level with them stays below.
    level.call("HUDSetTextProgress", {str("ORPHANS"), Value(5.0), position(255.0, 0.0, 0.0)});
    CHECK(rows[0].label == "ORPHANS");
    CHECK(rows[0].score == 5);
    CHECK(rows[0].colour.r == 255);
    CHECK(rows[0].colour.g == 0);
    level.call("HUDSetTextProgress", {str("RIFFS"), Value(5.0), position(0.0, 255.0, 0.0)});
    CHECK(rows[1].label == "RIFFS");
    level.call("HUDSetTextProgress", {str("NOBODY"), Value(9.0), position(0.0, 0.0, 0.0)});
    CHECK(rows[0].score == 5);
    level.call("HUDEnableTextProgress", {Value(0.0), Value(labels), Value(3.0)});
    CHECK_FALSE(rows[0].active);
}

TEST_CASE("W_ShowStopWatch shows the watch with its label and arms the warning", "[scripting][rumble]") {
    Level level;
    level.call("W_ShowStopWatch", {Value(1.0), str("TIME ")});
    CHECK(level.hud.stopWatch().shown);
    CHECK(level.hud.stopWatch().label == "TIME ");
    level.call("W_ShowStopWatch", {Value(0.0)});
    CHECK_FALSE(level.hud.stopWatch().shown);
}

TEST_CASE("King of the hill's crown, reticules and split mode", "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    const int gang = level.scene.brains.gangs().create(3, "Gang1");
    level.scene.brains.gangs().addMember(gang, thug);
    level.call("GangAttachSpinningIcon", {Value(static_cast<double>(gang)), str("dyn_crown"), Value(0.0)});
    CHECK(thug.human().script().icon == "dyn_crown");
    level.call("GangRemoveSpinningIcon", {Value(static_cast<double>(gang))});
    CHECK(thug.human().script().icon.empty());

    // A handle no human has leaves the reticules as they are.
    level.call("HuForceEnableReticule", {Value(99.0), Value(1.0)});
    CHECK_FALSE(level.state.forceReticules);
    level.call("HuForceEnableReticule", {Value(thug.handle()), Value(1.0)});
    CHECK(level.state.forceReticules);
}

TEST_CASE("Battle royal's knock-out and Warrior commands switch", "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    level.call("HuSetConscious", {Value(thug.handle()), Value(0.0)});
    CHECK(thug.human().script().knockedOut);
    CHECK_FALSE(thug.human().alive());
    CHECK(thug.dead());
    level.call("HuSetConscious", {Value(thug.handle()), Value(1.0)});
    CHECK_FALSE(thug.human().script().knockedOut);
    CHECK(thug.human().alive());
    CHECK_FALSE(thug.dead());

    level.call("TurnWarriorCommands", {Value(0.0)});
    CHECK_FALSE(level.state.characters.warriorCommands[0][1]);
    level.call("TurnWarriorCommands", {Value(2.0)});
    CHECK(level.state.characters.warriorCommands[1][3]);
}

TEST_CASE("Survival's police: brain type, attack weight, class, position and the goals at the player",
          "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    const double handle = thug.handle();
    level.call("BrSetType", {Value(handle), Value(4.0)});
    CHECK(thug.type() == coney::ai::BrainType::Civilian);
    // 0 (the player's) and past 6 are not taken.
    level.call("BrSetType", {Value(handle), Value(0.0)});
    level.call("BrSetType", {Value(handle), Value(7.0)});
    CHECK(thug.type() == coney::ai::BrainType::Civilian);
    level.call("BrSetAttackWeight", {Value(handle), Value(41.0), Value(0.0)});
    level.call("BrSetAttackWeight", {Value(handle), Value(39.0), Value(50.0)});
    CHECK(thug.attackWeights()[41] == 0);
    CHECK(thug.attackWeights()[39] == 50);

    // The class of the type it was made as: Rembrandt's 32 answers 30; no human answers 0.
    level.humans.find(handle)->type = 32;
    CHECK(level.call("HuGetCharType", {Value(handle)}).number() == 30.0);
    CHECK(level.call("HuGetCharType", {Value(999.0)}).number() == 0.0);

    const Value at = level.call("HuGetPosition", {Value(handle)});
    REQUIRE(at.table() != nullptr);
    CHECK(at.table()->field("x").number() == 44.0);
    CHECK(at.table()->field("y").number() == 40.0);
    const Value none = level.call("HuGetPosition", {Value(999.0)});
    REQUIRE(none.table() != nullptr);
    CHECK(none.table()->field("x").number() == 0.0);

    // The move goal on top of the engage goal: it runs to the player, then the engage goal fights him.
    level.scene.player().human().spawn(nullptr, {50.0F, 40.0F, 0.0F}, 0.0F);
    level.call("GoalEngageEnemy", {Value(handle), Value(1.0)});
    level.call("GoalMoveToHuman", {Value(handle), Value(1.0), Value(5.0), Value(1.0)});
    REQUIRE(thug.topGoal() != nullptr);
    CHECK(thug.topGoal()->type() == coney::ai::GoalType::MoveToHuman);
    for (int k = 0;
         k < 10 * 30 && thug.topGoal() != nullptr && thug.topGoal()->type() == coney::ai::GoalType::MoveToHuman; ++k) {
        level.scene.run(1);
    }
    CHECK(thug.distanceTo(level.scene.player()) <= 1.5F);
    CHECK(thug.findGoal(coney::ai::GoalType::MoveToHuman) == nullptr);
    CHECK(thug.findGoal(coney::ai::GoalType::EngageEnemy) != nullptr);
    REQUIRE(thug.topGoal() != nullptr);
    for (int k = 0; k < 60 && thug.topGoal()->type() != coney::ai::GoalType::Fight; ++k) {
        level.scene.run(1);
    }
    CHECK(thug.topGoal()->type() == coney::ai::GoalType::Fight);
    CHECK(thug.target() == &level.scene.player());
}

TEST_CASE("Wheelchair's control, auto-lock flag, reverse button, colour and a teleported object",
          "[scripting][rumble]") {
    Level level;
    coney::camera::Cameras cameras;
    coney::world_objects::SpawnRecords records;
    records.createPool(4);
    level.context.cameras = &cameras;
    level.context.spawnRecords = &records;
    // Player 1, handle 1: the commands are a pad's.
    Brain& racer = level.scene.player();
    const double handle = 1.0;

    cameras.enable(coney::camera::Cameras::kSwitchLookBehind, true);
    level.call("HuSetWheelchairControl", {Value(handle), Value(1.0)});
    CHECK(racer.human().hasFlag(coney::human::flag::kWheelchair));
    CHECK(racer.human().script().disabledCommands == ~std::uint64_t{0});
    CHECK_FALSE(cameras.enabled(coney::camera::Cameras::kSwitchLookBehind));
    level.call("HuSetWheelchairControl", {Value(handle), Value(0.0)});
    CHECK_FALSE(racer.human().hasFlag(coney::human::flag::kWheelchair));
    CHECK(racer.human().script().disabledCommands == 0);
    CHECK(cameras.enabled(coney::camera::Cameras::kSwitchLookBehind));

    level.call("HuSetNoAutoLock", {Value(handle), Value(1.0)});
    CHECK(racer.human().hasFlag(coney::human::flag::kNoAutoLock));
    level.call("CamAssignRevCamButton", {Value(512.0)});
    CHECK(cameras.reverseButton() == 512);
    level.call("ActGiveWay", {Value(handle), Value(1.0)});

    // A glow: ObjColor tints it, Teleport moves it and turns it about z.
    coney::world_objects::SpawnRecord glow;
    glow.handle = 300.0;
    glow.typeName = "dyn_w_mission";
    REQUIRE(records.add(glow) != nullptr);
    auto colour = std::make_shared<coney::script::Table>();
    for (const double k : {1.0, 2.0, 3.0, 4.0}) {
        REQUIRE(colour->set(Value(k), Value(k == 1.0 ? 255.0 : (k == 4.0 ? 1.0 : 16.0))).has_value());
    }
    level.call("ObjColor", {Value(300.0), Value(std::move(colour))});
    CHECK(records.find(300.0)->tint == 0xFF101001U);
    level.call("Teleport", {Value(300.0), position(5.0, 6.0, 7.0), Value(180.0)});
    CHECK(records.find(300.0)->position == std::array<float, 3>{5.0F, 6.0F, 7.0F});
    CHECK(std::abs(records.find(300.0)->rotation[2] - 1.0F) < 1e-5F);
}
