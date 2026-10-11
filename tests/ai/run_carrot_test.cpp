// SPDX-License-Identifier: GPL-3.0-or-later
// `GoalRunCarrotRun` (ai/chase_goals.h, docs/references/bindings/ai.md#goalruncarrotrun): the boost rule from the
// nearest chaser's distance, the turn boost it gives, and the goal from Lua over a synthetic scene: the carrot runs its
// path at its gait's speed with no chaser near, faster (and farther) with one inside the distance, restores its turn
// boost when it ends past the last point or is flushed. Handles: 1 the player, 2 and up the AI humans, 100 and up the
// flags.
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/chase_goals.h"
#include "ai/goal.h"
#include "ai/move_action.h"
#include "ai/scripted_brains.h"
#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/ai_fixtures.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using Catch::Approx;
using coney::ai::Brain;
using coney::ai::GoalType;
using coney::ai::RunCarrotGoal;
using coney::ai::RunCarrotOrder;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A front-end host that ignores every request.
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

// A synthetic scene whose brains a script state's bindings drive through the scripted brains.
struct Level {
    coney::test::AiScene scene;
    coney::world_objects::WorldFlags flags;
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::BindingContext context;
    std::unique_ptr<ScriptSystem> scripts;

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
            [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); },
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

    // A path of flags 100, 101, ... at `points`, made with AddPath.
    Value path(const std::vector<coney::anim::Vec3>& points) {
        auto table = std::make_shared<coney::script::Table>();
        for (std::size_t i = 0; i < points.size(); ++i) {
            const double handle = 100.0 + static_cast<double>(i);
            flags.add(handle, "p", {points[i].x, points[i].y, points[i].z}, 0.0F);
            REQUIRE(table->set(Value(static_cast<double>(i + 1)), Value(handle)).has_value());
        }
        return call("AddPath", {Value("carrot"), Value(table)});
    }

    // `GoalRunCarrotRun(carrot, path, 0, false, 5, 1, gang, distance, keepAhead)`, as the seventh mission's run.
    void runCarrot(double carrot, const Value& route, int gang, double distance, bool keepAhead = false) {
        call("GoalRunCarrotRun", {Value(carrot), route, Value(0.0), Value(), Value(5.0), Value(1.0),
                                  Value(static_cast<double>(gang)), Value(distance), keepAhead ? Value(1.0) : Value()});
    }
};

} // namespace

TEST_CASE("the carrot's boost: 1 - d/distance inside the distance, or past it with keepAhead", "[ai][carrot]") {
    const RunCarrotOrder chased{
        .mode = 0, .reverse = false, .gait = 5, .radius = 1.0F, .chasers = 0, .distance = 10.0F, .keepAhead = false};
    CHECK(coney::ai::carrotBoost(chased, std::nullopt) == 0.0F);
    CHECK(coney::ai::carrotBoost(chased, 0.0F) == Approx(1.0F));
    CHECK(coney::ai::carrotBoost(chased, 2.5F) == Approx(0.75F));
    CHECK(coney::ai::carrotBoost(chased, 10.0F) == 0.0F);
    CHECK(coney::ai::carrotBoost(chased, 30.0F) == 0.0F);
    RunCarrotOrder ahead = chased;
    ahead.keepAhead = true;
    CHECK(coney::ai::carrotBoost(ahead, 5.0F) == 0.0F);
    CHECK(coney::ai::carrotBoost(ahead, 12.0F) == Approx(0.4F));
    CHECK(coney::ai::carrotBoost(ahead, 15.0F) == Approx(1.0F));
    CHECK(coney::ai::carrotBoost(ahead, 40.0F) == Approx(1.0F));
    // The turn boost added: none, 2 up to a half, 3 above.
    CHECK(coney::ai::carrotTurnBoost(0.0F) == 0);
    CHECK(coney::ai::carrotTurnBoost(0.25F) == 2);
    CHECK(coney::ai::carrotTurnBoost(0.5F) == 2);
    CHECK(coney::ai::carrotTurnBoost(0.75F) == 3);
}

TEST_CASE("with no chaser near, the carrot runs its path at its gait's speed and ends past the last point",
          "[ai][carrot]") {
    Level level;
    Brain& carrot = level.add({40.0F, 40.0F, 0.0F});
    const Value route = level.path({{40.0F, 46.0F, 0.0F}, {40.0F, 52.0F, 0.0F}});
    const int warriors = level.scripted->gangCreate(1, "Warriors");
    level.scripted->gangAddMember(warriors, 1.0); // the player, far behind
    level.scene.player().human().place({40.0F, 0.0F, 0.0F}, 0.0F);
    const int savedTurn = carrot.turnBoost();
    level.runCarrot(2.0, route, warriors, 13.0);
    REQUIRE(carrot.topGoal() != nullptr);
    REQUIRE(carrot.topGoal()->type() == GoalType::RunCarrot);
    level.scene.run(8);
    const auto* run = dynamic_cast<const RunCarrotGoal*>(carrot.topGoal());
    REQUIRE(run != nullptr);
    CHECK(run->current() == 0);
    CHECK(run->speed() == Approx(coney::ai::gaitSpeed(carrot.human().speeds(), 5)));
    CHECK(carrot.turnBoost() == savedTurn);
    // On to the second point, then done: the goal is gone and the turn boost is as before.
    for (int i = 0; i < 60 && carrot.topGoal() != nullptr; ++i) {
        level.scene.run(10);
    }
    CHECK(carrot.topGoal() == nullptr);
    CHECK(carrot.human().position().y > 50.0F);
    CHECK(carrot.turnBoost() == savedTurn);
}

TEST_CASE("a chaser inside the distance speeds the carrot up and raises its turn boost", "[ai][carrot]") {
    Level level;
    Brain& carrot = level.add({40.0F, 40.0F, 0.0F});
    const Value route = level.path({{40.0F, 80.0F, 0.0F}});
    const int warriors = level.scripted->gangCreate(1, "Warriors");
    level.scripted->gangAddMember(warriors, 1.0);
    level.scene.player().human().place({40.0F, 38.0F, 0.0F}, 0.0F); // 2 m behind, inside 13 m
    const int savedTurn = carrot.turnBoost();
    level.runCarrot(2.0, route, warriors, 13.0);
    level.scene.run(8);
    const auto* run = dynamic_cast<const RunCarrotGoal*>(carrot.topGoal());
    REQUIRE(run != nullptr);
    const float sprint = coney::ai::gaitSpeed(carrot.human().speeds(), 5);
    CHECK(run->speed() > sprint);
    CHECK(run->speed() <= sprint + coney::ai::kCarrotBoostSpeed);
    CHECK(carrot.turnBoost() >= savedTurn + 2);
}

TEST_CASE("GoalRunCarrotRun on a handle with no human does nothing", "[ai][carrot]") {
    Level level;
    const Value route = level.path({{40.0F, 46.0F, 0.0F}});
    level.runCarrot(99.0, route, 0, 13.0);
    CHECK(level.scene.player().topGoal() == nullptr);
}

TEST_CASE("a chaser inside the distance makes the carrot cover more ground than with none", "[ai][carrot]") {
    // The y the carrot reaches in 60 updates along a long path, with the player (its only chaser) 2 m behind or far.
    const auto reach = [](bool chased) {
        Level level;
        Brain& carrot = level.add({40.0F, 40.0F, 0.0F});
        const Value route = level.path({{40.0F, 140.0F, 0.0F}});
        const int warriors = level.scripted->gangCreate(1, "Warriors");
        level.scripted->gangAddMember(warriors, 1.0);
        level.scene.player().human().place({40.0F, chased ? 38.0F : 0.0F, 0.0F}, 0.0F);
        level.runCarrot(2.0, route, warriors, 13.0);
        level.scene.run(60);
        return carrot.human().position().y;
    };
    const float alone = reach(false);
    const float chased = reach(true);
    CHECK(alone > 40.5F);
    CHECK(chased > alone + 0.5F);
}

TEST_CASE("a carrot flushed while its turn boost is raised gets its saved turn boost back", "[ai][carrot]") {
    Level level;
    Brain& carrot = level.add({40.0F, 40.0F, 0.0F});
    const Value route = level.path({{40.0F, 80.0F, 0.0F}});
    const int warriors = level.scripted->gangCreate(1, "Warriors");
    level.scripted->gangAddMember(warriors, 1.0);
    level.scene.player().human().place({40.0F, 38.0F, 0.0F}, 0.0F);
    const int savedTurn = carrot.turnBoost();
    level.runCarrot(2.0, route, warriors, 13.0);
    level.scene.run(8);
    REQUIRE(carrot.turnBoost() >= savedTurn + 2);
    carrot.flush();
    CHECK(carrot.topGoal() == nullptr);
    CHECK(carrot.turnBoost() == savedTurn);
}
