// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/enemy_scan.h"

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/gangs.h"
#include "raycast/collision_mesh.h"
#include "support/ai_fixtures.h"
#include "support/collision_fixtures.h"

// The AI's enemy scan (docs/research/ai.md#enemy-scan): what a scanner lists, what it keeps and drops, and when it
// scans. Synthetic floors; the player (slot 0) in one gang, the guards in an enemy gang.

using coney::ai::Brain;
using coney::ai::scanEnemies;
using coney::ai::scanSees;
using coney::anim::Vec3;

namespace {

// Puts the player in gang "Players" and `guards` in gang "Guards", enemies of each other.
void makeSides(coney::test::AiScene& scene, const std::vector<Brain*>& guards) {
    auto& gangs = scene.brains.gangs();
    const int players = gangs.create(0, "Players");
    const int enemies = gangs.create(2, "Guards");
    gangs.addMember(players, scene.player());
    for (Brain* guard : guards) {
        gangs.addMember(enemies, *guard);
    }
    gangs.makeEnemies(players, enemies);
}

// Whether `brain` lists `other`.
bool lists(const Brain& brain, const Brain& other) {
    return std::ranges::find(brain.enemies(), &other) != brain.enemies().end();
}

} // namespace

TEST_CASE("a guard lists an enemy in his cone and sight, and only behind him within the near radius", "[ai][scan]") {
    coney::test::AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    // The guard at (40, 50) faces -y, toward the player at (40, 40).
    Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
    makeSides(scene, {&guard});
    scene.run(1);
    CHECK(scanSees(guard, scene.player()));
    CHECK(scanEnemies(scene.brains.gangs(), guard) == 1);
    CHECK(lists(guard, scene.player()));
    CHECK(scanEnemies(scene.brains.gangs(), guard) == 0); // listed already

    // Turned away he no longer sees him 10 m off, but does within 1.5 m of a standing player.
    coney::test::AiScene behind;
    behind.brains.setCollision(behind.mesh.get());
    Brain& far = behind.add(Vec3{40.0F, 50.0F, 0.0F}, 0.0F);
    Brain& near = behind.add(Vec3{40.0F, 41.2F, 0.0F}, 0.0F);
    makeSides(behind, {&far, &near});
    behind.run(1);
    CHECK_FALSE(scanSees(far, behind.player()));
    CHECK(scanSees(near, behind.player()));
}

TEST_CASE("a wall hides an enemy, and a friend or an enemy beyond the sight range is never listed", "[ai][scan]") {
    coney::test::AiScene scene;
    scene.mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingPlusY(45.0F, 30.0F, 50.0F, 0.0F, 4.0F)));
    scene.brains.setCollision(scene.mesh.get());
    Brain& walled = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
    Brain& distant = scene.add(Vec3{70.0F, 40.0F, 0.0F}, 90.0F);
    distant.setSight(25.0F, distant.fieldOfView());
    makeSides(scene, {&walled, &distant});
    Brain& friendly = scene.add(Vec3{45.0F, 40.0F, 0.0F}, 90.0F);
    scene.run(1);
    CHECK_FALSE(scanSees(walled, scene.player()));
    CHECK_FALSE(scanSees(distant, scene.player()));
    CHECK(scanEnemies(scene.brains.gangs(), friendly) == 0);
}

TEST_CASE("an enemy out of sight is dropped beyond 3 m and kept within it", "[ai][scan]") {
    coney::test::AiScene scene;
    scene.mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingPlusY(45.0F, 30.0F, 50.0F, 0.0F, 4.0F)));
    scene.brains.setCollision(scene.mesh.get());
    Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
    makeSides(scene, {&guard});
    scene.run(1);
    // Listed by hand, then behind the wall 10 m off: the scan drops him.
    guard.addEnemy(scene.player());
    scanEnemies(scene.brains.gangs(), guard);
    CHECK_FALSE(lists(guard, scene.player()));
    // Within 3 m (behind the guard, unseen) he stays.
    scene.player().human().place(Vec3{40.0F, 52.5F, 0.0F}, 0.0F);
    scene.run(1);
    guard.addEnemy(scene.player());
    scanEnemies(scene.brains.gangs(), guard);
    CHECK(lists(guard, scene.player()));
}

TEST_CASE("the schedule scans each brain once an interval, at most five an update", "[ai][scan]") {
    coney::test::AiScene scene;
    std::vector<Brain*> guards;
    for (int k = 0; k < 7; ++k) {
        guards.push_back(&scene.add(Vec3{30.0F + static_cast<float>(k), 60.0F, 0.0F}, 180.0F));
    }
    makeSides(scene, guards);
    coney::ai::ScanSchedule schedule;
    schedule.setInterval(*guards[0], 500);
    CHECK(schedule.interval(*guards[0]) == 500);
    CHECK(schedule.interval(*guards[1]) == coney::ai::kScanIntervalMs);
    schedule.beginUpdate();
    int scanned = 0;
    for (Brain* guard : guards) {
        scanned += schedule.maybeScan(scene.brains.gangs(), *guard, 0) ? 1 : 0;
    }
    CHECK(scanned == coney::ai::kScanTokens);
    // The next update the two refused scan; the first not before its 500 ms.
    schedule.beginUpdate();
    CHECK(schedule.maybeScan(scene.brains.gangs(), *guards[5], 33));
    CHECK_FALSE(schedule.maybeScan(scene.brains.gangs(), *guards[0], 33));
    CHECK(schedule.maybeScan(scene.brains.gangs(), *guards[0], 500));
}
