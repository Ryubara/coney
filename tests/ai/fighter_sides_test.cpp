// SPDX-License-Identifier: GPL-3.0-or-later
// The gangs of the debug menus' spawned fighters (docs/guides/debug-menu.md#pages): which gang each side joins, and
// that the original's own friend and enemy tests, the enemy scan and the hit answer then treat an ally as the player's
// friend, an enemy as his enemy and a neutral as neither. Synthetic scenes.
#include "ai/fighter_sides.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brain_events.h"
#include "ai/enemy_scan.h"
#include "ai/gangs.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::FighterSide;
using coney::ai::FighterSides;
using coney::ai::Gangs;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// The player in a Warriors gang (kind 0, as the levels' scripts make it), the way a level has him.
int playerInWarriors(AiScene& scene) {
    const int warriors = scene.brains.gangs().create(coney::ai::kWarriorsKind, "Warriors");
    scene.brains.gangs().addMember(warriors, scene.player());
    return warriors;
}

// A fighter at `feet` facing `heading`, joined to `side`.
Brain& spawn(AiScene& scene, FighterSides& sides, FighterSide side, Vec3 feet, float heading = 0.0F) {
    Brain& fighter = scene.add(feet, heading);
    sides.join(side, fighter, scene.player());
    return fighter;
}

// Whether `brain` lists `other` among its enemies.
bool lists(const Brain& brain, const Brain& other) {
    return std::ranges::find(brain.enemies(), &other) != brain.enemies().end();
}

} // namespace

TEST_CASE("an enemy fighter joins a gang of the fighters' kind that is the player's gang's enemy", "[ai][fighters]") {
    // Expected: ai-reimpl.md#gang-is-enemy-of, confirmed (code)
    AiScene scene;
    const int warriors = playerInWarriors(scene);
    FighterSides sides(scene.brains.gangs());
    Brain& enemy = spawn(scene, sides, FighterSide::Enemy, {40.0F, 50.0F, 0.0F}, 180.0F);
    const coney::ai::Gang* gang = enemy.gang();
    REQUIRE(gang != nullptr);
    CHECK(gang->kind() == coney::ai::kFighterGangKind);
    CHECK(gang->name() == "Fighters");
    CHECK(sides.sideOf(gang) == FighterSide::Enemy);
    CHECK(Gangs::enemies(gang, scene.player().gang()));
    CHECK(Gangs::enemies(scene.player().gang(), gang));
    CHECK_FALSE(Gangs::friends(gang, scene.player().gang()));
    CHECK(scene.player().gang()->id() == warriors);
}

TEST_CASE("an ally is the player's friend and fights his enemies, the enemy fighters among them", "[ai][fighters]") {
    // Expected: ai-reimpl.md#gang-is-enemy-of, confirmed (code)
    AiScene scene;
    playerInWarriors(scene);
    // A gang the level's scripts made hostile to the player before any fighter came.
    const int thugs = scene.brains.gangs().create(21, "Thugs");
    scene.brains.gangs().makeEnemies(scene.player().gang()->id(), thugs);
    FighterSides sides(scene.brains.gangs());
    Brain& ally = spawn(scene, sides, FighterSide::Ally, {40.0F, 36.0F, 0.0F});
    Brain& enemy = spawn(scene, sides, FighterSide::Enemy, {40.0F, 50.0F, 0.0F}, 180.0F);

    const coney::ai::Gang* gang = ally.gang();
    REQUIRE(gang != nullptr);
    CHECK(gang != scene.player().gang());
    CHECK(gang->name() == "Allies");
    CHECK(gang->kind() == coney::ai::kWarriorsKind);
    CHECK(sides.sideOf(gang) == FighterSide::Ally);
    // A friend of the player, never his or his own side's enemy.
    CHECK(Gangs::friends(gang, scene.player().gang()));
    CHECK(Gangs::friends(scene.player().gang(), gang));
    CHECK_FALSE(Gangs::enemies(gang, scene.player().gang()));
    // The enemies of the player's gang are the ally's: the script's gang, and the enemy fighters made after him.
    CHECK(Gangs::enemies(gang, enemy.gang()));
    CHECK(Gangs::enemies(enemy.gang(), gang));
    CHECK(Gangs::enemies(gang, scene.brains.gangs().find(thugs)));
    CHECK_FALSE(Gangs::friends(gang, enemy.gang()));
}

TEST_CASE("an enemy fighter made before an ally is the ally's enemy too, and a second ally gang stays friends",
          "[ai][fighters]") {
    // Expected: ai-reimpl.md#gang-is-enemy-of, confirmed (code)
    AiScene scene;
    playerInWarriors(scene);
    FighterSides sides(scene.brains.gangs());
    Brain& enemy = spawn(scene, sides, FighterSide::Enemy, {40.0F, 50.0F, 0.0F}, 180.0F);
    Brain& first = spawn(scene, sides, FighterSide::Ally, {38.0F, 36.0F, 0.0F});
    CHECK(Gangs::enemies(first.gang(), enemy.gang()));
    // Ten fill a gang; the next goes to a gang of its own on the same side: friends of the first and the player.
    for (int i = 0; i < 9; ++i) {
        spawn(scene, sides, FighterSide::Ally, {30.0F + static_cast<float>(i), 30.0F, 0.0F});
    }
    Brain& eleventh = spawn(scene, sides, FighterSide::Ally, {42.0F, 36.0F, 0.0F});
    REQUIRE(eleventh.gang() != nullptr);
    CHECK(eleventh.gang() != first.gang());
    CHECK(eleventh.gang()->name() == "Allies2");
    CHECK(Gangs::friends(eleventh.gang(), first.gang()));
    CHECK(Gangs::friends(eleventh.gang(), scene.player().gang()));
    CHECK(Gangs::enemies(eleventh.gang(), enemy.gang()));
    CHECK(sides.sideOf(eleventh.gang()) == FighterSide::Ally);

    // A gang the player's gang gets as an enemy after the first ally gang filled reaches that full gang at the next
    // spawn, whichever gang the fighter joins.
    const int late = scene.brains.gangs().create(21, "Late");
    scene.brains.gangs().makeEnemies(scene.player().gang()->id(), late);
    CHECK_FALSE(Gangs::enemies(first.gang(), scene.brains.gangs().find(late)));
    spawn(scene, sides, FighterSide::Ally, {44.0F, 36.0F, 0.0F});
    CHECK(Gangs::enemies(first.gang(), scene.brains.gangs().find(late)));
    CHECK(Gangs::enemies(eleventh.gang(), scene.brains.gangs().find(late)));
}

TEST_CASE("a neutral fighter is nobody's friend or enemy until it is hit", "[ai][fighters]") {
    // Expected: ai.md#shared-events, confirmed (code)
    AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    playerInWarriors(scene);
    FighterSides sides(scene.brains.gangs());
    Brain& neutral = spawn(scene, sides, FighterSide::Neutral, {40.0F, 44.0F, 0.0F}, 180.0F);
    Brain& enemy = spawn(scene, sides, FighterSide::Enemy, {40.0F, 50.0F, 0.0F}, 180.0F);
    Brain& ally = spawn(scene, sides, FighterSide::Ally, {40.0F, 36.0F, 0.0F});
    const coney::ai::Gang* gang = neutral.gang();
    REQUIRE(gang != nullptr);
    CHECK(gang->kind() == coney::ai::kNeutralGangKind);
    CHECK(sides.sideOf(gang) == FighterSide::Neutral);
    for (const Brain* other : {&scene.player(), &enemy, &ally}) {
        CHECK_FALSE(Gangs::friends(gang, other->gang()));
        CHECK_FALSE(Gangs::friends(other->gang(), gang));
        CHECK_FALSE(Gangs::enemies(gang, other->gang()));
        CHECK_FALSE(Gangs::enemies(other->gang(), gang));
    }
    // Scans list none of them either way.
    scene.run(2);
    for (Brain* scanner : {&neutral, &enemy, &ally, &scene.player()}) {
        static_cast<void>(coney::ai::scanEnemies(scene.brains.gangs(), *scanner));
    }
    CHECK(neutral.enemies().empty());
    CHECK_FALSE(lists(enemy, neutral));
    CHECK_FALSE(lists(ally, neutral));
    CHECK_FALSE(lists(scene.player(), neutral));

    // Hit by the player, it answers as any soldier hit by a stranger: the two gangs become enemies and it fights him.
    static_cast<void>(coney::ai::deliverEvent(
        neutral, coney::ai::BrainEvent{.id = coney::ai::kEventDamaged, .other = &scene.player(), .value = 10}));
    CHECK(Gangs::enemies(gang, scene.player().gang()));
    CHECK(neutral.target() == &scene.player());
    // The allies, who fight whom the player fights, take it on at their next join.
    spawn(scene, sides, FighterSide::Ally, {42.0F, 36.0F, 0.0F});
    CHECK(Gangs::enemies(ally.gang(), gang));
}

TEST_CASE("an ally's scan lists the enemy fighters and never the player or his own side; the enemy's lists both",
          "[ai][fighters]") {
    // Expected: ai-gangs.md#aggro-on-sight, confirmed (code)
    AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    playerInWarriors(scene);
    FighterSides sides(scene.brains.gangs());
    // The player at (40, 40) faces +y; the allies at his back face +y too; the enemies ahead face -y.
    Brain& ally = spawn(scene, sides, FighterSide::Ally, {40.0F, 34.0F, 0.0F}, 0.0F);
    Brain& otherAlly = spawn(scene, sides, FighterSide::Ally, {43.0F, 34.0F, 0.0F}, 0.0F);
    Brain& enemy = spawn(scene, sides, FighterSide::Enemy, {40.0F, 48.0F, 0.0F}, 180.0F);
    Brain& otherEnemy = spawn(scene, sides, FighterSide::Enemy, {43.0F, 48.0F, 0.0F}, 180.0F);
    scene.run(2);
    for (Brain* scanner : {&ally, &otherAlly, &enemy, &otherEnemy}) {
        static_cast<void>(coney::ai::scanEnemies(scene.brains.gangs(), *scanner));
    }
    // The ally sees both enemies, not the player nor the other ally.
    CHECK(lists(ally, enemy));
    CHECK(lists(ally, otherEnemy));
    CHECK_FALSE(lists(ally, scene.player()));
    CHECK_FALSE(lists(ally, otherAlly));
    // The enemy sees the player and the allies, not the other enemy.
    CHECK(lists(enemy, scene.player()));
    CHECK(lists(enemy, ally));
    CHECK_FALSE(lists(enemy, otherEnemy));
    // The step's strike tests (Humans::setOpposition, which the player's hits and lock-on use) skip a friend.
    CHECK(Gangs::friends(scene.player().gang(), ally.gang()));
    CHECK_FALSE(Gangs::friends(scene.player().gang(), enemy.gang()));
}

TEST_CASE("fighters beyond a gang's capacity go to a new gang of their side, and cleared fighters leave room",
          "[ai][fighters]") {
    // Expected: unverified, #924 (the rollover is Coney's own)
    AiScene scene;
    const int warriors = playerInWarriors(scene);
    FighterSides sides(scene.brains.gangs());
    std::vector<Brain*> allies;
    allies.reserve(25);
    for (int i = 0; i < 25; ++i) {
        allies.push_back(&spawn(scene, sides, FighterSide::Ally, {10.0F + static_cast<float>(i), 30.0F, 0.0F}));
    }
    // Every ally is in a gang, none dropped to no gang, and three gangs hold them.
    std::set<int> gangs;
    for (const Brain* ally : allies) {
        REQUIRE(ally->gang() != nullptr);
        gangs.insert(ally->gang()->id());
        CHECK(sides.sideOf(ally->gang()) == FighterSide::Ally);
    }
    CHECK(gangs.size() == 3);
    // The player's own gang is as it was: nobody of his crew was pushed out.
    CHECK(scene.brains.gangs().find(warriors)->members().size() == 1);
    // A cleared fighter leaves its gang (as a deleted human does), and the next one takes the room.
    Brain* first = allies.front();
    const int firstGang = first->gang()->id();
    scene.brains.gangs().removeMember(*first);
    Brain& next = spawn(scene, sides, FighterSide::Ally, {40.0F, 30.0F, 0.0F});
    CHECK(next.gang()->id() == firstGang);
}

TEST_CASE("a player with no gang of his own still has fighters of each side, allies fighting the enemies",
          "[ai][fighters]") {
    // Expected: unverified, #924 (a player with no gang is Coney's case)
    AiScene scene;
    FighterSides sides(scene.brains.gangs());
    Brain& ally = spawn(scene, sides, FighterSide::Ally, {40.0F, 36.0F, 0.0F});
    Brain& enemy = spawn(scene, sides, FighterSide::Enemy, {40.0F, 50.0F, 0.0F}, 180.0F);
    REQUIRE(ally.gang() != nullptr);
    REQUIRE(enemy.gang() != nullptr);
    CHECK(ally.gang()->kind() == coney::ai::kWarriorsKind);
    CHECK(Gangs::enemies(ally.gang(), enemy.gang()));
    CHECK(Gangs::enemies(enemy.gang(), ally.gang()));
}

TEST_CASE("a script's gang that took a side's name makes the side's gang a numbered one", "[ai][fighters]") {
    // Expected: unverified, #924 (the numbering is Coney's own)
    AiScene scene;
    playerInWarriors(scene);
    static_cast<void>(scene.brains.gangs().create(21, "Fighters"));
    FighterSides sides(scene.brains.gangs());
    Brain& enemy = spawn(scene, sides, FighterSide::Enemy, {40.0F, 50.0F, 0.0F}, 180.0F);
    REQUIRE(enemy.gang() != nullptr);
    CHECK(enemy.gang()->kind() == coney::ai::kFighterGangKind);
    CHECK(enemy.gang()->name() == "Fighters2");
}

TEST_CASE("a fighter made when every gang slot is in use joins no gang, and says so by the result", "[ai][fighters]") {
    // Expected: unverified, #924 (Coney's own debug tool)
    AiScene scene;
    playerInWarriors(scene);
    for (int i = 1; scene.brains.gangs().create(21, "Script" + std::to_string(i)) >= 0; ++i) {
    }
    FighterSides sides(scene.brains.gangs());
    Brain& fighter = scene.add({40.0F, 50.0F, 0.0F}, 180.0F);
    CHECK(sides.join(FighterSide::Enemy, fighter, scene.player()) == -1);
    CHECK(fighter.gang() == nullptr);
    // Room freed by a script's GangDelete is found again at the next fighter.
    scene.brains.gangs().remove(5);
    Brain& next = scene.add({41.0F, 50.0F, 0.0F}, 180.0F);
    CHECK(sides.join(FighterSide::Enemy, next, scene.player()) >= 0);
    CHECK(next.gang() != nullptr);
}
