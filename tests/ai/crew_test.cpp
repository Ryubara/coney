// SPDX-License-Identifier: GPL-3.0-or-later
// The Warriors' default command and the chief's automatic commands (docs/research/ai.md#warrior-commands): the follow
// tactic's goals, the follow goal's fight mode, and CrewOrders' switches to attack and back. Synthetic humans on a
// flat floor.
#include <cmath>
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
#include "ai/turn_action.h"
#include "human/locomotion.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::test::AiScene;

namespace {

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

TEST_CASE("the chief's automatic commands: attack 1.5 s after an attacker is close, follow 1.5 s after the fight",
          "[ai][crew]") {
    AiScene scene;
    std::vector<Brain*> members;
    static_cast<void>(makeCrew(scene, 1, 3.0F, members));
    Brain& chief = scene.player();
    coney::ai::CrewOrders orders;

    // Nothing while no one is close.
    CHECK_FALSE(orders.update(chief, coney::ai::kCrewFollow, 0).has_value());

    Brain& enemy = scene.add({41.0F, 40.0F, 0.0F}, 0.0F);
    enemy.setTarget(&chief);
    REQUIRE(chief.attackSlots().size() == 1);
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
