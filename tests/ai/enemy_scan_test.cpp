// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/enemy_scan.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/gangs.h"
#include "raycast/collision_mesh.h"
#include "support/ai_fixtures.h"
#include "support/collision_fixtures.h"

// The AI's enemy scan (docs/research/ai-fights.md#enemy-scan): what a scanner lists, what it keeps and drops, and when
// it scans. Synthetic floors; the player (slot 0) in one gang, the guards in an enemy gang.

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
    // Expected: ai-fights.md#enemy-scan, confirmed (code)
    coney::test::AiScene scene;
    scene.brains.setCollision(scene.mesh.get());
    // The guard at (40, 50) faces -y, toward the player at (40, 40).
    Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
    makeSides(scene, {&guard});
    scene.run(1);
    CHECK(scanSees(guard, scene.player()));
    // His own update's scan has listed him already; forgotten, a scan lists him again.
    CHECK(lists(guard, scene.player()));
    guard.forget(scene.player());
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
    // Expected: ai-fights.md#enemy-scan, confirmed (code)
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
    // Expected: ai-fights.md#enemy-scan, confirmed (code)
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
    // Expected: ai-fights.md#enemy-scan, confirmed (code)
    coney::test::AiScene scene;
    std::vector<Brain*> guards;
    guards.reserve(7);
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

TEST_CASE("a gang always seen is listed anywhere in sight range, behind the scanner and behind a wall", "[ai][scan]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code). Brain_ScanEnemies 0x0028b358 reads the member's gang's
    // always-seen byte (+0xdc, GangSetAlwaysSeen 0x0016bde8): set, it skips Human_MaySpectate and the whole sight step
    // (near radius, view cone, line of sight); the shadow rule and the filter still apply.
    coney::test::AiScene scene;
    scene.mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingPlusY(45.0F, 30.0F, 50.0F, 0.0F, 4.0F)));
    scene.brains.setCollision(scene.mesh.get());
    // 10 m off, facing away, a wall between them.
    Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 0.0F);
    makeSides(scene, {&guard});
    scene.run(1);
    REQUIRE_FALSE(scanSees(guard, scene.player()));
    auto& gangs = scene.brains.gangs();
    REQUIRE(scene.player().gang() != nullptr);
    gangs.setAlwaysSeen(scene.player().gang()->id(), true);
    CHECK(scanSees(guard, scene.player()));
    guard.forget(scene.player());
    CHECK(scanEnemies(gangs, guard) == 1);
    CHECK(lists(guard, scene.player()));
}

TEST_CASE("a wounded scanner's list goes, an arrested one's stays", "[ai][scan]") {
    // Expected: ai-fights.md#enemy-scan and characters.md (Human_IsWounded 0x00227d98: state 0x10000), confirmed
    // (code). Brain_ScanEnemies 0x0028b358 empties the list of a scanner Human_IsDownOrDead 0x00227e60 calls down:
    // states 0x180050000, dead, dying, knocked out or wounded (0x10000); arrested is none of them.
    for (const bool wounded : {true, false}) {
        INFO((wounded ? "wounded" : "arrested"));
        coney::test::AiScene scene;
        scene.brains.setCollision(scene.mesh.get());
        Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
        makeSides(scene, {&guard});
        scene.run(1);
        REQUIRE(lists(guard, scene.player()));
        if (wounded) {
            guard.human().setWounded(true);
        } else {
            guard.human().setArrested(true);
        }
        scanEnemies(scene.brains.gangs(), guard);
        CHECK(lists(guard, scene.player()) != wounded);
    }
}

TEST_CASE("a scanner whose gang is fighting lists an enemy more than 1.9 m above him, unless his class role is 10",
          "[ai][scan]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code). Human_MaySpectate 0x002225d8 passes a scanner that may not
    // start a fight (Brain_MayStartFight 0x002904e0 is 0: here his gang's alert state +0x32 is 1, fighting, as
    // Gang_SetAlertState 0x00164a28 sets it) unless his class role (+0x11b) is 10; else the 1.9 m height limit
    // applies. A role Coney does not know is not counted as a pass: the limit applies (a Coney stand-in, #1011).
    struct Case {
        int alert;
        std::optional<int> role;
        bool listed;
    };
    for (const Case c :
         {Case{coney::ai::kGangCalm, 3, false}, Case{coney::ai::kGangFighting, 3, true},
          Case{coney::ai::kGangFighting, 10, false}, Case{coney::ai::kGangFighting, std::nullopt, false}}) {
        INFO("alert " << c.alert << ", role " << c.role.value_or(-1));
        coney::test::AiScene scene;
        scene.brains.setCollision(scene.mesh.get());
        Brain& guard = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
        makeSides(scene, {&guard});
        auto& gangs = scene.brains.gangs();
        coney::ai::GangLookups lookups;
        lookups.role = [&guard, role = c.role](const Brain& brain) {
            return &brain == &guard ? role : std::optional<int>{};
        };
        gangs.setLookups(std::move(lookups));
        scene.run(1);
        REQUIRE(guard.gang() != nullptr);
        guard.gang()->setAlertState(c.alert);
        // Up on something 2.5 m high, 10 m ahead of the guard and in his sight.
        scene.player().human().place(Vec3{40.0F, 40.0F, 2.5F}, 0.0F);
        CHECK(scanSees(guard, scene.player()) == c.listed);
    }
}
