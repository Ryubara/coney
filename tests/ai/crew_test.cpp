// SPDX-License-Identifier: GPL-3.0-or-later
// The Warriors' default command and the chief's automatic commands (docs/research/ai-gangs.md#warrior-commands): the
// follow tactic's goals, the follow goal's fight mode, and CrewOrders' switches to attack and back. Synthetic humans on
// a flat floor.
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/crew.h"
#include "ai/formations.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/move_action.h"
#include "ai/route_planner.h"
#include "ai/turn_action.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "support/ai_fixtures.h"
#include "support/path_fixtures.h"
#include "world/path_map.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// The distance in plan between two points.
float planDistance(Vec3 a, Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// The player's crew: a gang holding the player as chief and `count` AI members `away` metres east of him.
int makeCrew(AiScene& scene, int count, float away, std::vector<Brain*>& members) {
    const int gang = scene.brains.gangs().create(1, "Warriors");
    scene.brains.gangs().addMember(gang, scene.player());
    for (int k = 0; k < count; ++k) {
        Brain& member = scene.add({40.0F + away, 40.0F + 2.0F * static_cast<float>(k), 0.0F}, 0.0F);
        scene.brains.gangs().addMember(gang, member);
        members.push_back(&member);
    }
    return gang;
}

// Gives `gang` the follow tactic for the player.
void follow(AiScene& scene, int gang) {
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::WarriorFollowTactic>(
                                             scene.player().handle(), scene.services, scene.brains.formations()));
}

} // namespace

TEST_CASE("the follow tactic gives each AI member the follow goal and leaves the chief alone", "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 2, 4.0F, members);
    follow(scene, gang);
    scene.run(1);

    for (const Brain* member : members) {
        REQUIRE(member->goalCount() > 0);
        CHECK(member->topGoal()->type() == GoalType::FollowPlayer);
    }
    CHECK(scene.player().goalCount() == 0);
    const auto* goal = dynamic_cast<const coney::ai::FollowPlayerGoal*>(members[0]->topGoal());
    REQUIRE(goal != nullptr);
    CHECK(goal->leader() == scene.player().handle());
    CHECK(goal->distance() == coney::ai::kCrewFollowDistance);
    CHECK(goal->mode() == 3);
}

TEST_CASE("re-adding a member already in the crew gives every member the follow goal again", "[ai][crew]") {
    // level80's EndStartCam: GangBrFlush(0) empties the members' stacks under the running follow tactic, then
    // GangAddMember for humans already in gang 0 gives them back the follow (event 0x16 with 1).
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 2, 4.0F, members);
    follow(scene, gang);
    scene.run(1);
    for (Brain* member : members) {
        member->flush();
        CHECK(member->goalCount() == 0);
    }
    scene.run(5);
    CHECK(members[0]->goalCount() == 0); // the tactic's Process gives nothing by itself
    scene.brains.gangs().addMember(gang, *members[0]);
    for (const Brain* member : members) {
        REQUIRE(member->goalCount() > 0);
        CHECK(member->topGoal()->type() == GoalType::FollowPlayer);
        CHECK(member->gang() == scene.brains.gangs().find(gang));
    }
    // The gang keeps one entry per member.
    CHECK(scene.brains.gangs().find(gang)->members().size() == 3);
}

TEST_CASE("an add before the follow tactic's first update starts it first, so no follow goal is doubled",
          "[ai][crew]") {
    // level3's checkpoint 3: the script sets the follow tactic and adds the crew in the same step. Gang_OnEvent runs
    // the gang's update for a tactic not yet started (0x00165144), so the start's goals are given, then the event's
    // give pops them to the base and pushes one again.
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 2, 4.0F, members);
    follow(scene, gang);
    scene.brains.gangs().addMember(gang, *members[0]);
    const coney::ai::Gang* crew = scene.brains.gangs().find(gang);
    REQUIRE(crew->tactic() != nullptr);
    CHECK(crew->tactic()->started());
    for (const Brain* member : members) {
        CHECK(member->goalCount() == 1);
        REQUIRE(member->topGoal() != nullptr);
        CHECK(member->topGoal()->type() == GoalType::FollowPlayer);
    }
    scene.run(1);
    for (const Brain* member : members) {
        CHECK(member->goalCount() == 1);
    }
}

TEST_CASE("followers join the chief's formation and walk to their slots", "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 2, 14.0F, members);
    follow(scene, gang);
    const float before = members[0]->distanceTo(scene.player());
    scene.run(150);

    for (const Brain* member : members) {
        CHECK(member->following() != nullptr);
    }
    CHECK(members[0]->distanceTo(scene.player()) < before - 5.0F);
}

TEST_CASE("a follower whose crewmate is attacked faces the attacker and never attacks", "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, 3.0F, members);
    follow(scene, gang);
    scene.run(2);

    // An enemy two metres from the follower goes for it.
    Brain& enemy = scene.add({45.0F, 40.0F, 0.0F}, 0.0F);
    enemy.addEnemy(*members[0]);
    enemy.setTarget(members[0]);
    REQUIRE(members[0]->attackSlots().size() == 1);
    scene.run(30);

    CHECK(members[0]->target() == &enemy);
    CHECK(members[0]->topGoal()->type() == GoalType::FollowPlayer);
    CHECK(enemy.human().fighter().health().value() == 1400);
}

TEST_CASE("the chief's automatic commands: attack 1.5 s after his target is close while he has attackers, follow "
          "1.5 s after the fight",
          "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    static_cast<void>(makeCrew(scene, 1, 3.0F, members));
    Brain& chief = scene.player();
    coney::ai::CrewOrders orders;

    // Nothing while no one is close.
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewFollow, 0).has_value());

    // An attacker close while the chief has no target of his own (WarChief_IsTargetInRange reads human +0xc8): nothing,
    // however long it lasts.
    Brain& enemy = scene.add({41.0F, 40.0F, 0.0F}, 0.0F);
    enemy.setTarget(&chief);
    REQUIRE(chief.attackSlots().size() == 1);
    REQUIRE(chief.human().fighter().target() == nullptr);
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewFollow, 100).has_value());
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewFollow, 900).has_value());

    // His stance takes the enemy 1 m from him as his target: the attack follows 1.5 s later.
    scene.run(2);
    REQUIRE(chief.human().fighter().target() == &enemy.human());
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewFollow, 1000).has_value());
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewFollow, 2400).has_value());
    CHECK(orders.update(chief, coney::ai::kCrewFollow, 2500) == coney::ai::kCrewAttack);

    // Under attack, a member with an enemy keeps it.
    members[0]->addEnemy(enemy);
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewAttack, 3000).has_value());
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewAttack, 9000).has_value());

    // The enemy gone, follow comes back 1.5 s later.
    enemy.setTarget(nullptr);
    scene.brains.remove(enemy.human());
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewAttack, 10000).has_value());
    CHECK(orders.update(chief, coney::ai::kCrewAttack, 11500) == coney::ai::kCrewFollow);
}

TEST_CASE("the hold tactic gives each AI member a hold at his own spot and turns them outward", "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 3, 3.0F, members);
    std::vector<coney::anim::Vec3> spots;
    spots.reserve(members.size());
    for (const Brain* member : members) {
        spots.push_back(member->human().position());
    }
    scene.brains.gangs().setTactic(
        gang, std::make_unique<coney::ai::WarriorHoldTactic>(scene.player().handle(), scene.services));
    scene.run(1);

    for (std::size_t k = 0; k < members.size(); ++k) {
        REQUIRE(members[k]->goalCount() > 0);
        CHECK(members[k]->topGoal()->type() == GoalType::HoldPosition);
        const auto* goal = dynamic_cast<const coney::ai::HoldPositionGoal*>(members[k]->topGoal());
        REQUIRE(goal != nullptr);
        CHECK(goal->radius() == coney::ai::kCrewHoldRadius);
        CHECK(goal->point().x == spots[k].x);
        CHECK(goal->point().y == spots[k].y);
    }
    CHECK(scene.player().goalCount() == 0);

    // No enemies about: the turns come 2-4 s later, a third of a turn apart from the chief's heading (a turn ends
    // within kTurnDoneAngle of it).
    scene.run(150);
    const float chief = scene.player().human().heading();
    for (std::size_t k = 0; k < members.size(); ++k) {
        const float want =
            coney::human::wrapAngle(chief + 2.0F * std::numbers::pi_v<float> / 3.0F * static_cast<float>(k));
        CHECK(std::fabs(coney::human::wrapAngle(members[k]->human().heading() - want)) <= coney::ai::kTurnDoneAngle);
    }
}

TEST_CASE("a holding Warrior walks back to his spot and fights only an enemy after him", "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, 3.0F, members);
    Brain& member = *members[0];
    const coney::anim::Vec3 spot = member.human().position();
    scene.brains.gangs().setTactic(
        gang, std::make_unique<coney::ai::WarriorHoldTactic>(scene.player().handle(), scene.services));
    scene.run(1);

    // Pushed 4 m off his spot, he walks back within the radius (once his outward turn, 2-4 s off, is done).
    member.human().place({spot.x + 4.0F, spot.y, spot.z}, member.human().heading());
    scene.run(300);
    CHECK(std::hypot(member.human().position().x - spot.x, member.human().position().y - spot.y) <=
          coney::ai::kCrewHoldRadius);

    // An enemy in reach who is not after him: watched, not fought.
    Brain& enemy = scene.add({spot.x + 1.2F, spot.y, 0.0F}, 0.0F);
    member.addEnemy(enemy);
    scene.run(30);
    CHECK(member.topGoal()->type() == GoalType::HoldPosition);

    // Once he goes for him: a fight from the spot.
    enemy.addEnemy(member);
    enemy.setTarget(&member);
    scene.run(5);
    CHECK(member.findGoal(GoalType::Fight) != nullptr);
    CHECK(member.findGoal(GoalType::HoldPosition) != nullptr);
}

TEST_CASE("the attack tactic gives each AI member FollowAndAttack, and he fights the crew's attacker", "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 2, 3.0F, members);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::WarriorAttackTactic>(
                                             scene.player().handle(), scene.services, scene.brains.formations()));
    scene.run(1);
    for (const Brain* member : members) {
        REQUIRE(member->goalCount() > 0);
        CHECK(member->topGoal()->type() == GoalType::FollowAndAttack);
        CHECK(member->following() != nullptr);
    }
    CHECK(scene.player().goalCount() == 0);

    // An enemy goes for the chief 6 m away: the crew closes on him and fights (synthetic humans have no attack clips,
    // so the fight goal is checked, not the hits).
    Brain& enemy = scene.add({46.0F, 40.0F, 0.0F}, 0.0F);
    enemy.addEnemy(scene.player());
    enemy.setTarget(&scene.player());
    REQUIRE(scene.player().attackSlots().size() == 1);
    scene.run(150);
    CHECK(members[0]->target() == &enemy);
    CHECK(members[0]->topGoal()->type() == GoalType::Fight);
    CHECK(members[0]->findGoal(GoalType::FollowAndAttack) != nullptr);
    CHECK(members[0]->distanceTo(enemy) < members[0]->meleeFar());
}

TEST_CASE("an attacking Warrior with no enemy comes back to within 8 m of the chief", "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, 20.0F, members);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::WarriorAttackTactic>(
                                             scene.player().handle(), scene.services, scene.brains.formations()));
    scene.run(300);
    CHECK(members[0]->distanceTo(scene.player()) <= coney::ai::kCrewAttackStay);
    CHECK(members[0]->topGoal()->type() == GoalType::FollowAndAttack);
}

TEST_CASE("FollowFormation's speed: the leader's within reach, a run times the distance far behind, the walk near",
          "[ai][crew]") {
    // The page's walk, jog, run and sprint (docs/research/ai-gangs.md#warrior-follow, "The speed").
    const coney::human::Speeds speeds{
        .base = 1.0F, .sneak = 1.0F, .walk = 1.65F, .jog = 3.4F, .run = 7.8F, .sprint = 10.25F};
    const float top = 1.25F * speeds.sprint;
    using coney::ai::followSpeed;
    // Within the arrival distance: the leader's speed, capped at 1.25 x sprint.
    CHECK(followSpeed(0.5F, 0.75F, 1.0F, false, speeds).speed == 1.0F);
    CHECK(followSpeed(0.5F, 0.75F, 20.0F, false, speeds).speed == top);
    // A running leader 12.95 m ahead: f = 0.1 x 12.2 + 1, the run x f over the clamp: 1.25 x sprint (12.81 m/s).
    const coney::ai::FollowPace running = followSpeed(12.95F, 0.75F, 7.8F, false, speeds);
    CHECK(running.speed == top);
    CHECK(running.gait == 4);
    // A standing leader 13.75 m off: f = 1.3, the run's 7.8 x 1.3.
    const coney::ai::FollowPace far = followSpeed(13.75F, 0.75F, 0.0F, false, speeds);
    CHECK(std::fabs(far.speed - (7.8F * 1.3F)) < 1e-4F);
    CHECK(far.gait == 4);
    // 5.75 m: the jog x 0.5.
    const coney::ai::FollowPace jog = followSpeed(5.75F, 0.75F, 0.0F, false, speeds);
    CHECK(std::fabs(jog.speed - 1.7F) < 1e-4F);
    CHECK(jog.gait == 3);
    // Near, behind a walking leader: f is small, so the walk (the floor).
    CHECK(followSpeed(1.75F, 0.75F, 1.63F, false, speeds).speed == speeds.walk);
    CHECK(followSpeed(1.75F, 0.75F, 1.9F, false, speeds).speed == speeds.walk);
    // With attackers f gains 1: 2.75 m, f = 1.2, the walk x 1.2.
    const coney::ai::FollowPace attacked = followSpeed(2.75F, 0.75F, 0.0F, true, speeds);
    CHECK(std::fabs(attacked.speed - (1.65F * 1.2F)) < 1e-4F);
    CHECK(attacked.gait == 2);
}

TEST_CASE("a follower with no usable slot follows the human it queues behind within 2.5 m", "[ai][crew]") {
    AiScene scene;
    Brain& first = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& second = scene.add({48.0F, 40.0F, 0.0F}, 0.0F);
    const Brain& outsider = scene.add({30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Formation* formation = scene.brains.formations().of(scene.player(), true);
    REQUIRE(formation != nullptr);
    formation->setSlotCount(1, 1, 0);
    formation->setSlot(0, 2.0F, 0.0F, 0, 0);
    REQUIRE(formation->join(first));
    REQUIRE(formation->join(second));
    formation->plan(0);

    // The one nearer the slot takes it; the other queues behind him.
    const std::optional<coney::ai::FollowPoint> slotted = formation->followPoint(first, 0.75F);
    const std::optional<coney::ai::FollowPoint> queued = formation->followPoint(second, 0.75F);
    REQUIRE(slotted.has_value());
    REQUIRE(queued.has_value());
    if (!slotted || !queued) {
        return;
    }
    CHECK(slotted->slot);
    CHECK(slotted->radius == 0.75F);
    CHECK(slotted->pace == &scene.player());
    CHECK_FALSE(queued->slot);
    CHECK(queued->radius == coney::ai::kQueueFollowRadius);
    CHECK(queued->pace == &first);
    CHECK(queued->point == first.human().position());
    CHECK_FALSE(formation->followPoint(outsider, 0.75F).has_value());
}

TEST_CASE("a follower within 9 m in a straight line stands; one 12 m behind runs to his slot and slows to the walk",
          "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, 6.0F, members);
    follow(scene, gang);
    Brain& near = *members[0];
    const Vec3 nearStart = near.human().position();
    scene.run(60);
    CHECK(near.findGoal(GoalType::FollowFormation) == nullptr);
    CHECK(planDistance(near.human().position(), nearStart) < 0.05F);

    // Another 12 m off: beyond 9 m it follows, at the run times f (behind the chief within 2.5 m until the next plan
    // gives it a slot).
    Brain& far = scene.add({52.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(gang, far);
    scene.run(2);
    REQUIRE(far.findGoal(GoalType::FollowFormation) != nullptr);
    const auto* move = dynamic_cast<const coney::ai::MoveAction*>(far.frontAction());
    REQUIRE(move != nullptr);
    REQUIRE(far.following() != nullptr);
    const std::optional<coney::ai::FollowPoint> point = far.following()->followPoint(far, coney::ai::kCrewSlotReach);
    REQUIRE(point.has_value());
    if (move == nullptr || !point) {
        return;
    }
    const float away = coney::anim::distance(far.human().position(), point->point);
    const coney::ai::FollowPace expected =
        coney::ai::followSpeed(away, point->radius, 0.0F, false, far.human().speeds());
    REQUIRE(move->speed().has_value());
    CHECK(std::fabs(move->speed().value_or(0.0F) - expected.speed) < 0.2F);
    CHECK(move->speed().value_or(0.0F) > 4.0F * far.human().speeds().walk);
    CHECK(move->request().gait == 4);

    // Near the slot (within 1.5 m) the move asks for the walk, and it arrives and stands.
    float nearSpeed = 0.0F;
    for (int k = 0; k < 300 && nearSpeed == 0.0F; ++k) {
        scene.run(1);
        const auto* now = dynamic_cast<const coney::ai::MoveAction*>(far.frontAction());
        if (now != nullptr && coney::anim::distance(far.human().position(), point->point) < 1.5F) {
            nearSpeed = now->speed().value_or(0.0F);
        }
    }
    CHECK(nearSpeed == far.human().speeds().walk);
    scene.run(90);
    CHECK(far.findGoal(GoalType::FollowFormation) == nullptr);
    CHECK(coney::anim::distance(far.human().position(), point->point) < 1.0F);
}

TEST_CASE("a leader walking round an L-shaped wall keeps his two followers moving, each behind the one ahead",
          "[ai][crew]") {
    // An L of path polygons, a corridor east along y 10-14 and one north along x 66-70; nodes at its ends and corner.
    // The formation's two slots lie 3 m to either side of the leader, off the corridor, so neither is ever usable:
    // the followers queue, the second behind the first.
    coney::test::PathBuilder builder;
    const std::uint32_t corner = builder.polygon(
        {{50.0F, 10.0F}, {70.0F, 10.0F}, {70.0F, 30.0F}, {66.0F, 30.0F}, {66.0F, 14.0F}, {50.0F, 14.0F}});
    builder.node(corner, 52.0F, 12.0F);
    builder.node(corner, 68.0F, 12.0F);
    builder.node(corner, 68.0F, 28.0F);
    builder.link(0, 1);
    builder.link(1, 2);
    const coney::world::PathMap map = builder.build();
    coney::ai::RoutePlanner planner(map);
    AiScene scene;
    scene.brains.setPlanner(&planner);
    Brain& leader = scene.add({56.0F, 12.0F, 0.0F}, -90.0F);
    Brain& first = scene.add({53.0F, 12.0F, 0.0F}, -90.0F);
    Brain& second = scene.add({51.0F, 12.0F, 0.0F}, -90.0F);
    coney::ai::Formation* formation = scene.brains.formations().of(leader, true);
    REQUIRE(formation != nullptr);
    formation->setSlotCount(2, 2, 0);
    formation->setSlot(0, 3.0F, 0.0F, 0, 0);
    formation->setSlot(1, -3.0F, 0.0F, 0, 0);
    for (Brain* follower : {&first, &second}) {
        REQUIRE(follower->pushGoal(std::make_unique<coney::ai::FollowPlayerGoal>(
            scene.services, scene.brains.formations(), leader.handle(), coney::ai::kCrewFollowDistance, 3)));
    }
    // The leader walks round the corner to the far end.
    leader.queueAction(std::make_unique<coney::ai::MoveAction>(coney::ai::MoveRequest{
        .point = {68.0F, 28.0F, 0.0F}, .radius = 0.5F, .gait = 2, .option = false, .delayMs = 0, .flagKind = false}));
    scene.run(30 * 30);

    CHECK(planDistance(leader.human().position(), {68.0F, 28.0F, 0.0F}) < 1.0F);
    const coney::ai::Follower* firstEntry = formation->follower(first);
    const coney::ai::Follower* secondEntry = formation->follower(second);
    REQUIRE(firstEntry != nullptr);
    REQUIRE(secondEntry != nullptr);
    // Both came round the corner, each within 2.5 m (and a step) of the human it queues behind.
    for (const coney::ai::Follower* entry : {firstEntry, secondEntry}) {
        CHECK(entry->slot < 0);
        REQUIRE(entry->behind != nullptr);
        CHECK(entry->brain->human().position().y > 20.0F);
        CHECK(entry->brain->distanceTo(*entry->behind) < coney::ai::kQueueFollowRadius + 0.5F);
    }
}

TEST_CASE("a follower across a kind-8 fence from its leader takes the route over the fence and climbs", "[ai][crew]") {
    // A yard round the chief at (40, 40) cut by a fence (a hole of its area across x 32-48 at y 38); a node either
    // side, linked by a climb.
    coney::test::PathBuilder builder;
    const std::uint32_t yard = builder.rectangle(30.0F, 50.0F, 30.0F, 50.0F);
    builder.hole(32.0F, 48.0F, 37.9F, 38.1F);
    builder.node(yard, 40.0F, 37.5F);
    builder.node(yard, 40.0F, 38.5F);
    builder.link(0, 1, coney::ai::edge_flag::kClimb);
    const coney::world::PathMap map = builder.build();
    coney::ai::RoutePlanner planner(map);
    AiScene scene;
    scene.brains.setPlanner(&planner);
    const int gang = scene.brains.gangs().create(1, "Warriors");
    scene.brains.gangs().addMember(gang, scene.player());
    Brain& member = scene.add({40.0F, 33.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(gang, member);
    follow(scene, gang);

    // 7 m from the chief, within 9 m, but the fence cuts the straight line: it follows, over the fence's climb.
    bool climbs = false;
    for (int k = 0; k < 150 && !climbs; ++k) {
        scene.run(1);
        const auto* move = dynamic_cast<const coney::ai::MoveAction*>(member.frontAction());
        climbs = move != nullptr && move->routed() && member.findGoal(GoalType::FollowFormation) != nullptr &&
                 (member.human().traversal() == coney::human::Traversal::Climbing ||
                  member.human().record().climbToward.has_value());
    }
    CHECK(climbs);
}

TEST_CASE("every formation plan puts the follower's new point into its running move: a slot's at 0.1 m, the one "
          "ahead's at 1.0 m",
          "[ai][crew]") {
    AiScene scene;
    Brain& leader = scene.add({40.0F, 60.0F, 0.0F}, 0.0F);
    Brain& follower = scene.add({40.0F, 40.0F, 0.0F}, 0.0F);
    // A bystander in the formation with a move of its own and no follow goal: a plan leaves its move alone.
    Brain& bystander = scene.add({30.0F, 40.0F, 0.0F}, 0.0F);
    coney::ai::Formation* formation = scene.brains.formations().of(leader, true);
    REQUIRE(formation != nullptr);
    formation->setSlotCount(1, 1, 0);
    formation->setSlot(0, 2.0F, 0.0F, 0, 0);
    REQUIRE(formation->join(bystander));
    bystander.queueAction(std::make_unique<coney::ai::MoveAction>(coney::ai::MoveRequest{
        .point = {30.0F, 70.0F, 0.0F}, .radius = 0.5F, .gait = 2, .option = false, .delayMs = 0, .flagKind = false}));
    REQUIRE(follower.pushGoal(std::make_unique<coney::ai::FollowPlayerGoal>(
        scene.services, scene.brains.formations(), leader.handle(), coney::ai::kCrewFollowDistance, 3)));
    scene.run(3);
    REQUIRE(follower.topGoal() != nullptr);
    REQUIRE(follower.topGoal()->type() == GoalType::FollowFormation);
    auto* move = dynamic_cast<coney::ai::MoveAction*>(follower.frontAction());
    REQUIRE(move != nullptr);

    // The leader steps 6 m east: the next update plans, and the move goes to the new slot at once, radius 0.1 m.
    leader.human().place({46.0F, 60.0F, 0.0F}, 0.0F);
    scene.run(1);
    const std::optional<Vec3> slot = formation->slotPoint(follower);
    REQUIRE(slot.has_value());
    REQUIRE(follower.frontAction() == move);
    CHECK(coney::anim::distance(move->request().point, slot.value_or(Vec3{})) < 0.07F);
    CHECK(std::fabs(slot.value_or(Vec3{}).x - 48.0F) < 0.1F);
    CHECK(move->request().radius == coney::ai::kFollowSlotMoveRadius);
    REQUIRE(bystander.frontAction() != nullptr);
    CHECK(dynamic_cast<coney::ai::MoveAction*>(bystander.frontAction())->request().point == Vec3{30.0F, 70.0F, 0.0F});

    // With no slot, the next plan gives it the leader's own position, radius 1.0 m.
    formation->setSlotCount(0, 0, 0);
    leader.human().place({52.0F, 60.0F, 0.0F}, 0.0F);
    scene.run(1);
    REQUIRE(follower.frontAction() == move);
    CHECK(planDistance(move->request().point, {52.0F, 60.0F, 0.0F}) < 0.07F);
    CHECK(move->request().radius == coney::ai::kRetargetQueueRadius);
}

TEST_CASE("while a move left over from FollowFormation runs, the follow goal does nothing; then it stands",
          "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, 12.0F, members);
    follow(scene, gang);
    Brain& member = *members[0];
    // 12 m off: it follows; then its FollowFormation ends (as when the leader stands and it is within reach of its
    // slot) with the move still running.
    scene.run(2);
    REQUIRE(member.topGoal() != nullptr);
    REQUIRE(member.topGoal()->type() == GoalType::FollowFormation);
    REQUIRE(member.actionCount() == 1);
    member.popGoal();
    REQUIRE(member.topGoal()->type() == GoalType::FollowPlayer);

    // Beyond 9 m the follow goal's Process would push FollowFormation again; it is not run while the move runs.
    int pushed = 0;
    int moving = 0;
    for (int k = 0; k < 240 && member.actionCount() > 0; ++k) {
        scene.run(1);
        pushed += member.findGoal(GoalType::FollowFormation) != nullptr ? 1 : 0;
        moving += member.actionCount() > 0 ? 1 : 0;
    }
    CHECK(pushed == 0);
    CHECK(moving > 30);
    CHECK(member.actionCount() == 0);
    // The move took it near its slot, within 9 m in line: the follow goal stands.
    scene.run(60);
    CHECK(member.findGoal(GoalType::FollowFormation) == nullptr);
    CHECK(member.distanceTo(scene.player()) < coney::ai::kCrewFollowDistance);
}
