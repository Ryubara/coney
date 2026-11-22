// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/enemy_scan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/gangs.h"
#include "ai/sectors.h"
#include "ai/steering.h"
#include "core/crowd_limits.h"
#include "core/engine_limits.h"
#include "raycast/collision_mesh.h"
#include "support/ai_fixtures.h"
#include "support/collision_fixtures.h"

// The AI's enemy scan (docs/research/ai-fights.md#enemy-scan): what a scanner lists, what it keeps and drops, and when
// it scans. Synthetic floors; the player (slot 0) in one gang, the guards in an enemy gang.

using coney::ai::Brain;
using coney::ai::kScanMaxEnemies;
using coney::ai::nearestSeen;
using coney::ai::scanEnemies;
using coney::ai::scanSees;
using coney::ai::SightCandidate;
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

namespace {

// The player's gang and `guards` guards in enemy gangs of ten, the guards in rows north of the player; returns the
// scene's members in table order (the player first).
std::vector<Brain*> crowdOfGuards(coney::test::AiScene& scene, int guards) {
    auto& gangs = scene.brains.gangs();
    const int players = gangs.create(0, "Players");
    gangs.addMember(players, scene.player());
    std::vector<Brain*> members{&scene.player()};
    int gang = -1;
    for (int k = 0; k < guards; ++k) {
        if (k % 10 == 0) {
            gang = gangs.create(2, "Guards " + std::to_string(k / 10));
            gangs.makeEnemies(players, gang);
        }
        const int column = k % 40;
        const int row = k / 40;
        members.push_back(
            &scene.add(Vec3{20.0F + static_cast<float>(column), 50.0F + static_cast<float>(row), 0.0F}, 180.0F));
        gangs.addMember(gang, *members.back());
    }
    return members;
}

// One gangs' update at `nowMs`, then every member in table order asks the schedule; returns how many scanned.
int scansOfOneUpdate(coney::test::AiScene& scene, const std::vector<Brain*>& members, std::uint64_t nowMs) {
    auto& gangs = scene.brains.gangs();
    gangs.update(nowMs);
    int scanned = 0;
    for (Brain* member : members) {
        scanned += gangs.scans().maybeScan(gangs, *member, nowMs) ? 1 : 0;
    }
    return scanned;
}

} // namespace

TEST_CASE("with the original's 60 brains in gangs an update scans five", "[ai][scan][crowd]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code): AI_TakeScanToken 0x00293cd0, 5 an update
    coney::test::AiScene scene;
    const std::vector<Brain*> members = crowdOfGuards(scene, 59);
    REQUIRE(members.size() == 60);
    CHECK(scansOfOneUpdate(scene, members, 0) == coney::crowd::kOrigScanTokens);
    CHECK(scansOfOneUpdate(scene, members, 33) == coney::crowd::kOrigScanTokens);
}

TEST_CASE("past 60 brains in gangs an update scans five per 60, at most twenty, and the refused scan next",
          "[ai][scan][crowd]") {
    // Expected: unverified, #1569 (Coney's own rule past the original's 60 humans: docs/engine/crowd-rules.md)
    coney::test::AiScene scene;
    const std::vector<Brain*> members = crowdOfGuards(scene, 120);
    REQUIRE(members.size() == 121);
    // 121 brains: 11 tokens an update; every brain has scanned after 11 updates, in table order.
    int total = 0;
    for (int update = 0; update < 11; ++update) {
        const int scanned = scansOfOneUpdate(scene, members, static_cast<std::uint64_t>(update) * 33U);
        CHECK(scanned == 11);
        total += scanned;
    }
    CHECK(total == 121);
    // None is due again before its 2 s.
    CHECK(scansOfOneUpdate(scene, members, 400) == 0);

    coney::test::AiScene big;
    const std::vector<Brain*> crowd = crowdOfGuards(big, 300);
    CHECK(scansOfOneUpdate(big, crowd, 0) == coney::crowd::kCrowdScanTokenCap);
}

TEST_CASE("the scan casts its lines of sight nearest first and stops once 16 are seen", "[ai][scan]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code): step 4 of Brain_ScanEnemies 0x0028b358 keeps the nearest 16
    // of all it found, so a member farther than 16 found ones leaves the list whatever his line of sight; issue 1555.
    // 24 candidates 1 m to 24 m away, listed farthest first.
    std::vector<SightCandidate> candidates;
    for (int metres = 24; metres >= 1; --metres) {
        candidates.push_back(SightCandidate{.distance = static_cast<float>(metres), .needsRay = true});
    }
    std::vector<float> cast;
    std::vector<float> blocked;
    const auto line = [&](std::size_t k) {
        cast.push_back(candidates[k].distance);
        return std::ranges::find(blocked, candidates[k].distance) == blocked.end();
    };
    // Every line clear: 16 cast, nearest first, and the 16 nearest found.
    std::vector<bool> found = nearestSeen(candidates, kScanMaxEnemies, line);
    CHECK(cast == std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        CHECK(found[k] == (candidates[k].distance <= 16.0F));
    }
    // A blocked line is not counted: with 2, 5 and 7 m blocked the rays go on to 19 m.
    cast.clear();
    blocked = {2.0F, 5.0F, 7.0F};
    found = nearestSeen(candidates, kScanMaxEnemies, line);
    CHECK(cast.size() == 19);
    CHECK(cast.back() == 19.0F);
    CHECK_FALSE(found[candidates.size() - 2]);  // 2 m
    CHECK(found[candidates.size() - 19]);       // 19 m
    CHECK_FALSE(found[candidates.size() - 20]); // 20 m: never cast
    // A member whose gang is always seen counts without a ray; one the scanner refers to is cast however far, and so
    // is one as far as the 16th found.
    cast.clear();
    blocked.clear();
    candidates[candidates.size() - 1].needsRay = false; // 1 m
    candidates[0].referred = true;                      // 24 m
    candidates.push_back(SightCandidate{.distance = 16.0F, .needsRay = true});
    found = nearestSeen(candidates, kScanMaxEnemies, line);
    CHECK(cast == std::vector<float>{2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 16, 24});
    CHECK(found[candidates.size() - 2]); // 1 m, no ray
    CHECK(found[0]);                     // 24 m, referred to
    CHECK(found.back());                 // the second at 16 m
    CHECK_FALSE(found[1]);               // 23 m
}

TEST_CASE("a brain refers to another through its target, enemy list, attack slots, sectors and detour", "[ai][scan]") {
    // Expected: ai-fights.md#in-coney, confirmed (code): the fields Brain::forget() clears, which the scan keeps
    // testing
    coney::test::AiScene scene;
    Brain& owner = scene.add(Vec3{40.0F, 60.0F, 0.0F}, 180.0F);
    Brain& other = scene.add(Vec3{40.0F, 61.0F, 0.0F}, 0.0F); // 1 m off: in a sector of owner's
    Brain& stranger = scene.add(Vec3{60.0F, 60.0F, 0.0F}, 0.0F);
    CHECK_FALSE(owner.refersTo(stranger));
    // The sectors: other is the nearest in one, until forgotten.
    static_cast<void>(owner.sectors(coney::ai::kSectorAgeMs));
    REQUIRE(owner.sectorRecord().heldBy(other));
    CHECK(owner.refersTo(other));
    owner.forget(other);
    CHECK_FALSE(owner.refersTo(other));
    // The enemy list.
    owner.addEnemy(other);
    CHECK(owner.refersTo(other));
    owner.forget(other);
    CHECK_FALSE(owner.refersTo(other));
    // The target, and the attack slot it takes on the target.
    owner.setTarget(&other);
    REQUIRE(owner.target() == &other);
    CHECK(owner.refersTo(other));
    REQUIRE(std::ranges::find(other.attackSlots(), &owner) != other.attackSlots().end());
    CHECK(other.refersTo(owner));
    owner.forget(other);
    other.forget(owner);
    CHECK_FALSE(owner.refersTo(other));
    CHECK_FALSE(other.refersTo(owner));
    // The detour.
    coney::ai::setAvoiding(owner, &other);
    CHECK(owner.refersTo(other));
    owner.forget(other);
    CHECK_FALSE(owner.refersTo(other));
}

TEST_CASE("with more than 16 enemies in sight a scan lists the nearest 16 and still drops a farther target",
          "[ai][scan]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code): more than 16 found, the 16 nearest are kept and the rest
    // forgotten (Brain_ScanEnemies 0x0028b358, step 4); a target among them is dropped (Brain::forget()).
    coney::test::AiScene scene;
    Brain& guard = scene.add(Vec3{40.0F, 70.0F, 0.0F}, 180.0F);
    // 20 enemies straight ahead, 2 m to 21 m off, listed farthest first.
    std::vector<Brain*> enemies;
    enemies.reserve(20);
    for (int k = 19; k >= 0; --k) {
        const float side = (k % 2 == 0) ? 0.3F : -0.3F;
        enemies.push_back(&scene.add(Vec3{40.0F + side, 68.0F - static_cast<float>(k), 0.0F}, 0.0F));
    }
    auto& gangs = scene.brains.gangs();
    // Two gangs of 10 (a gang's most members), both enemies of the guard's.
    const int guards = gangs.create(2, "Guards");
    const int far = gangs.create(3, "Far");
    const int near = gangs.create(3, "Near");
    gangs.addMember(guards, guard);
    for (std::size_t k = 0; k < enemies.size(); ++k) {
        gangs.addMember(k < 10 ? far : near, *enemies[k]);
    }
    scene.run(1);
    gangs.makeEnemies(guards, far);
    gangs.makeEnemies(guards, near);
    Brain& farthest = *enemies.front();
    guard.setTarget(&farthest);
    REQUIRE(guard.target() == &farthest);
    CHECK(scanEnemies(gangs, guard) == static_cast<int>(kScanMaxEnemies));
    // The nearest 16, nearest first; the farthest, his target, forgotten.
    REQUIRE(guard.enemies().size() == kScanMaxEnemies);
    for (std::size_t k = 0; k < kScanMaxEnemies; ++k) {
        CHECK(guard.enemies()[k] == enemies[enemies.size() - 1 - k]);
    }
    CHECK(guard.target() != &farthest);
    CHECK_FALSE(lists(guard, farthest));
}

TEST_CASE("under the engine limits preset past 60 brains in gangs an update scans the original's five",
          "[ai][scan][crowd][engine-limits]") {
    // Expected: ai-fights.md#enemy-scan, confirmed (code): AI_TakeScanToken 0x00293cd0, 5 an update; the preset pins
    // the lift at it (docs/guides/running-options.md#engine-limits)
    coney::test::AiScene scene;
    scene.brains.setEngineLimits(coney::EngineLimits::Original);
    const std::vector<Brain*> members = crowdOfGuards(scene, 120);
    REQUIRE(members.size() == 121);
    CHECK(scansOfOneUpdate(scene, members, 0) == coney::crowd::kOrigScanTokens);
    CHECK(scansOfOneUpdate(scene, members, 33) == coney::crowd::kOrigScanTokens);
}
