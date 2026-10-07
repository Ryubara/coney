// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/sectors.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/route_planner.h"
#include "human/human.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"
#include "support/path_fixtures.h"

// The neighbour sectors (docs/research/ai.md#neighbour-sectors): the numbering, the rebuild's counts, nearest and
// flags, the free-player flag, the lazy wall probe, the readers and the maximum age. Synthetic humans on a floor.

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::sectorOf;
using coney::ai::Sectors;
using coney::anim::Vec3;
using coney::human::Human;
namespace flag = coney::ai::sector_flag;

namespace {

// Humans on an 80 m floor, each with a brain; a planner over one 4 m square of path round (40, 40) when asked.
struct Scene {
    coney::test::FightCharacter character;
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    coney::world::PathMap map;
    coney::ai::RoutePlanner planner{map};
    std::vector<std::unique_ptr<Human>> humans;
    coney::ai::Brains brains;

    explicit Scene(bool withPaths = false) : map(paths()) {
        if (withPaths) {
            brains.setPlanner(&planner);
        }
    }

    // A path rectangle [38, 42] x [39, 42]: from (40, 40) its edge lies 1 m behind (-y) and 2 m ahead (+y).
    static coney::world::PathMap paths() {
        coney::test::PathBuilder builder;
        builder.rectangle(38.0F, 42.0F, 39.0F, 42.0F);
        return builder.build();
    }

    // A human with its feet at (x, y) facing `headingDegrees` (0 is +y, 90 is -x), with a brain of `type`.
    Brain& add(float x, float y, float headingDegrees, BrainType type = BrainType::Gang) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        made.setFighterProfile(coney::human::FighterProfile{
            .player = type == BrainType::Player, .powerClass = coney::ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), Vec3{x, y, 0.0F}, headingDegrees);
        return brains.add(made, type, coney::ai::FightSettings{}, static_cast<std::uint32_t>(humans.size()));
    }
};

} // namespace

TEST_CASE("sector 0 is straight ahead and the index rises anticlockwise, 45 degrees a sector", "[ai][sectors]") {
    Scene scene;
    const Human& owner = scene.add(40.0F, 40.0F, 0.0F).human(); // facing +y
    CHECK(sectorOf(owner, {40.0F, 41.0F, 0.0F}) == 0);
    CHECK(sectorOf(owner, {39.0F, 41.0F, 0.0F}) == 1); // ahead-left
    CHECK(sectorOf(owner, {39.0F, 40.0F, 0.0F}) == 2); // left
    CHECK(sectorOf(owner, {40.0F, 39.0F, 0.0F}) == 4); // behind
    CHECK(sectorOf(owner, {41.0F, 40.0F, 0.0F}) == 6); // right
    CHECK(sectorOf(owner, {41.0F, 41.0F, 0.0F}) == 7);
    // 20 degrees off either way stays in sector 0; 25 degrees is past the edge.
    const auto at = [](float degrees) {
        const float r = degrees * std::numbers::pi_v<float> / 180.0F;
        return Vec3{40.0F - std::sin(r), 40.0F + std::cos(r), 0.0F};
    };
    CHECK(sectorOf(owner, at(20.0F)) == 0);
    CHECK(sectorOf(owner, at(-20.0F)) == 0);
    CHECK(sectorOf(owner, at(25.0F)) == 1);
    CHECK(sectorOf(owner, at(-25.0F)) == 7);
    // A point in sector k is k x 45 degrees round from the current heading.
    const Vec3 left = coney::ai::sectorPoint(owner, 2, 1.0F);
    CHECK(std::fabs(left.x - 39.0F) < 1e-4F);
    CHECK(std::fabs(left.y - 40.0F) < 1e-4F);
    CHECK(coney::ai::sectorTurnWay(0, 2) == 1);
    CHECK(coney::ai::sectorTurnWay(0, 6) == -1);
    CHECK(coney::ai::sectorTurnWay(7, 1) == 1);
}

TEST_CASE("a rebuild counts the humans within 1.5 m by sector, keeps the nearest and flags how close",
          "[ai][sectors]") {
    Scene scene;
    Brain& owner = scene.add(40.0F, 40.0F, 0.0F);
    Brain& ahead = scene.add(40.0F, 41.0F, 0.0F);     // 1 m ahead: d² 1 < 1.5
    Brain& ahead2 = scene.add(40.0F, 41.4F, 0.0F);    // farther in the same sector
    Brain& left = scene.add(38.7F, 40.0F, 0.0F);      // 1.3 m left: d² 1.69, below 2.5
    const Brain& far = scene.add(40.0F, 38.0F, 0.0F); // 2 m behind: beyond 1.5 m
    Sectors& sectors = owner.sectors(coney::ai::kSectorAgeMs);
    CHECK(sectors[0].nearest == &ahead);
    CHECK(sectors[0].count == 2);
    CHECK(sectors[0].flags == (flag::kOccupied | flag::kVeryClose));
    CHECK(sectors[2].nearest == &left);
    CHECK(sectors[2].count == 1);
    CHECK(sectors[2].flags == flag::kOccupied);
    CHECK(sectors[4].nearest == nullptr);
    CHECK(sectors[4].flags == 0);
    CHECK(sectors.heldBy(ahead));
    CHECK_FALSE(sectors.heldBy(ahead2));
    CHECK_FALSE(sectors.heldBy(far));
    CHECK_FALSE(sectors.allClear());
    // The readers: a very close human blocks; an occupied sector is not free; the cost is the flags + 2 x the count.
    CHECK(sectors.blocked(owner, 0));
    CHECK_FALSE(sectors.free(owner, 2));
    CHECK(sectors.free(owner, 4));
    CHECK(sectors.cost(owner, 0) == 3 + 2 * 2);
    CHECK(sectors.cost(owner, 2) == 1 + 2);
    CHECK(sectors.cost(owner, 4) == 0);
    // A human going away is forgotten.
    owner.forget(ahead);
    CHECK(owner.sectorRecord()[0].nearest == nullptr);
}

TEST_CASE("the record is rebuilt only once it is as old as the caller allows", "[ai][sectors]") {
    Scene scene;
    Brain& owner = scene.add(40.0F, 40.0F, 0.0F);
    Brain& other = scene.add(40.0F, 41.0F, 0.0F);
    CHECK(owner.sectors(coney::ai::kSectorAgeMs)[0].nearest == &other);
    other.human().place(Vec3{40.0F, 39.0F, 0.0F}, 0.0F);
    owner.update(400);
    CHECK(owner.sectors(coney::ai::kSectorAgeMs)[0].nearest == &other); // 400 ms old: kept
    owner.update(500);
    CHECK(owner.sectors(coney::ai::kSectorGiveWayAgeMs)[4].nearest == &other); // 500 ms for giving way: rebuilt
    CHECK(owner.sectorRecord().refreshedMs() == 500);
}

TEST_CASE("an AI's record flags a free player within 5.5 m, but not one in its fight", "[ai][sectors]") {
    Scene scene;
    Brain& owner = scene.add(40.0F, 40.0F, 0.0F);
    Brain& player = scene.add(36.0F, 40.0F, 0.0F, BrainType::Player); // 4 m to the left
    Sectors& sectors = owner.sectors(coney::ai::kSectorAgeMs);
    CHECK(sectors[2].flags == flag::kFreePlayer);
    CHECK(sectors[2].nearest == nullptr);
    CHECK(sectors.blocked(owner, 2));
    CHECK_FALSE(sectors.free(owner, 2));
    CHECK(sectors.allClear());
    // Targeting the AI, he is part of its fight.
    player.setTarget(&owner);
    owner.update(1000);
    CHECK(owner.sectors(coney::ai::kSectorAgeMs)[2].flags == 0);
    // A player's own record never has flag 8.
    CHECK(player.sectors(coney::ai::kSectorAgeMs)[6].flags == 0);
}

TEST_CASE("the wall probe runs lazily: a sector without a walkable line 1.5 m out is a wall", "[ai][sectors]") {
    Scene scene(true);
    Brain& owner = scene.add(40.0F, 40.0F, 0.0F); // facing +y; the path ends 1 m behind, 2 m ahead
    Sectors& sectors = owner.sectors(coney::ai::kSectorAgeMs);
    CHECK(sectors[4].probePending);
    CHECK(sectors.wall(owner, 4));
    CHECK_FALSE(sectors[4].probePending);
    CHECK_FALSE(sectors.wall(owner, 0));
    CHECK_FALSE(sectors.free(owner, 4));
    CHECK(sectors.blocked(owner, 4));
    CHECK(sectors.cost(owner, 4) == flag::kWall);
    CHECK(sectors.free(owner, 0));
}
