// SPDX-License-Identifier: GPL-3.0-or-later
// The scripted goals' Lua callbacks, GoalPlayDynAnimation, GoalAddressPerson, and the brain switches BrFlush and BrDead
// (docs/research/ai.md#scripted). Synthetic humans; the script services keep what they are asked.
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/address_person_goal.h"
#include "ai/brain.h"
#include "ai/engage_goals.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/idle_goals.h"
#include "ai/melee_goal.h"
#include "ai/play_anim_action.h"
#include "ai/play_dyn_animation_goal.h"
#include "ai/script_services.h"
#include "ai/story_goals.h"
#include "ai/tactic_crowd.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::test::AiScene;

namespace {

// Steps `scene` until `brain` has no goal, at most `limit` steps; returns whether it ran out of goals.
bool runOutOfGoals(AiScene& scene, const Brain& brain, int limit) {
    for (int k = 0; k < limit && brain.goalCount() > 0; ++k) {
        scene.run(1);
    }
    return brain.goalCount() == 0;
}

} // namespace

TEST_CASE("GoalPlayDynAnimation plays slot 668 and calls back 33 ms after its end with the handle and 1",
          "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    REQUIRE(coney::ai::goalPlayDynAnimation(brain, scene.services, "bow", "P1.Bowed", true));
    CHECK(brain.topGoal()->type() == GoalType::PlayDynAnimation);
    CHECK(scene.services.loaded == std::vector<std::string>{"bow"});
    REQUIRE(runOutOfGoals(scene, brain, 60));

    CHECK(scene.services.clips == std::vector<int>{coney::ai::kDynamicAnimId});
    CHECK(scene.services.freed == 1);
    REQUIRE(scene.services.calls.size() == 1);
    const coney::test::ScriptCall& call = scene.services.calls[0];
    CHECK(call.function == "P1.Bowed");
    CHECK(call.scheduled);
    CHECK(call.delayMs == coney::ai::kGoalCallbackDelayMs);
    CHECK(call.args == std::vector<double>{brain.handle(), 1.0});
}

TEST_CASE("GoalPlayDynAnimation without a clip name is not pushed; a clip that cannot start still completes",
          "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    CHECK_FALSE(coney::ai::goalPlayDynAnimation(brain, scene.services, "", "P1.Bowed", true));
    CHECK(brain.goalCount() == 0);

    scene.services.clipsPlay = false;
    REQUIRE(coney::ai::goalPlayDynAnimation(brain, scene.services, "bow", "", true));
    REQUIRE(runOutOfGoals(scene, brain, 60));
    CHECK(scene.services.calls.empty()); // no callback asked for
    CHECK(scene.services.freed == 1);
}

TEST_CASE("an interrupted GoalPlayDynAnimation ends when taken up again and calls back with 0", "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    REQUIRE(coney::ai::goalPlayDynAnimation(brain, scene.services, "bow", "P1.Bowed", true));
    scene.run(1);
    brain.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    scene.run(2);
    brain.popGoal();
    REQUIRE(runOutOfGoals(scene, brain, 10));
    REQUIRE(scene.services.calls.size() == 1);
    CHECK(scene.services.calls[0].args == std::vector<double>{brain.handle(), 0.0});
}

TEST_CASE("BrFlush ends the goals, firing their callbacks, then clears the actions", "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    REQUIRE(coney::ai::goalPlayDynAnimation(brain, scene.services, "bow", "P1.Bowed", true));
    scene.run(2);
    brain.flush();
    CHECK(brain.goalCount() == 0);
    CHECK(brain.actionCount() == 0);
    REQUIRE(scene.services.calls.size() == 1);
    CHECK(scene.services.calls[0].function == "P1.Bowed");
    CHECK(scene.services.calls[0].args == std::vector<double>{brain.handle(), 0.0});
}

TEST_CASE("BrDead clears the actions only, stops the warnings counting, and takes a player's pad", "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    brain.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    scene.run(2);
    brain.queueAction(std::make_unique<coney::ai::PlayAnimAction>(scene.services, 5, true, 500));
    REQUIRE(brain.actionCount() == 1);

    brain.setDead(true);
    CHECK(brain.dead());
    CHECK(brain.actionCount() == 0);
    CHECK(brain.goalCount() == 1);
    CHECK_FALSE(coney::ai::deliverEvent(brain, coney::ai::BrainEvent{.id = coney::ai::kEventAttackWarning}));
    CHECK(brain.attackWarnings() == 0);
    brain.setDead(false);
    CHECK(coney::ai::deliverEvent(brain, coney::ai::BrainEvent{.id = coney::ai::kEventAttackWarning}));
    CHECK(brain.attackWarnings() == 1);

    // Only a player's brain hands the pad over.
    std::vector<bool> pad;
    brain.setPadControl([&pad](bool on) { pad.push_back(on); });
    brain.setDead(true);
    CHECK(pad.empty());
    scene.player().setPadControl([&pad](bool on) { pad.push_back(on); });
    scene.player().setDead(true);
    scene.player().setDead(false);
    CHECK(pad == std::vector<bool>{false, true});
}

TEST_CASE("a dead player brain runs its goals like an AI brain", "[ai][scripted]") {
    AiScene scene;
    Brain& player = scene.player();
    player.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    scene.run(2);
    CHECK_FALSE(player.topGoal()->started());
    player.setDead(true);
    scene.run(2);
    CHECK(player.topGoal()->started());
}

TEST_CASE("GoalAddressPerson turns to the target, plays its scene once close, and ends after it", "[ai][scripted]") {
    AiScene scene;
    // 1.5 m east of the player, facing +y: the player is 90 degrees off.
    Brain& speaker = scene.add({41.5F, 40.0F, 0.0F}, 0.0F);
    REQUIRE(coney::ai::goalAddressPerson(
        speaker, scene.services,
        coney::ai::AddressOrder{
            .target = scene.player().handle(), .approach = 2.0F, .range = 4.0F, .scene = 7, .callback = "P1.Said"}));
    scene.run(1);
    CHECK(speaker.actionCount() == 1); // the turn, queued at the start

    // Turned, it pushes the scene's goal, which waits for the scene.
    for (int k = 0; k < 120 && scene.services.scenes.empty(); ++k) {
        scene.run(1);
    }
    REQUIRE(scene.services.scenes.size() == 1);
    CHECK(scene.services.scenes[0] == std::pair<int, std::string>{7, "P1.Said"});
    CHECK(speaker.topGoal()->type() == GoalType::PlayAnimation);
    scene.run(10);
    CHECK(speaker.goalCount() == 2);

    // The scene over: its goal stops it; the address ends on its next update, calling nothing back itself.
    scene.services.sceneOver = true;
    REQUIRE(runOutOfGoals(scene, speaker, 20));
    CHECK(scene.services.stopped == std::vector<int>{7});
    CHECK(scene.services.calls.empty());
}

TEST_CASE("GoalAddressPerson without a scene never ends; a missing target ends it", "[ai][scripted]") {
    AiScene scene;
    Brain& speaker = scene.add({41.0F, 40.0F, 0.0F}, 90.0F);
    REQUIRE(coney::ai::goalAddressPerson(
        speaker, scene.services,
        coney::ai::AddressOrder{
            .target = scene.player().handle(), .approach = 2.0F, .range = 0.0F, .scene = -1, .callback = ""}));
    scene.run(90);
    CHECK(speaker.topGoal() != nullptr);
    CHECK(speaker.topGoal()->type() == GoalType::AddressPerson);

    speaker.flush();
    REQUIRE(coney::ai::goalAddressPerson(
        speaker, scene.services,
        coney::ai::AddressOrder{.target = 99.0, .approach = 2.0F, .range = 0.0F, .scene = -1, .callback = ""}));
    CHECK(runOutOfGoals(scene, speaker, 5));
}

TEST_CASE("GoalFight pushes no fight goal for a member of a gang under a tactic", "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({42.0F, 40.0F, 0.0F}, 0.0F);
    CHECK(brain.fight(scene.player()));
    CHECK(brain.findGoal(GoalType::Fight) != nullptr);

    brain.flush();
    const int gang = scene.brains.gangs().create(19, "Crowd");
    scene.brains.gangs().addMember(gang, brain);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::TacticCrowd>("", true, 0));
    CHECK(brain.fight(scene.player()));
    CHECK(brain.findGoal(GoalType::Fight) == nullptr);
}

TEST_CASE("GoalFight pushes FindEnemy, Melee and the fight goal, replacing the last fight's and what is above it",
          "[ai][scripted]") {
    // Brain_PushFightGoal (docs/research/ai.md#targets): the old Melee and FindEnemy go with everything above them.
    AiScene scene;
    Brain& brain = scene.add({40.0F, 42.0F, 0.0F}, 180.0F);
    brain.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    brain.startFight(scene.player());
    REQUIRE(brain.goalCount() == 4);
    CHECK(brain.topGoal()->type() == GoalType::Fight);
    CHECK(brain.findGoal(GoalType::Melee) != nullptr);
    CHECK(brain.findGoal(GoalType::FindEnemy) != nullptr);

    brain.pushGoal(std::make_unique<coney::ai::IdleGoal>());
    brain.startFight(scene.player());
    CHECK(brain.goalCount() == 4);
    CHECK(brain.topGoal()->type() == GoalType::Fight);
}

TEST_CASE("GoalFight starts nothing at threat response 0 and pushes no goal under a tactic", "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({40.0F, 49.0F, 0.0F}, 180.0F);
    brain.setThreatResponse(0);
    brain.startFight(scene.player());
    CHECK(brain.goalCount() == 0);
    CHECK(brain.target() == nullptr);

    brain.setThreatResponse(2);
    const int gang = scene.brains.gangs().create(19, "Crowd");
    scene.brains.gangs().addMember(gang, brain);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::TacticCrowd>("", true, 0));
    brain.startFight(scene.player());
    CHECK(brain.goalCount() == 0);
    CHECK(brain.target() == &scene.player());
}

TEST_CASE("GoalFight from beyond the fight goal's range runs in with EngageEnemy and charges out of the run",
          "[ai][scripted]") {
    // level99's sparring Warriors are sent from about 9 m (docs/research/ai.md#fight-approach): the fight goal ends
    // at once, the Melee goal pushes EngageEnemy, which runs at the standing target with the charge armed and attacks
    // within 1.6 m.
    AiScene scene;
    Brain& brain = scene.add({40.0F, 49.0F, 0.0F}, 180.0F);
    REQUIRE(brain.distanceTo(scene.player()) > brain.meleeFar() * coney::ai::kFightRangeScale);
    brain.startFight(scene.player());
    scene.run(1);
    REQUIRE(brain.topGoal() != nullptr);
    CHECK(brain.topGoal()->type() == GoalType::EngageEnemy);
    CHECK(static_cast<const coney::ai::EngageEnemyGoal*>(brain.topGoal())->chargeArmed());

    float nearest = brain.distanceTo(scene.player());
    bool charged = false;
    for (int k = 0; k < 300 && !charged; ++k) {
        scene.run(1);
        nearest = std::min(nearest, brain.distanceTo(scene.player()));
        const coney::ai::Goal* top = brain.topGoal();
        charged = brain.nextAttackMs() > 0 && top != nullptr && top->type() == GoalType::EngageEnemy;
    }
    CHECK(charged);
    CHECK(nearest <= coney::ai::kChargeRange);
    CHECK(brain.findGoal(GoalType::Melee) != nullptr);
}

TEST_CASE("the run-in stops within 0.75 of the far range of a standing target when the charge is not armed",
          "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({40.0F, 44.5F, 0.0F}, 180.0F);
    brain.addEnemy(scene.player());
    brain.setTarget(&scene.player());
    brain.pushGoal(std::make_unique<coney::ai::EngageEnemyGoal>());
    scene.run(1);
    REQUIRE(brain.topGoal() != nullptr);
    CHECK_FALSE(static_cast<const coney::ai::EngageEnemyGoal*>(brain.topGoal())->chargeArmed());
    CHECK(runOutOfGoals(scene, brain, 200));
    const float distance = brain.distanceTo(scene.player());
    CHECK(distance <= coney::ai::kEngageStopShare * brain.meleeFar());
    CHECK(distance > coney::ai::kChargeRange);
    CHECK(brain.nextAttackMs() == 0);
}

TEST_CASE("the FindEnemy goal starts the fight again while the target can be fought, and ends without one",
          "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({40.0F, 42.0F, 0.0F}, 180.0F);
    brain.addEnemy(scene.player());
    brain.setTarget(&scene.player());
    brain.pushGoal(std::make_unique<coney::ai::FindEnemyGoal>(coney::ai::kNoFightLimit, 0));
    scene.run(1);
    CHECK(brain.findGoal(GoalType::Melee) != nullptr);
    CHECK(brain.goalCount() == 3);

    brain.flush();
    brain.setTarget(nullptr);
    brain.pushGoal(std::make_unique<coney::ai::FindEnemyGoal>(coney::ai::kNoFightLimit, 0));
    scene.player().human().fighter().health().set(0);
    CHECK(runOutOfGoals(scene, brain, 2));
}

TEST_CASE("a fight's goals all end when its target is out of health", "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({40.0F, 49.0F, 0.0F}, 180.0F);
    brain.startFight(scene.player());
    scene.run(1);
    REQUIRE(brain.findGoal(GoalType::Melee) != nullptr);
    scene.player().human().fighter().health().set(0);
    CHECK(runOutOfGoals(scene, brain, 30));
}

TEST_CASE("a Melee goal past its time limit ends without running", "[ai][scripted]") {
    AiScene scene;
    Brain& brain = scene.add({40.0F, 49.0F, 0.0F}, 180.0F);
    brain.addEnemy(scene.player());
    brain.setTarget(&scene.player());
    brain.pushGoal(std::make_unique<coney::ai::MeleeGoal>(0));
    scene.run(1);
    CHECK(brain.goalCount() == 0);
    CHECK(brain.actionCount() == 0);
}
