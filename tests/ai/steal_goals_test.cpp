// SPDX-License-Identifier: GPL-3.0-or-later
// The Warriors' steal command (docs/research/ai-code.md#t2-warrior-steal, docs/research/ai-gangs.md#warrior-steal): the
// tactic's store front and clusters, the claims that keep two Warriors off one object, the WarriorVandalSteal goal's
// fetches and GetItem's walk and pick-up (docs/research/ai.md#get-item), and the wreck half of WarriorVandalSteal
// (docs/research/crew-wreck.md: panes and smash targets). Synthetic humans on a flat floor and
// a store of stand-in objects; the level's side (ItemServices) is a fake that takes an object at once when a pick-up
// starts.
#include "ai/steal_goals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/attack_action.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/item_services.h"
#include "ai/move_action.h"
#include "ai/perception.h"
#include "ai/smash_goals.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"
#include "support/ai_fixtures.h"
#include "support/collision_fixtures.h"
#include "support/fake_items.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;
using coney::test::FakeItems;
using coney::test::kZone;

namespace {

// The player's crew: a gang holding the player as chief and `count` AI members a few metres east of him.
int makeCrew(AiScene& scene, int count, std::vector<Brain*>& members) {
    const int gang = scene.brains.gangs().create(1, "Warriors");
    scene.brains.gangs().addMember(gang, scene.player());
    for (int k = 0; k < count; ++k) {
        Brain& member = scene.add({42.0F, 40.0F + (2.0F * static_cast<float>(k)), 0.0F}, 0.0F);
        scene.brains.gangs().addMember(gang, member);
        members.push_back(&member);
    }
    return gang;
}

// Gives `gang` the steal tactic for the player over `items`.
coney::ai::WarriorStealTactic& steal(AiScene& scene, int gang, FakeItems& items) {
    auto tactic = std::make_unique<coney::ai::WarriorStealTactic>(scene.player().handle(), scene.services,
                                                                  scene.brains.formations(), &items);
    coney::ai::WarriorStealTactic& kept = *tactic;
    scene.brains.gangs().setTactic(gang, std::move(tactic));
    return kept;
}

// A store front 5 m east of the player (who stands at 40, 40) with four shelf objects 1.2 m up in two groups.
void stockStore(FakeItems& items) {
    items.storeFlags.push_back({.handle = 900, .position = {45.0F, 40.0F, 0.0F}, .zone = kZone});
    items.add(1, {46.0F, 44.0F, 1.2F});
    items.add(2, {46.5F, 44.0F, 1.2F});
    items.add(3, {46.0F, 36.0F, 1.2F});
    items.add(4, {46.5F, 36.0F, 1.2F});
}

} // namespace

TEST_CASE("the steal tactic takes the store front within 12 m of the chief and clusters its owned objects",
          "[ai][steal]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 2, members);
    FakeItems items;
    stockStore(items);
    coney::ai::WarriorStealTactic& tactic = steal(scene, gang, items);
    scene.run(1);

    REQUIRE(tactic.store().has_value());
    CHECK(tactic.store() == std::optional<double>(900.0));
    CHECK(tactic.zone() == kZone);
    // Two groups half a metre apart inside, eight metres from each other: two clusters of two.
    REQUIRE(tactic.clusters().clusters().size() == 2);
    CHECK(tactic.clusters().clusters()[0].objects.size() == 2);
    // Every AI member runs WarriorVandalSteal (or the fetch it pushed); the chief nothing.
    for (const Brain* member : members) {
        CHECK(const_cast<Brain*>(member)->findGoal(GoalType::WarriorVandalSteal) != nullptr);
    }
    CHECK(scene.player().goalCount() == 0);
}

TEST_CASE("with no store front within 12 m the steal tactic has no store and hands out nothing", "[ai][steal]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, members);
    FakeItems items;
    items.storeFlags.push_back({.handle = 900, .position = {60.0F, 40.0F, 0.0F}, .zone = kZone});
    items.add(1, {61.0F, 40.0F, 1.2F});
    coney::ai::WarriorStealTactic& tactic = steal(scene, gang, items);
    scene.run(60);

    CHECK_FALSE(tactic.store().has_value());
    CHECK(tactic.zone() == -1);
    CHECK(items.takes.empty());
    CHECK(members[0]->findGoal(GoalType::GetItem) == nullptr);
}

TEST_CASE("under the steal command the Warriors fetch the store's objects one by one until none is left",
          "[ai][steal]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 2, members);
    FakeItems items;
    stockStore(items);
    static_cast<void>(steal(scene, gang, items));
    scene.run(60 * 30);

    REQUIRE(items.takes.size() == 4);
    for (const FakeItems::Take& take : items.takes) {
        // Each taken by a member, standing within GetItem's reach of it in plan.
        CHECK((take.taker == members[0]->handle() || take.taker == members[1]->handle()));
        const Vec3 at = items.objects.at(static_cast<std::size_t>(take.object) - 1).position;
        CHECK(std::hypot(at.x - take.standing.x, at.y - take.standing.y) <= coney::ai::kGetItemReach);
    }
    // Both Warriors stole: the claims kept them off each other's objects.
    const bool both = (items.takes[0].taker != items.takes[1].taker) || (items.takes[1].taker != items.takes[2].taker);
    CHECK(both);
    // With nothing left they stay with the chief under WarriorVandalSteal.
    for (Brain* member : members) {
        CHECK(member->findGoal(GoalType::GetItem) == nullptr);
        CHECK(member->findGoal(GoalType::WarriorVandalSteal) != nullptr);
    }
}

TEST_CASE("an object claimed for the gang is not handed out again until its 5 s run out", "[ai][steal]") {
    AiScene scene;
    Brain& a = scene.add({40.0F, 42.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(1, {41.0F, 44.0F, 1.2F});
    items.add(2, {41.4F, 44.0F, 1.2F});
    coney::ai::StealClusters clusters;
    clusters.build(items.ownedItems(kZone));
    REQUIRE(clusters.clusters().size() == 1);

    CHECK(clusters.claim(a, items, 1, 0) == std::optional<double>(1.0));
    CHECK(clusters.claim(a, items, 1, 0) == std::optional<double>(2.0));
    CHECK_FALSE(clusters.claim(a, items, 1, 1000).has_value());
    // Released, or run out, it is handed out again.
    clusters.release(2.0);
    CHECK(clusters.claim(a, items, 1, 1000) == std::optional<double>(2.0));
    CHECK(clusters.claim(a, items, 1, coney::ai::kStealClaimMs + 1) == std::optional<double>(1.0));
    // Another gang's claim does not hold this one back (the claim is kept on the object: one gang at a time).
    CHECK(clusters.claim(a, items, 2, coney::ai::kStealClaimMs + 1) == std::optional<double>(1.0));
    CHECK(clusters.clusters()[0].users == 5);
}

TEST_CASE("an object with an enemy standing by it is not handed out", "[ai][steal]") {
    AiScene scene;
    Brain& a = scene.add({40.0F, 42.0F, 0.0F}, 0.0F);
    Brain& guard = scene.add({41.0F, 43.5F, 0.0F}, 0.0F);
    a.addEnemy(guard);
    FakeItems items;
    items.add(1, {41.0F, 44.0F, 1.2F});
    coney::ai::StealClusters clusters;
    clusters.build(items.ownedItems(kZone));

    CHECK_FALSE(clusters.claim(a, items, 1, 0).has_value());
}

TEST_CASE("GetItem walks to the object and ends when it is gone before he gets there", "[ai][steal]") {
    AiScene scene;
    Brain& a = scene.add({40.0F, 30.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(1, {40.0F, 40.0F, 1.2F});
    REQUIRE(a.pushGoal(std::make_unique<coney::ai::GetItemGoal>(1.0, 4, items)));
    const float before = std::hypot(a.human().position().x - 40.0F, a.human().position().y - 40.0F);
    scene.run(30);
    const float after = std::hypot(a.human().position().x - 40.0F, a.human().position().y - 40.0F);
    CHECK(after < before);
    REQUIRE(a.topGoal() != nullptr);
    CHECK(a.topGoal()->type() == GoalType::GetItem);
    // Someone else took it.
    items.objects[0].gone = true;
    scene.run(2);
    CHECK(a.findGoal(GoalType::GetItem) == nullptr);
    CHECK(items.takes.empty());
}

TEST_CASE("under the wreck command with no store a Warrior walks up to a pane by the chief and swings at it",
          "[ai][steal][wreck]") {
    // A glass wall 3 m north of the chief (y 43, 3 m high); the pane is its middle, 1.5 m up, facing the crew.
    AiScene scene;
    const auto floor = std::move(scene.mesh);
    std::vector<coney::test::Tri> pane = coney::test::wallFacingMinusY(43.0F, 30.0F, 50.0F, 0.0F, 3.0F);
    for (coney::test::Tri& tri : pane) {
        tri.material = coney::ai::kGlassMaterial;
    }
    scene.mesh = coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), pane));
    // The brains' sight rays (the clear ray to the pane) read the same mesh.
    scene.brains.setCollision(scene.mesh.get());
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, members);
    FakeItems items;
    items.glass.push_back(
        {.handle = 500, .centre = {42.0F, 43.0F, 1.5F}, .normal = {0.0F, -1.0F, 0.0F}, .height = 3.0F});
    static_cast<void>(steal(scene, gang, items));

    bool sent = false;
    bool swung = false;
    for (int k = 0; k < 60 * 10 && !swung; ++k) {
        scene.run(1);
        const auto* goal = dynamic_cast<const coney::ai::DestroyItemGoal*>(members[0]->findGoal(GoalType::DestroyItem));
        if (goal != nullptr && goal->object() == 500.0) {
            sent = true;
            swung = goal->swung();
        }
    }
    CHECK(sent);
    REQUIRE(swung);
    // He swung within the goal's reach of the spot under the pane.
    const Vec3 at = members[0]->human().position();
    CHECK(std::hypot(at.x - 42.0F, at.y - 43.0F) <= coney::ai::kDestroyItemReach + 0.1F);
    // The pane is claimed for the crew's gang.
    CHECK(items.claimedBy(500, gang, members[0]->nowMs()));
}

TEST_CASE("under the wreck command a carried smash target is fetched and any other is struck where it stands",
          "[ai][steal][wreck]") {
    const auto run = [](int animSet) {
        AiScene scene;
        std::vector<Brain*> members;
        const int gang = makeCrew(scene, 1, members);
        FakeItems items;
        items.targets.push_back({.handle = 600, .position = {44.0F, 36.0F, 0.0F}, .animSet = animSet});
        // The target is also an object on the floor, as a carried one is (GetItem ends at once on one it cannot find).
        items.add(600, {44.0F, 36.0F, 0.0F});
        static_cast<void>(steal(scene, gang, items));
        for (int k = 0; k < 30; ++k) {
            scene.run(1);
            if (members[0]->findGoal(GoalType::GetItem) != nullptr) {
                return GoalType::GetItem;
            }
            if (members[0]->findGoal(GoalType::DestroyItem) != nullptr) {
                return GoalType::DestroyItem;
            }
        }
        return GoalType::WarriorVandalSteal;
    };
    CHECK(run(4) == GoalType::GetItem);
    CHECK(run(5) == GoalType::GetItem);
    CHECK(run(0) == GoalType::DestroyItem);
}

TEST_CASE("GetItem never takes an object through an intact pane; with the pane gone it takes it",
          "[ai][steal][wreck]") {
    // A shelf object 1 m behind a glass wall (y 43), a lone gang member 0.4 m in front of the glass and so within
    // GetItem's reach of it; the question of issue #241 (docs/research/ai.md#get-item): the clear ray through the
    // pane makes him swing at it, never take.
    const auto takes = [](bool glass) {
        AiScene scene;
        const auto floor = std::move(scene.mesh);
        std::vector<coney::test::Tri> wall = coney::test::wallFacingMinusY(43.0F, 30.0F, 50.0F, 0.0F, 3.0F);
        for (coney::test::Tri& tri : wall) {
            tri.material = coney::ai::kGlassMaterial;
        }
        const std::vector<coney::test::Tri> ground = coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F);
        scene.mesh = coney::test::makeMesh(glass ? coney::test::join(ground, wall) : ground);
        // The brains' sight rays (GetItem's clear ray to the object) read the same mesh.
        scene.brains.setCollision(scene.mesh.get());
        Brain& thief = scene.add({42.0F, 42.6F, 0.0F}, 0.0F);
        FakeItems items;
        items.add(1, {42.0F, 44.0F, 1.0F});
        static_cast<void>(thief.pushGoal(std::make_unique<coney::ai::GetItemGoal>(1, 4, items)));
        scene.run(60 * 10);
        return items.takes.size();
    };
    CHECK(takes(true) == 0);
    CHECK(takes(false) == 1);
}

TEST_CASE("GetItem with no clear ray to the object, facing it, counts a try and walks to the standing point",
          "[ai][steal]") {
    // A solid wall between the thief and an object 1 m north of him, within GetItem's reach: GetItemGoal_Process
    // (0x002dc9f8) counts the try and falls through to the walk.
    AiScene scene;
    const auto floor = std::move(scene.mesh);
    scene.mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusY(43.5F, 30.0F, 50.0F, 0.0F, 3.0F)));
    scene.brains.setCollision(scene.mesh.get());
    Brain& thief = scene.add({42.0F, 43.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(1, {42.0F, 44.0F, 1.0F});
    REQUIRE(thief.pushGoal(std::make_unique<coney::ai::GetItemGoal>(1, 4, items)));
    const auto* goal = dynamic_cast<const coney::ai::GetItemGoal*>(thief.findGoal(GoalType::GetItem));
    REQUIRE(goal != nullptr);
    for (int k = 0; k < 30 && goal->tries() == 0; ++k) {
        scene.run(1);
    }
    REQUIRE(goal->tries() == 1);
    CHECK(thief.actionCount() > 0);
    CHECK(items.takes.empty());
}

namespace {

// A crew of the player and one AI member 12 m east of him under the steal tactic with nothing to fetch, the member
// holding an object `items` says he swings, with a pane in reach; the member plays a clip of anim `animId` holding
// `held` for 10 s.
Brain& swingingMember(AiScene& scene, FakeItems& items, std::uint32_t animId, std::uint32_t held,
                      coney::anim::AnimClip& clip) {
    const int gang = scene.brains.gangs().create(1, "Warriors");
    scene.brains.gangs().addMember(gang, scene.player());
    Brain& member = scene.add({52.0F, 40.0F, 0.0F}, 0.0F);
    scene.brains.gangs().addMember(gang, member);
    items.use = coney::ai::HeldUse::Swings;
    items.glass.push_back(
        {.handle = 500, .centre = {52.0F, 45.0F, 1.5F}, .normal = {0.0F, -1.0F, 0.0F}, .height = 3.0F});
    static_cast<void>(steal(scene, gang, items));
    member.human().script().heldObject = 700.0;
    clip.duration = 10.0F;
    member.human().playScripted(clip, animId, 1.0F, 0.0F, coney::human::HeldFlags{.held = held, .set = held});
    return member;
}

} // namespace

TEST_CASE("under the wreck command a Warrior winding up a throw of what he holds is busy: no swing, no walk back",
          "[ai][steal][wreck]") {
    // Human_IsSwingingHeld (0x002286a0): held flag 0x800000, or 0x1000010 with an object in hand during a throw clip
    // (467 among them). TryVandalise (0x002bf118) then answers busy: nothing queued, and the walk back to the leader
    // never starts.
    const auto busy = [](std::uint32_t animId, std::uint32_t held) {
        AiScene scene;
        FakeItems items;
        coney::anim::AnimClip clip;
        Brain& member = swingingMember(scene, items, animId, held, clip);
        bool queued = false;
        for (int k = 0; k < 60 * 3; ++k) {
            scene.run(1);
            queued = queued || member.actionCount() > 0;
        }
        return !queued && member.findGoal(GoalType::FollowFormation) == nullptr;
    };
    CHECK(busy(999, 0x800000));
    CHECK(busy(467, 0x1000010));
}

TEST_CASE("under the wreck command a Warrior holding a swung object with a held flag drops his target and stops",
          "[ai][steal][wreck]") {
    // TryVandalise (0x002bf118): with a throwable in hand, Human_DropTarget, then nothing while any held flag is set;
    // the goal goes on to the walk back to the leader 12 m away.
    AiScene scene;
    FakeItems items;
    coney::anim::AnimClip clip;
    Brain& member = swingingMember(scene, items, 999, 0x100, clip);
    Brain& foe = scene.add({60.0F, 40.0F, 0.0F}, 0.0F);
    member.setTarget(&foe);
    scene.run(1);
    CHECK(member.target() == nullptr);
    // No swing queued; the goal's own turn to the leader's facing may be (crew-wreck.md#process).
    CHECK(dynamic_cast<const coney::ai::AttackAction*>(member.frontAction()) == nullptr);
    bool followed = false;
    for (int k = 0; k < 60 * 3 && !followed; ++k) {
        scene.run(1);
        followed = member.findGoal(GoalType::FollowFormation) != nullptr;
    }
    CHECK(followed);
}

TEST_CASE("GetItem waits while its object is in flight and picks it up once it has landed", "[ai][steal]") {
    // GetItemGoal_Process (0x002dc9f8): the object's flag 0x4000000 set, the goal runs on and does nothing.
    AiScene scene;
    Brain& thief = scene.add({40.0F, 30.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(1, {40.0F, 30.5F, 0.0F});
    items.flying.push_back(1.0);
    REQUIRE(thief.pushGoal(std::make_unique<coney::ai::GetItemGoal>(1.0, 4, items)));
    scene.run(60);
    CHECK(items.takes.empty());
    CHECK(thief.findGoal(GoalType::GetItem) != nullptr);
    items.flying.clear();
    scene.run(60);
    CHECK(items.takes.size() == 1);
}

TEST_CASE("GetItem's reach is measured from its standing point, not from the object", "[ai][steal]") {
    // GetItemGoal_Process (0x002dc9f8): the squared distance from him to the goal's point (+0x10) under 2.25. The
    // object rolls to his feet after the goal has set its point 6 m away: he walks to the point before the pick-up.
    AiScene scene;
    Brain& thief = scene.add({40.0F, 30.0F, 0.0F}, 0.0F);
    FakeItems items;
    items.add(1, {40.0F, 36.0F, 0.0F});
    REQUIRE(thief.pushGoal(std::make_unique<coney::ai::GetItemGoal>(1.0, 4, items)));
    // The goal starts and sets its point on the first update.
    scene.run(1);
    REQUIRE(items.takes.empty());
    items.objects[0].position = {40.5F, 30.0F, 0.0F};
    scene.run(5);
    CHECK(items.takes.empty());
    scene.run(60 * 10);
    REQUIRE(items.takes.size() == 1);
    // Taken near the standing point, not where he first stood.
    CHECK(std::hypot(items.takes[0].standing.x - 40.0F, items.takes[0].standing.y - 36.0F) < coney::ai::kGetItemReach);
}

TEST_CASE("under the wreck command the claim on a struck target drops once DestroyItem is over", "[ai][steal][wreck]") {
    // TryVandalise (0x002bf118) keeps the target in the goal's +0x10; WarriorVandalStealGoal_Resume (0x002bed30)
    // clears its claim (+0xec) when the goal comes back on top, well before the search's 5 s run out.
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, members);
    FakeItems items;
    items.targets.push_back({.handle = 600, .position = {44.0F, 38.0F, 0.0F}, .animSet = 0});
    static_cast<void>(steal(scene, gang, items));
    bool sent = false;
    for (int k = 0; k < 60 * 2 && !sent; ++k) {
        scene.run(1);
        sent = members[0]->findGoal(GoalType::DestroyItem) != nullptr;
    }
    REQUIRE(sent);
    const std::uint64_t sentMs = members[0]->nowMs();
    REQUIRE(items.claimedBy(600, gang, sentMs));
    // The target is struck and gone from the list; the goal ends.
    items.targets.clear();
    bool over = false;
    for (int k = 0; k < 60 * 3 && !over; ++k) {
        scene.run(1);
        over = members[0]->findGoal(GoalType::DestroyItem) == nullptr;
    }
    REQUIRE(over);
    scene.run(2);
    CHECK(members[0]->nowMs() < sentMs + coney::ai::kSmashClaimMs);
    CHECK_FALSE(items.claimedBy(600, gang, members[0]->nowMs()));
}

namespace {

// An action that never ends: a queue that stays full.
class HoldAction final : public coney::ai::Action {
  public:
    [[nodiscard]] coney::ai::ActionStatus update(Brain& /*brain*/) override { return coney::ai::ActionStatus::Running; }
};

// A Warrior at `at` facing `headingDegrees` running WarriorVandalSteal for the human with handle `leader`, in no gang
// and under no tactic, so neither the fetch nor the wreck gives him anything.
Brain& vandal(AiScene& scene, FakeItems& items, Vec3 at, float headingDegrees, double leader) {
    Brain& member = scene.add(at, headingDegrees);
    REQUIRE(member.pushGoal(std::make_unique<coney::ai::WarriorVandalStealGoal>(
        scene.services, scene.brains.formations(), &items, leader)));
    return member;
}

// Moves `brain`'s human to `at` facing `headingDegrees`, as a scene's placement does.
void put(AiScene& scene, Brain& brain, Vec3 at, float headingDegrees = 0.0F) {
    brain.human().place(scene.mesh.get(), at, headingDegrees);
    brain.placed();
}

} // namespace

TEST_CASE("under the wreck command a Warrior whose leader is gone keeps his goal", "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    AiScene scene;
    FakeItems items;
    Brain& member = vandal(scene, items, {42.0F, 40.0F, 0.0F}, 0.0F, 99.0);
    scene.run(30);
    CHECK(member.findGoal(GoalType::WarriorVandalSteal) != nullptr);
}

TEST_CASE("under the wreck command the follow deadline runs from his first update, so later he follows at once",
          "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // The 2 s deadline is armed whenever it is 0, near the leader too, and cleared only by a wreck: once it has
    // passed, the first update he is beyond 3 m of the leader pushes FollowFormation.
    AiScene scene;
    FakeItems items;
    Brain& member = vandal(scene, items, {41.0F, 40.0F, 0.0F}, 0.0F, scene.player().handle());
    scene.run(60 * 3);
    REQUIRE(member.findGoal(GoalType::FollowFormation) == nullptr);
    put(scene, scene.player(), {40.0F, 50.0F, 0.0F});
    scene.run(2);
    CHECK(member.findGoal(GoalType::FollowFormation) != nullptr);
}

TEST_CASE("under the wreck command a Warrior away from the leader before the deadline drops his target and looks",
          "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // Far from the leader with the deadline not passed, the update goes on to the queued-action check and, his gang
    // not attacked, drops his target and sets his field of view to 1.92 rad.
    AiScene scene;
    FakeItems items;
    Brain& member = vandal(scene, items, {52.0F, 40.0F, 0.0F}, 0.0F, scene.player().handle());
    Brain& foe = scene.add({60.0F, 40.0F, 0.0F}, 0.0F);
    member.setTarget(&foe);
    member.setSight(member.sightRange(), 0.5F);
    scene.run(1);
    CHECK(member.target() == nullptr);
    CHECK(member.fieldOfView() == coney::ai::kDefaultFieldOfView);
    CHECK(member.findGoal(GoalType::FollowFormation) == nullptr);
}

TEST_CASE("under the wreck command a Warrior near the leader turns to the leader's facing however little he is off",
          "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // Every 4 s he turns to a point ahead of him along the leader's facing, with no threshold: 40 degrees off (under
    // the 60 the crew's own heading turn waits for) is turned, to within the turn's 15 degrees.
    AiScene scene;
    FakeItems items;
    Brain& leader = scene.add({40.0F, 40.0F, 0.0F}, 90.0F);
    Brain& member = vandal(scene, items, {41.0F, 40.0F, 0.0F}, 50.0F, leader.handle());
    scene.run(60 * 3);
    const float off = coney::human::wrapAngle(member.human().heading() - leader.human().heading());
    CHECK(std::fabs(off) <= coney::ai::kTurnDoneAngle);
}

TEST_CASE("under the wreck command the fetch comes before the queued-action check", "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // TryFetch runs first in the update: with an action queued that never ends he still fetches the store's objects
    // (the pushed GetItem clears the queue). The goal runs first with no store; the store is stocked once the action
    // is queued, and the tactic finds it within its 2 s.
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeCrew(scene, 1, members);
    FakeItems items;
    static_cast<void>(steal(scene, gang, items));
    scene.run(1);
    REQUIRE(members[0]->findGoal(GoalType::WarriorVandalSteal) != nullptr);
    members[0]->queueAction(std::make_unique<HoldAction>());
    stockStore(items);
    scene.run(60 * 3);
    CHECK_FALSE(items.takes.empty());
}

TEST_CASE("under the wreck command queued actions hold the wreck back unless a fidget plays", "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // With actions queued he stops, unless held flag 0x100 (a fidget) is set: then the wreck step runs and a smash
    // target by him is struck.
    const auto struck = [](std::uint32_t held) {
        AiScene scene;
        std::vector<Brain*> members;
        const int gang = makeCrew(scene, 1, members);
        Brain& member = *members[0];
        FakeItems items;
        static_cast<void>(steal(scene, gang, items));
        // The tactic's start flushes his actions: the queue and the clip come after it, then the target.
        scene.run(1);
        member.queueAction(std::make_unique<HoldAction>());
        coney::anim::AnimClip clip;
        clip.duration = 10.0F;
        member.human().playScripted(clip, 999, 1.0F, 0.0F, coney::human::HeldFlags{.held = held, .set = held});
        items.targets.push_back({.handle = 600, .position = {44.0F, 38.0F, 0.0F}, .animSet = 0});
        for (int k = 0; k < 30; ++k) {
            scene.run(1);
            if (member.findGoal(GoalType::DestroyItem) != nullptr) {
                return true;
            }
        }
        return false;
    };
    CHECK(struck(0x100));
    CHECK_FALSE(struck(0));
}

TEST_CASE("under the wreck command a failed move holds the follow back for 31 updates", "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // While the brain's move failure (+0x284) is set the goal counts updates; far from the leader with the deadline
    // passed, he follows only once the count reaches 31, which clears the failure and the count.
    AiScene scene;
    FakeItems items;
    Brain& member = vandal(scene, items, {41.0F, 40.0F, 0.0F}, 0.0F, scene.player().handle());
    scene.run(60 * 3);
    put(scene, scene.player(), {40.0F, 50.0F, 0.0F});
    member.setMoveFailure(coney::ai::MoveFailure::Stuck);
    scene.run(25);
    CHECK(member.findGoal(GoalType::FollowFormation) == nullptr);
    scene.run(15);
    CHECK(member.findGoal(GoalType::FollowFormation) != nullptr);
}

TEST_CASE("under the wreck command a Warrior follows a running leader even within 3 m", "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // The follow also starts when the leader's gait (+0x1a8) is above the jog, whatever the distance.
    AiScene scene;
    FakeItems items;
    Brain& leader = scene.add({40.0F, 40.0F, 0.0F}, 0.0F);
    Brain& member = vandal(scene, items, {40.5F, 42.0F, 0.0F}, 0.0F, leader.handle());
    scene.run(60 * 3);
    REQUIRE(member.findGoal(GoalType::FollowFormation) == nullptr);
    leader.queueAction(std::make_unique<coney::ai::MoveAction>(
        coney::ai::MoveRequest{.point = {40.0F, 75.0F, 0.0F}, .radius = 0.5F, .gait = 4}));
    bool followedNear = false;
    for (int k = 0; k < 120 && !followedNear; ++k) {
        scene.run(1);
        followedNear = member.findGoal(GoalType::FollowFormation) != nullptr &&
                       coney::anim::distance(member.human().position(), leader.human().position()) <= 3.0F;
    }
    CHECK(followedNear);
}

TEST_CASE("under the wreck command a Warrior whose gang is attacked looks all round", "[ai][steal][wreck]") {
    // Expected: crew-wreck.md#process, confirmed (code)
    // While a member of his gang has attackers his field of view is 2 pi.
    AiScene scene;
    std::vector<Brain*> members;
    static_cast<void>(makeCrew(scene, 1, members));
    Brain& member = *members[0];
    FakeItems items;
    REQUIRE(member.pushGoal(std::make_unique<coney::ai::WarriorVandalStealGoal>(
        scene.services, scene.brains.formations(), &items, scene.player().handle())));
    Brain& foe = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    foe.setTarget(&member);
    REQUIRE_FALSE(member.attackSlots().empty());
    scene.run(1);
    CHECK(member.fieldOfView() == 2.0F * std::numbers::pi_v<float>);
}
