// SPDX-License-Identifier: GPL-3.0-or-later
// The story bindings of the second and third missions from Lua to the level's humans, brains and gangs
// (scripting/story_bindings.h, ai/scripted_story.h): each binding called by name in a script state whose AI host is the
// scripted brains over a synthetic scene, then the humans, brains, gangs and the game state checked. Handles: 1 the
// player, 2 and up the AI humans the tests add, 100 and up the flags.
#include <cstddef>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_story.h"
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
using coney::ai::GoalType;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;
namespace flag = coney::human::flag;

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

// A synthetic scene whose brains a script state's story bindings drive, with a Lua function `Note` that keeps the
// arguments of each call.
struct Level {
    coney::test::AiScene scene;
    coney::world_objects::WorldFlags flags;
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::BindingContext context;
    std::unique_ptr<ScriptSystem> scripts;
    std::vector<std::vector<double>> notes;

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
                    notes.emplace_back();
                    for (const Value& arg : args) {
                        notes.back().push_back(arg.number().value_or(-1.0));
                    }
                    return coney::script::binding::none();
                });
            },
            ScriptSystem::Log{});
        scripts->create();
        scripted->setScripts(scripts.get());
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

// A Lua array of numbers.
Value array(const std::vector<double>& items) {
    auto table = std::make_shared<coney::script::Table>();
    for (std::size_t i = 0; i < items.size(); ++i) {
        REQUIRE(table->set(Value(static_cast<double>(i + 1)), Value(items[i])).has_value());
    }
    return Value(table);
}

} // namespace

TEST_CASE("The story's human switches reach the humans", "[ai][story]") {
    Level level;
    Brain& extra = level.add({44.0F, 40.0F, 0.0F});
    level.call("HuBlockLook", {Value(2.0), Value(1.0)});
    level.call("HuForceLook", {Value(2.0), Value(1.0)});
    CHECK(extra.human().hasFlag(flag::kBlockLook));
    CHECK(extra.human().hasFlag(flag::kForceLook));
    level.call("HuLockPadMovement", {Value(1.0), Value(1.0)});
    CHECK(level.scene.player().human().script().movementLocked);
    level.call("HuShadow", {Value(2.0), Value(0.0)});
    CHECK_FALSE(extra.human().script().shadow);
    level.call("HuSetHealth", {Value(2.0), Value(300.0)});
    CHECK(extra.human().health().value() == 300);
    level.call("HuKill", {Value(2.0)});
    CHECK(extra.human().health().value() == 1);
    CHECK(level.call("HuGetControlName", {Value(2.0)}).string() == "aiControl");
    CHECK(level.call("HuGetControlName", {Value(1.0)}).string() == "screenRelativeControl");
    CHECK(level.call("HuGetControlName", {Value(9.0)}).isNil());
    level.call("HuSetLOSRange", {Value(2.0), Value(12.0)});
    level.call("BrSetFOV", {Value(2.0), Value(90.0)});
    CHECK(extra.sightRange() == 12.0F);
    CHECK(extra.fieldOfView() == Catch::Approx(1.5708F));
}

TEST_CASE("Distances and paths run through the objects' places", "[ai][story]") {
    Level level;
    level.add({44.0F, 40.0F, 0.0F});
    CHECK(level.call("GetDistanceTweenHumans", {Value(1.0), Value(2.0)}).number().value_or(-1.0) == Catch::Approx(4.0));
    CHECK(level.call("TestDistance", {Value(1.0), Value(2.0), Value(5.0)}).number() == 1.0);
    CHECK(level.call("TestDistance", {Value(1.0), Value(2.0), Value(4.0)}).isNil());
    // With no path data the walking distance is the straight one.
    CHECK(level.call("WalkingDistance", {Value(1.0), Value(2.0)}).number().value_or(-1.0) == Catch::Approx(4.0));

    level.flags.add(100.0, "a", {44.0F, 50.0F, 0.0F}, 0.0F);
    level.flags.add(101.0, "b", {50.0F, 50.0F, 0.0F}, 0.0F);
    const Value path = level.call("AddPath", {Value("walk"), array({100.0, 0.0, 101.0, 999.0})});
    const std::optional<double> pathHandle = path.number();
    REQUIRE(pathHandle.has_value());
    if (!pathHandle) {
        return;
    }
    const coney::ai::WorldPath* kept = level.scripted->storyHost().path(*pathHandle);
    REQUIRE(kept != nullptr);
    CHECK(kept->points == std::vector<double>{100.0, 101.0});
    level.call("GoalTravelPath", {Value(2.0), path, Value(1.0), Value(0.0), Value(2.0), Value(0.5)});
    const Brain* walker = level.scripted->brain(2.0);
    REQUIRE(walker->topGoal() != nullptr);
    CHECK(walker->topGoal()->type() == GoalType::TravelPath);
}

TEST_CASE("Gangs take their turf, leader and exit", "[ai][story]") {
    Level level;
    Brain& first = level.add({44.0F, 40.0F, 0.0F});
    Brain& second = level.add({46.0F, 40.0F, 0.0F});
    const int gang = level.scripted->gangCreate(19, "Riffs");
    level.scripted->gangAddMember(gang, 2.0);
    level.scripted->gangAddMember(gang, 3.0);
    level.call("GangSetLeader", {Value(static_cast<double>(gang)), Value(3.0)});
    CHECK(level.call("GangGetLeader", {Value(static_cast<double>(gang))}).number() == 3.0);
    level.call("GangSetRespondPercentage", {Value(static_cast<double>(gang)), Value(40.0)});
    CHECK(level.scene.brains.gangs().find(gang)->orders().respondPercent == 40);
    level.call("GangSetHearRange", {Value(static_cast<double>(gang)), Value(1.0), Value(30.0)});
    CHECK(first.senses().helpHearRange == 30.0F);

    // Leaving through the nearest exit flag (activity 8), out of the AI's reach.
    level.flags.add(100.0, "exit", {60.0F, 40.0F, 0.0F}, 0.0F, 8);
    level.call("GangExitWorld", {Value(static_cast<double>(gang)), Value(), Value("Note")});
    CHECK(first.dead());
    REQUIRE(second.topGoal() != nullptr);
    CHECK(second.topGoal()->type() == GoalType::MoveToExitFlag);
    CHECK(level.scene.brains.gangs().find(gang)->orders().exiting);

    // Once no member is left alive the callback has the gang's id and the gang goes.
    first.human().fighter().health().set(0);
    second.human().fighter().health().set(0);
    level.scripted->storyHost().update();
    REQUIRE(level.notes.size() == 1);
    CHECK(level.notes[0] == std::vector<double>{static_cast<double>(gang)});
    CHECK(level.scene.brains.gangs().find(gang) == nullptr);
}

TEST_CASE("A Warrior command reaches the chief's crew and the callback", "[ai][story]") {
    Level level;
    Brain& crew = level.add({44.0F, 40.0F, 0.0F});
    const int gang = level.scripted->gangCreate(0, "Warriors");
    level.scripted->gangAddMember(gang, 1.0);
    level.scripted->gangAddMember(gang, 2.0);
    level.call("WCSetCallback", {Value("Note")});
    level.call("IssueWarriorCommand", {Value(0.0), Value(0.0)});
    CHECK(level.scripted->storyHost().warriorCommand() == 0);
    REQUIRE(crew.topGoal() != nullptr);
    CHECK(crew.topGoal()->type() == GoalType::TrackHuman);
    REQUIRE(level.notes.size() == 1);
    CHECK(level.notes[0] == std::vector<double>{1.0, 0.0});
    // An unforced repeat only repeats the line; a disabled command does nothing.
    level.call("IssueWarriorCommand", {Value(0.0), Value(0.0)});
    CHECK(level.notes.size() == 1);
    level.call("WCEnableCommand", {Value(1.0), Value(1.0), Value(0.0)});
    level.call("IssueWarriorCommand", {Value(1.0), Value(0.0)});
    CHECK(level.scripted->storyHost().warriorCommand() == 0);
}

TEST_CASE("The story's configuration reaches the game state", "[ai][story]") {
    Level level;
    level.call("CfgVerticalSightModifier", {Value(2.5)});
    level.call("SetSpawnMax", {Value(6.0)});
    level.call("CfgSetOutdoorMode", {Value(1.0)});
    level.call("CfgCivilianAggression", {Value(30.0), Value(40.0)});
    CHECK(level.state.story.verticalSight == 2.5F);
    CHECK(level.state.story.spawnMax == 6);
    CHECK(level.state.story.outdoor);
    CHECK(level.state.story.civilianAggression[0] == 30);
    CHECK(level.state.story.civilianAggression[1] == 40);
}

TEST_CASE("A group move gives the leader the flag and the others the leader", "[ai][story]") {
    Level level;
    Brain& lead = level.add({44.0F, 40.0F, 0.0F});
    Brain& follower = level.add({46.0F, 40.0F, 0.0F});
    const int gang = level.scripted->gangCreate(19, "Riffs");
    level.scripted->gangAddMember(gang, 2.0);
    level.scripted->gangAddMember(gang, 3.0);
    level.flags.add(100.0, "spot", {60.0F, 40.0F, 0.0F}, 0.0F);
    level.call("GangSetLeader", {Value(static_cast<double>(gang)), Value(2.0)});
    level.call("TacticMoveToFlag",
               {Value(static_cast<double>(gang)), Value(100.0), Value(3.0), Value(), Value("Note")});
    level.scene.brains.gangs().update(0);
    REQUIRE(lead.findGoal(GoalType::MoveToFlag) != nullptr);
    REQUIRE(follower.topGoal() != nullptr);
    CHECK(follower.topGoal()->type() == GoalType::TrackHuman);
    // Once the leader's walk is over the callback hears 8.
    lead.clearGoals();
    level.scene.brains.gangs().update(33);
    REQUIRE(level.notes.size() == 1);
    CHECK(level.notes[0] == std::vector<double>{static_cast<double>(gang), 8.0});
}

TEST_CASE("A defending gang reports the human it defends gone", "[ai][story]") {
    Level level;
    Brain& guard = level.add({44.0F, 40.0F, 0.0F});
    Brain& defended = level.add({46.0F, 40.0F, 0.0F});
    const int gang = level.scripted->gangCreate(19, "Orphans");
    level.scripted->gangAddMember(gang, 2.0);
    level.call("TacticDefend", {Value(static_cast<double>(gang)), Value(3.0), Value(2.0), Value("Note")});
    level.scene.brains.gangs().update(0);
    REQUIRE(guard.topGoal() != nullptr);
    CHECK(guard.topGoal()->type() == GoalType::TrackHuman);
    defended.human().fighter().health().set(0);
    level.scene.brains.gangs().update(2000);
    REQUIRE_FALSE(level.notes.empty());
    CHECK(level.notes.back() == std::vector<double>{static_cast<double>(gang), 11.0});
}

TEST_CASE("HuTag hands the human, the tag and the flag to the tag handler", "[ai][story]") {
    Level level;
    std::vector<double> got;
    level.scripted->storyHost().setTagHandler(
        [&got](double human, double tag, double flag) { got = {human, tag, flag}; });
    level.call("HuTag", {Value(1.0), Value(40.0), Value(100.0)});
    CHECK(got == std::vector<double>{1.0, 40.0, 100.0});
}
