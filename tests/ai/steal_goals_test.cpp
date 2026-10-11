// SPDX-License-Identifier: GPL-3.0-or-later
// The Warriors' steal command (docs/research/ai-code.md#t2-warrior-steal, docs/research/ai.md#warrior-steal): the
// tactic's store front and clusters, the claims that keep two Warriors off one object, the WarriorVandalSteal goal's
// fetches and GetItem's walk and pick-up (docs/research/ai.md#get-item). Synthetic humans on a flat floor and a store
// of stand-in objects; the level's side (ItemServices) is a fake that takes an object at once when a pick-up starts.
#include "ai/steal_goals.h"

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/item_services.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::GoalType;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// The store's zone in these tests.
constexpr int kZone = 24;

// A level's objects as the AI sees them: store fronts and owned objects; a pick-up takes its object at once (it is
// gone, as a TYPE_SPECIAL taken for good), noting who took it and where he stood.
class FakeItems final : public coney::ai::ItemServices {
  public:
    struct Object {
        double handle = 0;
        Vec3 position{};
        int zone = kZone;
        bool gone = false;
    };
    struct Take {
        double taker = 0;
        double object = 0;
        Vec3 standing{};
    };
    std::vector<coney::ai::StoreFlag> storeFlags;
    std::vector<Object> objects;
    std::vector<Take> takes;

    [[nodiscard]] std::vector<coney::ai::StoreFlag> stores() const override { return storeFlags; }
    [[nodiscard]] std::vector<coney::ai::LooseItem> ownedItems(int zone) const override {
        std::vector<coney::ai::LooseItem> items;
        for (const Object& object : objects) {
            if (object.zone == zone && !object.gone) {
                items.push_back({object.handle, object.position});
            }
        }
        return items;
    }
    [[nodiscard]] std::optional<Vec3> itemPosition(double handle) const override {
        const Object* object = find(handle);
        return object != nullptr && !object->gone ? std::optional<Vec3>(object->position) : std::nullopt;
    }
    [[nodiscard]] std::optional<double> holder(double /*handle*/) const override { return std::nullopt; }
    bool startPickUp(Brain& brain, double handle) override {
        Object* object = find(handle);
        if (object == nullptr || object->gone) {
            return false;
        }
        object->gone = true;
        takes.push_back({brain.handle(), handle, brain.human().position()});
        return true;
    }

    // Adds an owned object with `handle` at `at`.
    void add(double handle, Vec3 at) { objects.push_back({handle, at, kZone, false}); }

  private:
    // The object with `handle`; null for none.
    [[nodiscard]] Object* find(double handle) {
        for (Object& object : objects) {
            if (object.handle == handle) {
                return &object;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const Object* find(double handle) const { return const_cast<FakeItems*>(this)->find(handle); }
};

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
