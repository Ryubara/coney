// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble match's bindings (scripting/rumble_match_bindings.h, docs/research/rumble.md): each called by name in a
// script state whose AI host is the scripted brains over a synthetic scene, then the host's requests, the brains and
// the gangs checked; and the humans created while the start callback holds the level, as the head counts see them.
// Handles: 1 the player, 2 and up the AI humans the tests add.
#include "scripting/rumble_match_bindings.h"

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/idle_goals.h"
#include "ai/scripted_brains.h"
#include "ai/tactic.h"
#include "ai/tactic_attack.h"
#include "ai/tactic_confront.h"
#include "core/error.h"
#include "gui/global_strings.h"
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

// A front-end host that keeps the Rumble requests.
class RumbleHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
    void showRumbleModeIntro(std::string_view onDone, std::span<const std::string> names) override {
        introDone = onDone;
        introNames.assign(names.begin(), names.end());
    }
    void launchRumbleWin(std::string_view winner, std::string_view reason) override {
        wins.emplace_back(winner);
        wins.emplace_back(reason);
    }

    std::string introDone;
    std::vector<std::string> introNames;
    std::vector<std::string> wins;
};

// A synthetic scene whose brains a script state's bindings drive through the scripted brains.
struct Level {
    coney::test::AiScene scene;
    coney::world_objects::WorldFlags flags;
    std::unique_ptr<coney::ai::ScriptedBrains> scripted;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    RumbleHost host;
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
};

} // namespace

TEST_CASE("the Rumble match's bindings are registered in the binding table", "[scripting][rumble]") {
    for (const std::string_view name : coney::script::kRumbleMatchBindings) {
        CAPTURE(name);
        bool found = false;
        for (const coney::script::BindingInfo& info : coney::script::bindingTable()) {
            found = found || (info.name == name && info.kind == coney::script::BindingKind::Real);
        }
        CHECK(found);
    }
}

TEST_CASE("ShowRumbleModeIntro and HUDLaunchRumbleWin reach the host", "[scripting][rumble]") {
    Level level;
    auto names = std::make_shared<coney::script::Table>();
    REQUIRE(names->set(Value(1.0), Value("FURIES")).has_value());
    REQUIRE(names->set(Value(2.0), Value("ORPHANS")).has_value());
    level.call("ShowRumbleModeIntro", {Value("FinishCountdown"), Value(names)});
    CHECK(level.host.introDone == "FinishCountdown");
    REQUIRE(level.host.introNames.size() == 10);
    CHECK(level.host.introNames[0] == "FURIES");
    CHECK(level.host.introNames[1] == "ORPHANS");
    CHECK(level.host.introNames[2].empty());

    level.call("HUDLaunchRumbleWin", {Value("ORPHANS"), Value("WIN!")});
    CHECK(level.host.wins == std::vector<std::string>{"ORPHANS", "WIN!"});
}

TEST_CASE("TacticAttack and TacticConfront set the gang's tactic, the confront's defaults filled in",
          "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    Brain& rival = level.add({50.0F, 40.0F, 0.0F});
    coney::ai::Gangs& gangs = level.scene.brains.gangs();
    const int ours = gangs.create(19, "Furies");
    const int theirs = gangs.create(21, "Orphans");
    gangs.addMember(ours, thug);
    gangs.addMember(theirs, rival);

    level.call("TacticConfront", {Value(static_cast<double>(ours)), Value(static_cast<double>(theirs))});
    REQUIRE(gangs.find(ours)->tactic() != nullptr);
    CHECK(gangs.find(ours)->tactic()->type() == coney::ai::kConfrontTactic);
    level.scene.run(1);
    CHECK(thug.topGoal() != nullptr);
    CHECK(thug.target() == &rival);

    level.call("TacticAttack", {Value(static_cast<double>(ours)), Value("Attacked")});
    level.scene.run(1);
    REQUIRE(gangs.find(ours)->tactic() != nullptr);
    CHECK(gangs.find(ours)->tactic()->type() == coney::ai::kAttackTactic);

    CHECK(level.call("HuGetGang", {Value(thug.handle())}).number() == static_cast<double>(ours));
    CHECK(level.call("HuGetGang", {Value(99.0)}).number() == 65535.0);
}

TEST_CASE("HuSetMaxHealth fills a human to a new maximum; BrFlushGoals and BrFlushActions empty its brain",
          "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    level.call("HuSetMaxHealth", {Value(thug.handle()), Value(500.0)});
    CHECK(thug.human().fighter().health().maximum() == 500);
    CHECK(thug.human().fighter().health().value() == 500);

    thug.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    level.call("BrFlushGoals", {Value(thug.handle())});
    CHECK(thug.goalCount() == 0);
    level.call("BrFlushActions", {Value(thug.handle())});
    CHECK(thug.actionCount() == 0);
}

TEST_CASE("HuDelete takes an AI human out through the remover and never a player", "[scripting][rumble]") {
    Level level;
    Brain& thug = level.add({44.0F, 40.0F, 0.0F});
    std::vector<const Brain*> removed;
    level.scripted->setRemover([&removed](Brain& brain) { removed.push_back(&brain); });
    level.call("HuDelete", {Value(1.0)});
    CHECK(removed.empty());
    level.call("HuDelete", {Value(thug.handle())});
    CHECK(removed == std::vector<const Brain*>{&thug});
    CHECK(level.call("HuIsAlive", {Value(thug.handle())}).isNil());
}

TEST_CASE("humans created while the level is held count in their gangs and answer as alive until deleted",
          "[scripting][rumble]") {
    Level level;
    const int gang = level.scene.brains.gangs().create(19, "Furies");
    level.scripted->hold();
    // Two humans created for the gang during the hold: an AI fighter (7) and player 1 (8).
    for (const auto& [handle, playerIndex] : {std::pair{7.0, 0}, std::pair{8.0, 1}}) {
        coney::HumanCreation human;
        human.playerIndex = playerIndex;
        human.gang = gang;
        human.handle = handle;
        level.scripted->humanCreated(human);
    }
    const Value gangId(static_cast<double>(gang));
    CHECK(level.call("GangGetHeadCount", {gangId, Value(1.0)}).number() == 2.0);
    CHECK_FALSE(level.call("HuIsAlive", {Value(7.0)}).isNil());
    CHECK(level.call("HuIsAPlayer", {Value(7.0)}).isNil());
    CHECK_FALSE(level.call("HuIsAPlayer", {Value(8.0)}).isNil());
    CHECK(level.call("HuGetGang", {Value(7.0)}).number() == static_cast<double>(gang));

    level.call("HuDelete", {Value(7.0)});
    CHECK(level.call("GangGetHeadCount", {gangId, Value(1.0)}).number() == 1.0);
    CHECK(level.call("HuIsAlive", {Value(7.0)}).isNil());
}

TEST_CASE("HuSwitchPlayer hands the pad to the player's first standing team-mate and makes him player 1",
          "[scripting][rumble]") {
    Level level;
    Brain& downed = level.add({44.0F, 40.0F, 0.0F});
    Brain& mate = level.add({46.0F, 40.0F, 0.0F});
    Brain& rival = level.add({50.0F, 40.0F, 0.0F});
    Brain& stranger = level.add({60.0F, 40.0F, 0.0F});
    coney::ai::Gangs& gangs = level.scene.brains.gangs();
    const int ours = gangs.create(3, "Gang1");
    const int theirs = gangs.create(19, "Gang2");
    const int bystanders = gangs.create(0, "Gang0");
    gangs.addMember(ours, level.scene.player());
    gangs.addMember(ours, downed);
    gangs.addMember(ours, mate);
    gangs.addMember(theirs, rival);
    gangs.addMember(bystanders, stranger);
    downed.human().fighter().health().set(0);
    // The switcher swaps the brain types, as the play mode's does.
    std::vector<std::pair<const Brain*, const Brain*>> switches;
    level.scripted->setSwitcher(
        [&switches](Brain& from, Brain& to) {
            switches.emplace_back(&from, &to);
            from.setType(coney::ai::BrainType::Gang);
            to.setType(coney::ai::BrainType::Player);
        },
        false);

    // Not a player: no switch.
    CHECK(level.call("HuSwitchPlayer", {Value(mate.handle())}).number() == 0.0);
    CHECK(switches.empty());
    // The player: the down member is passed over.
    CHECK(level.call("HuSwitchPlayer", {Value(1.0)}).number() == mate.handle());
    REQUIRE(switches.size() == 1);
    CHECK(switches[0].first == &level.scene.player());
    CHECK(switches[0].second == &mate);
    CHECK(level.scripted->player() == &mate);
    CHECK_FALSE(level.call("HuIsAPlayer", {Value(mate.handle())}).isNil());
    CHECK(level.call("HuIsAPlayer", {Value(1.0)}).isNil());

    // The new player down too, the old one (now AI) still standing: back to him; then nobody standing in the gang,
    // and a kind-0 gang's human only outside level99.
    mate.human().fighter().health().set(0);
    CHECK(level.call("HuSwitchPlayer", {Value(mate.handle())}).number() == 1.0);
    level.scene.player().human().fighter().health().set(0);
    CHECK(level.call("HuSwitchPlayer", {Value(1.0)}).number() == 0.0);
    level.scripted->setSwitcher([](Brain& /*from*/, Brain& to) { to.setType(coney::ai::BrainType::Player); }, true);
    // ... and only in the story's game mode, 0.
    level.call("SetGameMode", {Value(3.0), Value(3.0), Value(19.0), Value(2.0)});
    CHECK(level.call("HuSwitchPlayer", {Value(1.0)}).number() == 0.0);
    level.call("SetGameMode", {Value(0.0), Value(0.0), Value(0.0), Value(0.0)});
    CHECK(level.call("HuSwitchPlayer", {Value(1.0)}).number() == stranger.handle());
}
