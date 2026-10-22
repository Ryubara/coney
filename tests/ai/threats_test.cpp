// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/threats.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/enemy_scan.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "support/ai_fixtures.h"

// Threats and the gang's shared target (docs/research/ai-reimpl.md#unit-1): Gang_IsEnemyOf with the truce,
// Brain_IsThreat over the brain types, players and the Warrior hit-back flag, Gang_SetSharedTarget, the enemy scan run
// for every brain by its own update, and the fight goal's forced re-target. Synthetic scenes only.

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::Gang;
using coney::ai::Gangs;
using coney::ai::isThreat;
using coney::anim::Vec3;

namespace {

// Whether `brain` lists `other` as an enemy.
bool lists(const Brain& brain, const Brain& other) {
    return std::ranges::find(brain.enemies(), &other) != brain.enemies().end();
}

} // namespace

TEST_CASE("Gang_IsEnemyOf reads one gang's mask, never between the police kinds, and not during a truce",
          "[ai][threat]") {
    Gangs gangs;
    const int warriors = gangs.create(0, "Warriors");
    const int rogues = gangs.create(2, "Rogues");
    const int police = gangs.create(1, "Police");
    const int policeLike = gangs.create(0x17, "PoliceLike");
    const Gang* w = gangs.find(warriors);
    const Gang* r = gangs.find(rogues);
    const Gang* p = gangs.find(police);
    const Gang* q = gangs.find(policeLike);
    CHECK_FALSE(Gangs::enemies(w, r));
    gangs.makeEnemies(warriors, rogues);
    CHECK(Gangs::enemies(w, r));
    CHECK(Gangs::enemies(r, w));
    // Kinds 1 and 0x17 are never enemies, either way round, whatever their masks say.
    gangs.makeEnemies(police, policeLike);
    CHECK_FALSE(Gangs::enemies(p, q));
    CHECK_FALSE(Gangs::enemies(q, p));
    gangs.makeEnemies(police, warriors);
    CHECK(Gangs::enemies(p, w));

    // A truce of 500 ms at 1000 ms: no gang is anyone's enemy until 1500 ms.
    gangs.update(1000);
    gangs.setTruce(500);
    CHECK(gangs.truceEndMs() == 1500);
    CHECK_FALSE(Gangs::enemies(w, r));
    gangs.update(1499);
    CHECK_FALSE(Gangs::enemies(w, r));
    CHECK_FALSE(Gangs::enemies(p, w));
    gangs.update(1500);
    CHECK(Gangs::enemies(w, r));
    // 0 ends a truce at once.
    gangs.setTruce(500);
    CHECK_FALSE(Gangs::enemies(w, r));
    gangs.setTruce(0);
    CHECK(gangs.truceEndMs() == 0);
    CHECK(Gangs::enemies(w, r));
}

TEST_CASE("Brain_IsThreat: never himself, a hitting-back Warrior and any player, else enemy gangs or two players",
          "[ai][threat]") {
    coney::test::AiScene scene;
    Brain& player = scene.player();
    Brain& other = scene.add(Vec3{20.0F, 20.0F, 0.0F}, 0.0F, BrainType::Player);
    CHECK_FALSE(isThreat(player, player));
    // Two pad players threaten each other, gangs or not.
    CHECK(isThreat(player, other));
    CHECK(isThreat(other, player));

    // Each brain type, with and without the hit-back flag, against a player of no gang: only a Warrior with the flag
    // (or a player) is a threat, both ways round.
    for (int type = 0; type <= 6; ++type) {
        const auto kind = static_cast<BrainType>(type);
        Brain& ai = scene.add(Vec3{30.0F + static_cast<float>(type), 30.0F, 0.0F}, 0.0F, kind);
        const bool bothPlayers = kind == BrainType::Player;
        CHECK(isThreat(ai, player) == bothPlayers);
        CHECK(isThreat(player, ai) == bothPlayers);
        ai.setHittingBack(true);
        const bool expected = bothPlayers || kind == BrainType::Warrior;
        CHECK(isThreat(ai, player) == expected);
        CHECK(isThreat(player, ai) == expected);
        // The flag counts against a pad player only, not another AI.
        Brain& bystander = scene.add(Vec3{30.0F + static_cast<float>(type), 32.0F, 0.0F}, 0.0F, BrainType::Gang);
        CHECK_FALSE(isThreat(ai, bystander));
        CHECK_FALSE(isThreat(bystander, ai));
    }

    // Gangs: an enemy gang's member is a threat; a friend's is not; a truce lifts it.
    Gangs& gangs = scene.brains.gangs();
    const int a = gangs.create(0, "Warriors");
    const int b = gangs.create(2, "Rogues");
    Brain& warrior = scene.add(Vec3{50.0F, 50.0F, 0.0F}, 0.0F, BrainType::Warrior);
    Brain& rogue = scene.add(Vec3{52.0F, 50.0F, 0.0F}, 0.0F, BrainType::Gang);
    gangs.addMember(a, warrior);
    gangs.addMember(b, rogue);
    CHECK_FALSE(isThreat(warrior, rogue));
    gangs.makeEnemies(a, b);
    CHECK(isThreat(warrior, rogue));
    CHECK(isThreat(rogue, warrior));
    gangs.setTruce(1000);
    CHECK_FALSE(isThreat(warrior, rogue));
}

TEST_CASE("Gang_SetSharedTarget: checked through the leader, refused for a friend, listed by every AI member",
          "[ai][threat]") {
    coney::test::AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int crewId = gangs.create(0, "Warriors");
    const int rogueId = gangs.create(2, "Rogues");
    const int friendId = gangs.create(3, "Friends");
    Gang& crew = *gangs.find(crewId);
    // The player (the chief) and two AI Warriors; a rogue and a friend.
    Brain& one = scene.add(Vec3{42.0F, 40.0F, 0.0F}, 0.0F, BrainType::Warrior);
    Brain& two = scene.add(Vec3{44.0F, 40.0F, 0.0F}, 0.0F, BrainType::Warrior);
    Brain& rogue = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F, BrainType::Gang);
    Brain& pal = scene.add(Vec3{46.0F, 50.0F, 0.0F}, 180.0F, BrainType::Gang);
    // No leader yet: nothing happens.
    crew.setSharedTarget(&rogue);
    CHECK(crew.chosenTarget() == nullptr);
    gangs.addMember(crewId, scene.player());
    gangs.addMember(crewId, one);
    gangs.addMember(crewId, two);
    gangs.addMember(rogueId, rogue);
    gangs.addMember(friendId, pal);
    gangs.makeFriends(crewId, friendId);

    // A friend: refused, the lists untouched.
    crew.setSharedTarget(&pal);
    CHECK(crew.chosenTarget() == nullptr);
    CHECK(one.enemies().empty());

    // An invalid human (not targetable): nothing happens.
    rogue.human().script().targetable = false;
    crew.setSharedTarget(&rogue);
    CHECK(crew.chosenTarget() == nullptr);
    rogue.human().script().targetable = true;

    // A valid one, not even an enemy gang's: the target, and every AI member lists him; the player does not.
    crew.setSharedTarget(&rogue);
    CHECK(crew.chosenTarget() == &rogue);
    CHECK(lists(one, rogue));
    CHECK(lists(two, rogue));
    CHECK_FALSE(lists(scene.player(), rogue));

    // The null target only clears it.
    crew.setSharedTarget(nullptr);
    CHECK(crew.chosenTarget() == nullptr);
    CHECK(lists(one, rogue));
    CHECK(lists(two, rogue));
}

TEST_CASE("every AI brain scans from its own update, at most five an update, each on its own interval",
          "[ai][threat][scan]") {
    coney::test::AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int players = gangs.create(0, "Players");
    const int guardsId = gangs.create(2, "Guards");
    gangs.addMember(players, scene.player());
    std::vector<Brain*> guards;
    guards.reserve(7);
    for (int k = 0; k < 7; ++k) {
        // Warriors (whose new enemy sends no help call) in a row 10 m in front of the player, facing him.
        guards.push_back(&scene.add(Vec3{37.0F + static_cast<float>(k), 50.0F, 0.0F}, 180.0F, BrainType::Warrior));
        gangs.addMember(guardsId, *guards.back());
    }
    gangs.makeEnemies(players, guardsId);
    const auto listing = [&] {
        return std::ranges::count_if(guards, [&](const Brain* guard) { return lists(*guard, scene.player()); });
    };
    // No tactic: the first update scans five, the next the other two.
    scene.run(1);
    CHECK(listing() == coney::ai::kScanTokens);
    scene.run(1);
    CHECK(listing() == 7);

    // Each guard forgets him and lists him again at his own next scan, 2 s after his last.
    for (Brain* guard : guards) {
        guard->forget(scene.player());
    }
    scene.run(1);
    CHECK(listing() == 0); // none due yet
    scene.run(58);         // 2 s after the first five
    CHECK(listing() == coney::ai::kScanTokens);
    scene.run(1);
    CHECK(listing() == 7);
}

TEST_CASE("the fight goal's re-target takes the threat straight ahead, forcing a slot on him", "[ai][threat]") {
    coney::test::AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int players = gangs.create(0, "Players");
    const int rogues = gangs.create(2, "Rogues");
    Brain& player = scene.player();
    // The fighter 1.1 m behind the player, facing past him at his target 2.6 m off; a mate of his holds the player's
    // only slot from nearer.
    Brain& fighter = scene.add(Vec3{40.0F, 38.9F, 0.0F}, 0.0F, BrainType::Gang);
    Brain& mate = scene.add(Vec3{40.0F, 40.6F, 0.0F}, 180.0F, BrainType::Gang);
    Brain& target = scene.add(Vec3{40.0F, 41.5F, 0.0F}, 180.0F, BrainType::Gang);
    gangs.addMember(players, player);
    gangs.addMember(players, target);
    gangs.addMember(rogues, fighter);
    gangs.addMember(rogues, mate);
    gangs.makeEnemies(players, rogues);
    for (Brain* calm : {&player, &mate, &target}) {
        calm->setThreatResponse(0);
    }
    player.setAttackSlotCount(1);
    mate.setTarget(&player);
    REQUIRE(player.attackSlots().size() == 1);
    fighter.setTarget(&target);
    REQUIRE(fighter.pushGoal(std::make_unique<coney::ai::FightGoal>()));
    scene.run(32);
    CHECK(fighter.target() == &player);
    CHECK(fighter.hasAttackSlot());
    // The farther newcomer took the nearer mate's slot, and the mate lost his target.
    CHECK(player.attackSlots().size() == 1);
    CHECK(mate.target() == nullptr);
}

TEST_CASE("the fight goal's first re-target runs on the update after it starts", "[ai][threat]") {
    coney::test::AiScene scene;
    Gangs& gangs = scene.brains.gangs();
    const int players = gangs.create(0, "Players");
    const int rogues = gangs.create(2, "Rogues");
    Brain& player = scene.player();
    // The fighter 1.1 m behind the player, facing past him at his target 2.6 m off.
    Brain& fighter = scene.add(Vec3{40.0F, 38.9F, 0.0F}, 0.0F, BrainType::Gang);
    Brain& target = scene.add(Vec3{40.0F, 41.5F, 0.0F}, 180.0F, BrainType::Gang);
    gangs.addMember(players, player);
    gangs.addMember(players, target);
    gangs.addMember(rogues, fighter);
    gangs.makeEnemies(players, rogues);
    for (Brain* calm : {&player, &target}) {
        calm->setThreatResponse(0);
    }
    fighter.setTarget(&target);
    REQUIRE(fighter.pushGoal(std::make_unique<coney::ai::FightGoal>()));
    // The start makes it due at once: the second update re-targets, not one a second later.
    scene.run(1);
    CHECK(fighter.target() == &target);
    const auto* goal = dynamic_cast<const coney::ai::FightGoal*>(fighter.findGoal(coney::ai::GoalType::Fight));
    REQUIRE(goal != nullptr);
    fighter.clearActions();
    const int kind = goal->kind();
    scene.run(1);
    CHECK(fighter.target() == &player);
    // The update that re-targets stops there (FightGoal_Process 0x002b3ab0): nothing queued, the kind kept.
    CHECK(fighter.actionCount() == 0);
    CHECK(goal->kind() == kind);
}
