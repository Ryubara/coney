// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/tactic_attack.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brain_events.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/hub_goals.h"
#include "ai/melee_goal.h"
#include "support/ai_fixtures.h"

// TacticAttack without the team-ups (docs/research/ai-reimpl.md#unit-2): the binding, Start's melee, spacing, cower
// clips and flight count, the 1 s enemy check and the 3 s idle pass, the events (a member hit, a death, a member down
// and the flight, violence nearby), Brain_HasFightGoal and the gang's alert state. Synthetic scenes only; the gangs'
// clock is driven by Gangs::update() so each timer is hit on the millisecond.

using coney::ai::Brain;
using coney::ai::BrainEvent;
using coney::ai::Gang;
using coney::ai::Gangs;
using coney::ai::GoalType;
using coney::ai::TacticAttack;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// A gang of kind `kind` named `name` with an AI member at each of `places`.
int makeGang(AiScene& scene, int kind, const std::string& name, const std::vector<Vec3>& places,
             std::vector<Brain*>& members) {
    const int gang = scene.brains.gangs().create(kind, name);
    for (const Vec3& feet : places) {
        Brain& member = scene.add(feet, 0.0F);
        scene.brains.gangs().addMember(gang, member);
        members.push_back(&member);
    }
    return gang;
}

// The gang's attack tactic; fails the test when it has none.
TacticAttack& attackOf(Gangs& gangs, int gang) {
    auto* tactic = dynamic_cast<TacticAttack*>(gangs.find(gang)->tactic());
    REQUIRE(tactic != nullptr);
    return *tactic;
}

// The codes the callback `function` was fired with for `gang`, oldest first.
std::vector<int> codes(const AiScene& scene, const std::string& function, int gang) {
    std::vector<int> found;
    for (const coney::test::ScriptCall& call : scene.services.calls) {
        if (call.function == function && call.args.size() == 2 && call.args[0] == gang) {
            found.push_back(static_cast<int>(call.args[1]));
        }
    }
    return found;
}

// Whether `brain` lists `other` as an enemy.
bool lists(const Brain& brain, const Brain& other) {
    return std::ranges::find(brain.enemies(), &other) != brain.enemies().end();
}

// The goal type on top of `brain`'s stack; nothing when it is empty.
std::optional<GoalType> top(const Brain& brain) {
    const coney::ai::Goal* goal = brain.topGoal();
    return goal != nullptr ? std::optional<GoalType>(goal->type()) : std::nullopt;
}

// A goal of any type that does nothing: something to keep a stack busy or to stand for a goal Coney does not build.
class StubGoal final : public coney::ai::Goal {
  public:
    explicit StubGoal(GoalType type) : Goal(type) {}
    [[nodiscard]] coney::ai::GoalStatus process(Brain& /*brain*/) override { return coney::ai::GoalStatus::Stop; }
};

} // namespace

TEST_CASE("Tactic_Attack sets an attack tactic on a gang in use, timed from the binding", "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}}, ours);
    gangs.update(2000);

    coney::ai::tacticAttack(gangs, 31, "Attack"); // no such gang: nothing
    coney::ai::tacticAttack(gangs, gang, "Attack");
    const TacticAttack& tactic = attackOf(gangs, gang);
    CHECK(tactic.type() == coney::ai::kAttackTactic);
    CHECK(tactic.callback() == "Attack");
    CHECK(tactic.nextEnemyCheckMs() == 3000);
    CHECK(tactic.nextIdlePassMs() == 5000);
    CHECK_FALSE(tactic.fled());
}

TEST_CASE("Start gives every standing member the Melee goal with no limit and response 2, but a PedReaction holder",
          "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    const int gang =
        makeGang(scene, 19, "Furies",
                 {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}, {46.0F, 40.0F, 0.0F}, {47.0F, 40.0F, 0.0F}}, ours);
    // One flees already (PedReaction), one is down; each keeps his response.
    ours[1]->pushGoal(std::make_unique<coney::ai::PedestrianReactionGoal>(Vec3{}));
    ours[1]->setThreatResponse(1);
    ours[3]->human().fighter().health().set(0);
    ours[3]->setThreatResponse(1);

    coney::ai::tacticAttack(gangs, gang, "Attack");
    gangs.update(16);
    for (const Brain* member : {ours[0], ours[2]}) {
        CHECK(top(*member) == GoalType::Melee);
        CHECK(member->threatResponse() == 2);
        CHECK(dynamic_cast<const coney::ai::MeleeGoal*>(member->topGoal()) != nullptr);
    }
    CHECK(top(*ours[1]) == GoalType::PedestrianReaction);
    CHECK(ours[1]->threatResponse() == 1);
    CHECK(ours[3]->goalCount() == 0);
    CHECK(ours[3]->threatResponse() == 1);
}

TEST_CASE("Start's spacing: 300 with no enemy listed, else 1000 over the living members", "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang =
        makeGang(scene, 19, "Furies",
                 {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}, {46.0F, 40.0F, 0.0F}, {47.0F, 40.0F, 0.0F}}, ours);
    makeGang(scene, 21, "Orphans", {{60.0F, 40.0F, 0.0F}}, theirs);

    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(16);
    CHECK(attackOf(gangs, gang).spacing() == 300);

    // The second member lists a standing enemy at a walk (gait 2 or less): the base 1000, split over four.
    ours[1]->addEnemy(*theirs[0]);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(32);
    CHECK(attackOf(gangs, gang).spacingBase() == 1000);
    CHECK(attackOf(gangs, gang).spacing() == 250);
}

TEST_CASE("the 1 s check returns 0 while any member lists an enemy and holds the idle pass, else 9",
          "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}}, ours);
    makeGang(scene, 21, "Orphans", {{60.0F, 40.0F, 0.0F}}, theirs);
    coney::ai::tacticAttack(gangs, gang, "Attack");
    gangs.update(0);
    ours[1]->addEnemy(*theirs[0]);

    // At 3001 both timers are due: the enemy check ends the update, so the idle member keeps his empty stack.
    ours[0]->clearGoals();
    gangs.update(3001);
    CHECK(codes(scene, "Attack", gang).empty());
    CHECK(ours[0]->goalCount() == 0);
    CHECK(attackOf(gangs, gang).nextIdlePassMs() == 3000);
    // The next update has no enemy check due, so the idle pass runs.
    gangs.update(3002);
    REQUIRE(ours[0]->goalCount() > 0);
    CHECK(ours[0]->goalAt(0)->type() == GoalType::Melee);
    CHECK(attackOf(gangs, gang).nextIdlePassMs() == 6002);

    // With every list empty the check returns 9 and the idle pass does not run in that update.
    ours[1]->clearEnemies();
    ours[0]->clearGoals();
    gangs.update(6100);
    CHECK(codes(scene, "Attack", gang) == std::vector<int>{coney::ai::kTacNoEnemies});
    CHECK(ours[0]->goalCount() == 0);
}

TEST_CASE("the idle pass sends each idle member to the farthest busy mate", "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    // Three idle members near 10, two busy mates at 20 (near) and 70 (far).
    const int gang = makeGang(
        scene, 19, "Furies",
        {{10.0F, 10.0F, 0.0F}, {11.0F, 10.0F, 0.0F}, {12.0F, 10.0F, 0.0F}, {20.0F, 10.0F, 0.0F}, {70.0F, 10.0F, 0.0F}},
        ours);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    for (Brain* member : ours) {
        member->clearGoals();
    }
    ours[3]->pushGoal(std::make_unique<StubGoal>(GoalType::Idle));
    ours[4]->pushGoal(std::make_unique<StubGoal>(GoalType::Idle));
    // Someone lists an enemy, so the enemy check does not report 9; it runs at 2500 and is next due after 3500, so the
    // update at 3001 runs the idle pass alone.
    ours[3]->addEnemy(scene.player());
    gangs.update(2500);
    gangs.update(3001);

    for (const Brain* idle : {ours[0], ours[1], ours[2]}) {
        REQUIRE(idle->goalCount() == 2);
        CHECK(idle->goalAt(0)->type() == GoalType::Melee);
        const auto* help = dynamic_cast<const coney::ai::HelpRespondGoal*>(idle->topGoal());
        REQUIRE(help != nullptr);
        CHECK(help->target() == ours[4]);
    }
    // The busy mates are left as they were.
    CHECK(top(*ours[3]) == GoalType::Idle);
    CHECK(ours[4]->goalCount() == 1);
}

TEST_CASE("violence nearby goes through the leader's side pick to every member with an empty stack",
          "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    std::vector<Brain*> strangers;
    const int gang =
        makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}, {46.0F, 40.0F, 0.0F}}, ours);
    const int other = makeGang(scene, 21, "Orphans", {{50.0F, 40.0F, 0.0F}}, theirs);
    makeGang(scene, 22, "Strangers", {{30.0F, 40.0F, 0.0F}, {31.0F, 40.0F, 0.0F}}, strangers);
    gangs.makeEnemies(gang, other);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    Gang& furies = *gangs.find(gang);
    ours[0]->clearGoals();
    ours[1]->clearGoals();

    // Two strangers fighting: the leader takes no side and nothing happens.
    const BrainEvent brawl{.id = coney::ai::kEventViolence, .other = strangers[0], .value = 0, .victim = strangers[1]};
    CHECK_FALSE(furies.tactic()->event(furies, *ours[2], brawl));
    CHECK(ours[0]->goalCount() == 0);

    // An Orphan hits a Fury: the idle members melee him, list and target him, and go over to help.
    const BrainEvent hit{.id = coney::ai::kEventViolence, .other = theirs[0], .value = 0, .victim = ours[2]};
    CHECK_FALSE(furies.tactic()->event(furies, *ours[2], hit));
    for (Brain* idle : {ours[0], ours[1]}) {
        REQUIRE(idle->goalCount() == 2);
        CHECK(idle->goalAt(0)->type() == GoalType::Melee);
        CHECK(top(*idle) == GoalType::HelpRespond);
        CHECK(lists(*idle, *theirs[0]));
        CHECK(idle->target() == theirs[0]);
    }
    // The busy one keeps his Melee goal alone.
    CHECK(ours[2]->goalCount() == 1);
}

TEST_CASE("a hit or a new enemy melees an own member with no fight goal; another gang's member is ignored",
          "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}}, ours);
    makeGang(scene, 21, "Orphans", {{50.0F, 40.0F, 0.0F}}, theirs);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    Gang& furies = *gangs.find(gang);

    // Busy with something else (the fight goal is not a "fight goal" to Brain_HasFightGoal): Melee on top.
    ours[0]->clearGoals();
    ours[0]->pushGoal(std::make_unique<StubGoal>(GoalType::Fight));
    CHECK_FALSE(ours[0]->hasFightGoal());
    static_cast<void>(furies.tactic()->event(furies, *ours[0], BrainEvent{.id = coney::ai::kEventDamaged}));
    CHECK(top(*ours[0]) == GoalType::Melee);
    CHECK(ours[0]->hasFightGoal());

    // A member with a Melee goal already: nothing new.
    const std::size_t before = ours[1]->goalCount();
    static_cast<void>(furies.tactic()->event(furies, *ours[1], BrainEvent{.id = coney::ai::kEventEnemyAdded}));
    CHECK(ours[1]->goalCount() == before);

    // Another gang's member: ignored.
    static_cast<void>(furies.tactic()->event(furies, *theirs[0], BrainEvent{.id = coney::ai::kEventDamaged}));
    CHECK(theirs[0]->goalCount() == 0);
}

TEST_CASE("Brain_HasFightGoal is a Melee or a LeftTurf goal on the stack", "[ai][tactics][attack]") {
    AiScene scene;
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    CHECK_FALSE(brain.hasFightGoal());
    brain.pushGoal(std::make_unique<StubGoal>(GoalType::LeftTurf));
    brain.pushGoal(std::make_unique<StubGoal>(GoalType::Idle));
    CHECK(brain.hasFightGoal());
    brain.clearGoals();
    brain.pushGoal(std::make_unique<coney::ai::FightGoal>());
    CHECK_FALSE(brain.hasFightGoal());
}

TEST_CASE("events 0x13 and 0x16 give every standing member the Melee goal again", "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}}, ours);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    Gang& furies = *gangs.find(gang);
    for (const int id : {coney::ai::kGangMessageRegive, coney::ai::kGangMessageMembers}) {
        for (Brain* member : ours) {
            member->pushGoal(std::make_unique<StubGoal>(GoalType::Idle));
        }
        static_cast<void>(furies.tactic()->event(furies, *ours[0], BrainEvent{.id = id}));
        for (const Brain* member : ours) {
            CHECK(member->goalCount() == 1);
            CHECK(top(*member) == GoalType::Melee);
        }
    }
}

TEST_CASE("flight: Start notes the living count once; beaten down, the role-11 members flee and the deaths go quiet",
          "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang = makeGang(
        scene, 19, "Furies",
        {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}, {46.0F, 40.0F, 0.0F}, {47.0F, 40.0F, 0.0F}, {48.0F, 40.0F, 0.0F}},
        ours);
    makeGang(scene, 21, "Orphans", {{60.0F, 40.0F, 0.0F}}, theirs);
    // Members 0 and 1 are of the fleeing role; the kind flees at 40 % of its start.
    gangs.setLookups(coney::ai::GangLookups{.role = [&ours](const Brain& brain) -> std::optional<int> {
                                                return &brain == ours[0] || &brain == ours[1]
                                                           ? std::optional<int>(coney::ai::kFleeRole)
                                                           : std::nullopt;
                                            },
                                            .fleeShare = [](int kind) { return kind == 19 ? 40 : 0; }});
    Gang& furies = *gangs.find(gang);
    REQUIRE(furies.canFlee()); // 1 after a reset
    CHECK(furies.fleeCount() == -1);
    for (Brain* member : ours) {
        member->addEnemy(*theirs[0]);
    }

    coney::ai::tacticAttack(gangs, gang, "Attack");
    gangs.update(0);
    CHECK(furies.fleeCount() == 5);
    CHECK(furies.clipGroup(coney::ai::kCowerClips) == 1); // gen_cower_plead
    // A second start does not count again.
    gangs.removeMember(*ours[4]);
    coney::ai::tacticAttack(gangs, gang, "Attack");
    gangs.update(16);
    CHECK(furies.fleeCount() == 5);
    TacticAttack& tactic = attackOf(gangs, gang);

    // A death before the flight: the callback hears 13.
    static_cast<void>(tactic.event(furies, *ours[3], BrainEvent{.id = coney::ai::kGangMessageHeadcount}));
    CHECK(codes(scene, "Attack", gang) == std::vector<int>{coney::ai::kTacMemberDied});

    // Three of four standing (above round(40 × 5 / 100) = 2): no flight.
    ours[3]->human().fighter().health().set(0);
    static_cast<void>(tactic.event(furies, *ours[3], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK_FALSE(tactic.fled());

    // Two standing and one more cuffed: the standing role-11 members flee from the Orphan.
    ours[2]->human().script().arrested = true;
    static_cast<void>(tactic.event(furies, *ours[2], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK(tactic.fled());
    for (const Brain* fleeing : {ours[0], ours[1]}) {
        CHECK(fleeing->threatResponse() == 0);
        REQUIRE(fleeing->goalCount() == 2);
        CHECK(fleeing->goalAt(0)->type() == GoalType::Melee);
        CHECK(top(*fleeing) == GoalType::PedestrianReaction);
    }

    // Fled: a death fires no callback, and the 1 s check never reports 9 even with no enemies.
    static_cast<void>(tactic.event(furies, *ours[3], BrainEvent{.id = coney::ai::kGangMessageHeadcount}));
    CHECK(codes(scene, "Attack", gang).size() == 1);
    for (Brain* member : ours) {
        member->clearEnemies();
    }
    gangs.update(5000);
    CHECK(codes(scene, "Attack", gang).size() == 1);

    // The end releases the cower clips.
    gangs.setTactic(gang, nullptr);
    CHECK(furies.clipGroup(coney::ai::kCowerClips) == 0);
}

TEST_CASE("flight: a share or threshold of 0 never flees, and fled is set only when a member was sent",
          "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    int share = 0;
    bool fleeingRole = true;
    gangs.setLookups(coney::ai::GangLookups{.role = [&fleeingRole](const Brain&) -> std::optional<int> {
                                                return fleeingRole ? std::optional<int>(coney::ai::kFleeRole)
                                                                   : std::nullopt;
                                            },
                                            .fleeShare = [&share](int) { return share; }});

    // A share of 0: every member down still sends nobody and leaves the gang unfled.
    std::vector<Brain*> zero;
    const int zeroGang = makeGang(scene, 19, "Zeroes", {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}}, zero);
    coney::ai::tacticAttack(gangs, zeroGang, "");
    gangs.update(0);
    for (Brain* member : zero) {
        member->human().fighter().health().set(0);
    }
    Gang& zeroes = *gangs.find(zeroGang);
    static_cast<void>(attackOf(gangs, zeroGang).event(zeroes, *zero[0], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK_FALSE(attackOf(gangs, zeroGang).fled());

    // A threshold that rounds to 0 (10 % of 3): the check does nothing, so the cuffed member left on his feet is not
    // sent either.
    share = 10;
    std::vector<Brain*> few;
    const int fewGang =
        makeGang(scene, 19, "Few", {{50.0F, 40.0F, 0.0F}, {51.0F, 40.0F, 0.0F}, {52.0F, 40.0F, 0.0F}}, few);
    coney::ai::tacticAttack(gangs, fewGang, "");
    gangs.update(16);
    few[0]->human().fighter().health().set(0);
    few[1]->human().fighter().health().set(0);
    few[2]->human().script().arrested = true;
    Gang& fewest = *gangs.find(fewGang);
    static_cast<void>(attackOf(gangs, fewGang).event(fewest, *few[0], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK_FALSE(attackOf(gangs, fewGang).fled());
    CHECK(few[2]->findGoal(GoalType::PedestrianReaction) == nullptr);

    // Beaten, but nobody of the fleeing role: nobody is sent, the gang has not fled and the 1 s check still reports 9,
    // so a script waiting for it is not left hanging.
    share = 40;
    fleeingRole = false;
    std::vector<Brain*> beaten;
    std::vector<Brain*> theirs;
    const int beatenGang = makeGang(scene, 19, "Beaten", {{60.0F, 40.0F, 0.0F}, {61.0F, 40.0F, 0.0F}}, beaten);
    makeGang(scene, 21, "Orphans", {{70.0F, 40.0F, 0.0F}}, theirs);
    coney::ai::tacticAttack(gangs, beatenGang, "Beaten");
    gangs.update(32);
    beaten[0]->human().fighter().health().set(0);
    Gang& beatenOnes = *gangs.find(beatenGang);
    static_cast<void>(
        attackOf(gangs, beatenGang).event(beatenOnes, *beaten[0], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK_FALSE(attackOf(gangs, beatenGang).fled());
    gangs.update(2000);
    CHECK(codes(scene, "Beaten", beatenGang) == std::vector<int>{coney::ai::kTacNoEnemies});
}

TEST_CASE("flight skips a member holding a PedReaction and a cuffed one, and a rerun stacks no second PedReaction",
          "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang =
        makeGang(scene, 19, "Furies",
                 {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}, {46.0F, 40.0F, 0.0F}, {47.0F, 40.0F, 0.0F}}, ours);
    makeGang(scene, 21, "Orphans", {{60.0F, 40.0F, 0.0F}}, theirs);
    gangs.setLookups(coney::ai::GangLookups{.role = [](const Brain&) -> std::optional<int> { return 11; },
                                            .fleeShare = [](int) { return 50; }});
    for (Brain* member : ours) {
        member->addEnemy(*theirs[0]);
    }
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    Gang& furies = *gangs.find(gang);
    REQUIRE(furies.fleeCount() == 4);
    TacticAttack& tactic = attackOf(gangs, gang);

    // Member 1 already runs (a PedReaction, response 1); member 2 is cuffed; member 3 is down. Two standing (0 and 1)
    // are at most round(50 × 4 / 100) = 2.
    ours[1]->pushGoal(std::make_unique<coney::ai::PedestrianReactionGoal>(Vec3{}));
    ours[1]->setThreatResponse(1);
    const std::size_t holderGoals = ours[1]->goalCount();
    ours[2]->human().script().arrested = true;
    ours[3]->human().fighter().health().set(0);
    static_cast<void>(tactic.event(furies, *ours[3], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK(tactic.fled());
    // Member 0 flees.
    REQUIRE(ours[0]->goalCount() == 2);
    CHECK(ours[0]->goalAt(0)->type() == GoalType::Melee);
    CHECK(top(*ours[0]) == GoalType::PedestrianReaction);
    CHECK(ours[0]->threatResponse() == 0);
    // The holder keeps his goals and response; the cuffed one is not sent.
    CHECK(ours[1]->goalCount() == holderGoals);
    CHECK(ours[1]->threatResponse() == 1);
    CHECK(ours[2]->findGoal(GoalType::PedestrianReaction) == nullptr);
    CHECK(ours[2]->threatResponse() == 2);

    // Event 18 again: member 0 still holds his PedReaction, so he is skipped (his response, changed since, stays).
    ours[0]->setThreatResponse(1);
    static_cast<void>(tactic.event(furies, *ours[3], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK(ours[0]->goalCount() == 2);
    CHECK(ours[0]->threatResponse() == 1);
}

TEST_CASE("Start's living count takes every member whose handle resolves, the dead included", "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}}, ours);
    makeGang(scene, 21, "Orphans", {{60.0F, 40.0F, 0.0F}}, theirs);
    gangs.setLookups(coney::ai::GangLookups{.role = {}, .fleeShare = [](int) { return 40; }});
    // A dead member is still a member (his handle resolves until he is deleted): Gang_CountLiving(g, 0) counts him.
    ours[1]->human().fighter().health().set(0);
    ours[0]->addEnemy(*theirs[0]);
    Gang& furies = *gangs.find(gang);
    CHECK(furies.living() == 2);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    CHECK(furies.fleeCount() == 2);
    CHECK(attackOf(gangs, gang).spacing() == 500);
}

TEST_CASE("a gang that cannot flee notes no count, takes no cower clip and never flees", "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}, {45.0F, 40.0F, 0.0F}}, ours);
    gangs.setLookups(coney::ai::GangLookups{.role = [](const Brain&) -> std::optional<int> { return 11; },
                                            .fleeShare = [](int) { return 100; }});
    Gang& furies = *gangs.find(gang);
    furies.setCanFlee(false);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    CHECK(furies.fleeCount() == -1);
    CHECK(furies.clipGroup(coney::ai::kCowerClips) == 0);
    TacticAttack& tactic = attackOf(gangs, gang);
    ours[0]->human().fighter().health().set(0);
    static_cast<void>(tactic.event(furies, *ours[0], BrainEvent{.id = coney::ai::kEventDown}));
    CHECK_FALSE(tactic.fled());

    // Kind 6 with team-ups takes the two cower clips whether or not it may flee.
    std::vector<Brain*> sixes;
    const int six = makeGang(scene, 6, "Sixes", {{50.0F, 40.0F, 0.0F}}, sixes);
    gangs.find(six)->orders().attackStrategies = true;
    gangs.find(six)->setCanFlee(false);
    coney::ai::tacticAttack(gangs, six, "");
    gangs.update(16);
    CHECK(gangs.find(six)->clipGroup(coney::ai::kCowerClips) == 2);
}

TEST_CASE("the alert state changes the clips and the members' turf limit once per change", "[ai][gangs][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}}, ours);
    Brain& player = scene.player();
    gangs.addMember(gang, player);
    Gang& furies = *gangs.find(gang);
    CHECK(furies.alertState() == coney::ai::kGangCalm);
    CHECK(ours[0]->keepsToTurf());

    furies.setAlertState(coney::ai::kGangFighting);
    CHECK(furies.clipGroup(coney::ai::kCombatFidgetClips) == 4);
    CHECK(furies.clipGroup(coney::ai::kTauntAdvanceClips) == 5);
    CHECK(furies.clipGroup(coney::ai::kTauntClips) == 3);
    CHECK(furies.clipGroup(coney::ai::kCheerClips) == 3);
    CHECK_FALSE(ours[0]->keepsToTurf());
    CHECK(player.keepsToTurf()); // the players are left alone

    // The same state again changes nothing: a group released meanwhile stays released.
    furies.releaseClipGroup(coney::ai::kCombatFidgetClips);
    ours[0]->setKeepsToTurf(true);
    furies.setAlertState(coney::ai::kGangFighting);
    CHECK(furies.clipGroup(coney::ai::kCombatFidgetClips) == 0);
    CHECK(ours[0]->keepsToTurf());

    // Another value: no clip change, the limit back on.
    ours[0]->setKeepsToTurf(false);
    furies.setAlertState(2);
    CHECK(furies.clipGroup(coney::ai::kTauntAdvanceClips) == 5);
    CHECK(ours[0]->keepsToTurf());

    // Calm: the groups go.
    furies.setAlertState(coney::ai::kGangFighting);
    furies.setAlertState(coney::ai::kGangCalm);
    CHECK(furies.clipGroup(coney::ai::kTauntAdvanceClips) == 0);
    CHECK(furies.clipGroup(coney::ai::kCheerClips) == 0);
    CHECK(ours[0]->keepsToTurf());
}

TEST_CASE("the alert state's kind exceptions: 5 takes no clips, 0x18 no fidgets, 0x17 no taunts",
          "[ai][gangs][attack]") {
    Gangs gangs;
    Gang& five = *gangs.find(gangs.create(5, "Five"));
    Gang& dealers = *gangs.find(gangs.create(0x18, "Dealers"));
    Gang& policeLike = *gangs.find(gangs.create(coney::ai::kPoliceLikeKind, "Cops"));
    for (Gang* gang : {&five, &dealers, &policeLike}) {
        gang->setAlertState(coney::ai::kGangFighting);
    }
    CHECK(five.clipGroup(coney::ai::kCombatFidgetClips) == 0);
    CHECK(five.clipGroup(coney::ai::kTauntAdvanceClips) == 0);
    CHECK(dealers.clipGroup(coney::ai::kCombatFidgetClips) == 0);
    CHECK(dealers.clipGroup(coney::ai::kTauntAdvanceClips) == 5);
    CHECK(policeLike.clipGroup(coney::ai::kCombatFidgetClips) == 4);
    CHECK(policeLike.clipGroup(coney::ai::kTauntAdvanceClips) == 0);
}

TEST_CASE("an attack tactic's start puts the gang on alert", "[ai][tactics][attack]") {
    AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    std::vector<Brain*> ours;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}}, ours);
    coney::ai::tacticAttack(gangs, gang, "");
    gangs.update(0);
    CHECK(gangs.find(gang)->alertState() == coney::ai::kGangFighting);
    CHECK_FALSE(ours[0]->keepsToTurf());
}
