// SPDX-License-Identifier: GPL-3.0-or-later
// The breakable street props: the hit counters, a strike's order and message 6, a dyn_masks prop's break and removal,
// the body flags and the body test (docs/research/objects.md#breakable-props); and the weapon piles' takes
// (docs/research/objects.md#weapon-piles). Synthetic types only.

#include "world_objects/props.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/object_fixtures.h"
#include "support/path_fixtures.h"
#include "world_objects/pickups.h"

namespace {

using coney::anim::Quat;
using coney::anim::Vec3;
using coney::test::RecordingServices;
using coney::world_objects::HitKind;
using coney::world_objects::ObjectHit;
using coney::world_objects::ObjectType;
using coney::world_objects::ObjectWorld;
using coney::world_objects::PropPose;
using coney::world_objects::Props;
using coney::world_objects::PropStrike;

// A dyn_masks type with `hitpoints` (+0x58) and `hits` (+0x5a) of material 47.
ObjectType masksType(int hitpoints, int hits, std::uint32_t modelHash = 0x1234U) {
    ObjectType type;
    type.name = "test_prop";
    type.className = std::string(coney::world_objects::kDynMasksClass);
    type.hitpoints = hitpoints;
    type.value = hits;
    type.material = 47;
    type.modelHash = modelHash;
    return type;
}

// A plain hit of `kind` from human 7.
ObjectHit hitOf(HitKind kind) {
    return ObjectHit{.attacker = 7.0, .kind = kind, .point = {}, .direction = {1, 0, 0}, .attackerAt = {}};
}

// One world with its recording services.
struct Fixture {
    RecordingServices services;
    ObjectWorld world{
        .collision = nullptr, .paths = nullptr, .services = &services, .random = nullptr, .knock = nullptr};
    Props props;

    // A strike of `kind` on object 50 of `type`.
    PropStrike strike(const ObjectType& type, HitKind kind = HitKind::Plain) {
        return props.strike(50.0, type, hitOf(kind), PropPose{.position = Vec3{1, 2, 0}, .rotation = Quat{}}, world);
    }
};

} // namespace

TEST_CASE("a hits-only prop breaks on the first blow and tells the boxes once", "[world_objects][props]") {
    Fixture f;
    const ObjectType stand = masksType(0, 1);
    const PropStrike first = f.strike(stand);
    CHECK(first.intactBefore);
    CHECK(first.broke);
    CHECK(f.props.broken(50.0));
    REQUIRE(f.services.damage.size() == 1);
    CHECK(f.services.damage[0] == std::pair{7.0, 50.0});
    // The impact sound (material x 9), then the break's (the material against itself), then the body goes.
    REQUIRE(f.services.pairs.size() == 2);
    CHECK(f.services.pairs[0] == std::pair<std::uint8_t, std::uint8_t>{47, 9});
    CHECK(f.services.pairs[1] == std::pair<std::uint8_t, std::uint8_t>{47, 47});
    REQUIRE(f.services.bodies.size() == 1);
    CHECK(f.services.bodies[0] == std::pair{50.0, false});
    // A broken prop takes nothing more.
    const PropStrike again = f.strike(stand);
    CHECK_FALSE(again.intactBefore);
    CHECK(f.services.damage.size() == 1);
}

TEST_CASE("the bench takes three bare blows or one charge, each blow while intact damage done",
          "[world_objects][props]") {
    const ObjectType bench = masksType(10, 2, 0x7852cedbU);
    SECTION("bare-handed: 10, 6, 2, broken") {
        Fixture f;
        CHECK_FALSE(f.strike(bench).broke);
        CHECK(f.props.counter(50.0) == std::uint8_t{6});
        // A surviving hit sounds the material against 5.
        CHECK(f.services.pairs[1] == std::pair<std::uint8_t, std::uint8_t>{47, 5});
        CHECK_FALSE(f.strike(bench).broke);
        CHECK(f.props.counter(50.0) == std::uint8_t{2});
        CHECK(f.strike(bench).broke);
        CHECK(f.services.damage.size() == 3);
        CHECK(f.services.splinterCount == Props::kBenchSplinters);
    }
    SECTION("a charge (16) breaks it at once, sounding against 0x1a") {
        Fixture f;
        CHECK(f.strike(bench, HitKind::Charge).broke);
        CHECK(f.services.pairs[0] == std::pair<std::uint8_t, std::uint8_t>{47, 0x1a});
        CHECK(f.services.damage.size() == 1);
    }
}

// An overhead_weapon type of model `modelHash`, material 31.
ObjectType overheadType(std::uint32_t modelHash) {
    ObjectType type;
    type.name = "test_trash";
    type.className = std::string(coney::world_objects::kOverheadWeaponClass);
    type.hitpoints = 150;
    type.value = 1;
    type.material = 31;
    type.modelHash = modelHash;
    return type;
}

TEST_CASE("any strike breaks a trash prop at once: its pieces, dust and sound, gone the next tick",
          "[world_objects][props]") {
    // docs/research/objects.md#trash-props: OverheadWeapon_Break, whatever the hit, by model.
    SECTION("the trash can: the dented can and four litter pieces") {
        Fixture f;
        const PropStrike struck = f.strike(overheadType(0xfbf3e3aeU));
        CHECK(struck.intactBefore);
        CHECK(struck.broke);
        CHECK(f.props.broken(50.0));
        CHECK(f.services.damage.size() == 1);
        CHECK(f.services.spawned == std::vector<std::string>{"dyn_trashcan_b", "dyn_trashbit_a", "dyn_trashbit_b",
                                                             "dyn_trashbit_d", "dyn_trashbit_d"});
        CHECK(f.services.dusts == 2);
        CHECK(f.services.splinterCount == 10);
        CHECK(f.services.bursts == 1);
        // The impact sound, then the break's material against itself; the body goes.
        REQUIRE(f.services.pairs.size() == 2);
        CHECK(f.services.pairs[1] == std::pair<std::uint8_t, std::uint8_t>{31, 31});
        CHECK(f.services.bodies == std::vector<std::pair<double, bool>>{{50.0, false}});
        // A later strike does nothing; it goes on the next tick.
        CHECK_FALSE(f.strike(overheadType(0xfbf3e3aeU)).broke);
        f.props.tick(f.world);
        CHECK(f.props.takeRemoved() == std::vector<double>{50.0});
    }
    SECTION("the bags: the litter alone") {
        Fixture f;
        CHECK(f.strike(overheadType(0x62502b03U)).broke);
        CHECK(f.services.spawned.size() == 4);
        CHECK(f.services.spawned.front() == "dyn_trashbit_a");
    }
    SECTION("the park bin: its piece and the litter") {
        Fixture f;
        CHECK(f.strike(overheadType(0xb0c69542U)).broke);
        REQUIRE(f.services.spawned.size() == 5);
        CHECK(f.services.spawned.front() == "dyn_parktrash_aa");
    }
    SECTION("the paper stack: no pieces, its eight debris; another model: 44 splinters") {
        Fixture f;
        CHECK(f.strike(overheadType(0xfbd21393U)).broke);
        CHECK(f.services.spawned.empty());
        CHECK(f.services.splinterCount == 8);
        Fixture g;
        CHECK(g.strike(overheadType(0x1234U), HitKind::Charge).broke);
        CHECK(g.services.splinterCount == 44);
    }
}

TEST_CASE("a trash break drops a bottle 0.5 m above the path polygon under the hit, knocked with a spin",
          "[world_objects][props]") {
    // docs/research/objects.md#trash-props: dyn_beerbottle, no velocity, spin (0, a, b) with a and b in +-pi.
    coney::world::PathMap paths = coney::test::uCorridors();
    Fixture f;
    f.world.paths = &paths;
    std::vector<std::pair<Vec3, Vec3>> knocks;
    std::vector<double> knocked;
    f.world.knock = [&](double object, Vec3 velocity, Vec3 spin) {
        knocked.push_back(object);
        knocks.emplace_back(velocity, spin);
    };
    ObjectHit hit = hitOf(HitKind::Plain);
    hit.point = Vec3{1.0F, 1.0F, 0.7F};
    CHECK(f.props.strike(50.0, overheadType(0x62502b03U), hit, PropPose{}, f.world).broke);
    REQUIRE(f.services.spawned.size() == 5);
    CHECK(f.services.spawned[0] == "dyn_beerbottle");
    CHECK(f.services.spawnedAt[0].x == 1.0F);
    CHECK(f.services.spawnedAt[0].z == 0.5F);
    REQUIRE(knocked == std::vector<double>{1001.0});
    CHECK(knocks[0].first.x == 0.0F);
    CHECK(knocks[0].first.z == 0.0F);
    CHECK(knocks[0].second.x == 0.0F);
    CHECK(std::abs(knocks[0].second.y) <= std::numbers::pi_v<float>);
    // No polygon under the hit (the U's hollow): no bottle.
    Fixture g;
    g.world.paths = &paths;
    hit.point = Vec3{4.0F, 5.0F, 0.7F};
    CHECK(g.props.strike(50.0, overheadType(0x62502b03U), hit, PropPose{}, g.world).broke);
    CHECK(g.services.spawned.size() == 4);
}

TEST_CASE("a trash prop broken by running into it sounds its break at 0.65", "[world_objects][props]") {
    // docs/research/objects.md#trash-props: volume 0.65 when the attacker's +0x368 is set.
    Fixture f;
    ObjectHit hit = hitOf(HitKind::Plain);
    hit.runIn = true;
    CHECK(f.props.strike(50.0, overheadType(0x62502b03U), hit, PropPose{}, f.world).broke);
    REQUIRE(f.services.volumes.size() == 2);
    CHECK(f.services.volumes[0] == 1.0F);
    CHECK(f.services.volumes[1] == 0.65F);
    Fixture g;
    CHECK(g.strike(overheadType(0x62502b03U)).broke);
    CHECK(g.services.volumes.back() == 1.0F);
}

TEST_CASE("a cash register makes its drawer; broken it stays, its drawer jumps out and spills $25-50",
          "[world_objects][props]") {
    // docs/research/script-types.md#dyn-cashreg: hit points from +0x5a (16), material CASHREG (103); the drawer at
    // (0, 0.02, -0.22) in the register's frame, 0.35 m out at its first update after the break, the money at its
    // second.
    ObjectType till;
    till.name = "dyn_cashreg";
    till.className = std::string(coney::world_objects::kCashRegisterClass);
    till.hitpoints = 50;
    till.value = 16;
    till.material = 103;
    const PropPose pose{.position = Vec3{1, 2, 0}, .rotation = Quat{}};
    SECTION("seven bare blows leave it standing, each with its sound and dust; the eighth breaks it") {
        Fixture f;
        f.props.initCashRegister(50.0, till, pose, f.world);
        f.props.initCashRegister(50.0, till, pose, f.world);
        REQUIRE(f.services.spawned == std::vector<std::string>{"dyn_cashreg_b"});
        CHECK(f.services.spawnedAt[0].y == 2.02F);
        CHECK(f.services.spawnedAt[0].z == -0.22F);
        CHECK(f.props.drawerOf(50.0) == 1001.0);
        // Its drawer updates every 60 ticks; shut, it does nothing.
        for (int tick = 0; tick < 30; ++tick) {
            f.props.tick(f.world);
        }
        for (int blow = 0; blow < 7; ++blow) {
            CHECK_FALSE(f.strike(till).broke);
        }
        CHECK_FALSE(f.props.broken(50.0));
        CHECK(f.services.bursts == 7);
        // Each blow: the impact sound, then the register against concrete.
        REQUIRE(f.services.pairs.size() == 14);
        CHECK(f.services.pairs[1] == std::pair<std::uint8_t, std::uint8_t>{103, 5});
        const PropStrike last = f.strike(till);
        CHECK(last.broke);
        CHECK(f.props.broken(50.0));
        CHECK(f.services.pairs.back() == std::pair<std::uint8_t, std::uint8_t>{103, 103});
        // Its broken model; it keeps its body; a later blow does nothing.
        CHECK(f.services.models ==
              std::vector<std::pair<double, std::uint32_t>>{{50.0, coney::world_objects::kCashRegisterBrokenModel}});
        CHECK(f.services.bodies.empty());
        CHECK_FALSE(f.props.bodyLost(50.0));
        CHECK_FALSE(f.strike(till).broke);
        // The drawer's next update (30 ticks on) moves it 0.35 m out; the one after spills the money.
        for (int tick = 1; tick < 30; ++tick) {
            f.props.tick(f.world);
        }
        CHECK(f.services.moves.empty());
        f.props.tick(f.world);
        REQUIRE(f.services.moves.size() == 1);
        CHECK(f.services.moves[0].first == 1001.0);
        CHECK(f.services.moves[0].second.y == 2.02F + Props::kDrawerOut);
        for (int tick = 1; tick < Props::kDrawerTicks; ++tick) {
            f.props.tick(f.world);
        }
        CHECK(f.services.spawned.size() == 1);
        f.props.tick(f.world);
        CHECK(f.services.spawned == std::vector<std::string>{"dyn_cashreg_b", "dyn_money"});
        CHECK(f.services.spawnedAt[1].z == -0.22F + Props::kMoneyRise);
        REQUIRE(f.services.values.size() == 1);
        CHECK(f.services.values[0].first == 1002.0);
        CHECK(f.services.values[0].second == static_cast<std::uint32_t>(Props::kMoneyLeast));
        for (int tick = 0; tick < 300; ++tick) {
            f.props.tick(f.world);
        }
        CHECK(f.services.spawned.size() == 2);
        CHECK(f.props.takeRemoved().empty());
    }
    SECTION("one charge (18) breaks it") {
        Fixture f;
        CHECK(f.strike(till, HitKind::Charge).broke);
    }
    SECTION("triangle's message 0 deletes its drawer and lifts it once; a broken one refuses") {
        Fixture f;
        f.props.initCashRegister(50.0, till, pose, f.world);
        CHECK(f.props.useCashRegister(50.0, till, f.world));
        CHECK(f.services.destroyed == std::vector<double>{1001.0});
        CHECK(f.props.drawerOf(50.0) == coney::world_objects::kNoObject);
        CHECK_FALSE(f.props.useCashRegister(50.0, till, f.world));
        Fixture g;
        g.props.initCashRegister(50.0, till, pose, g.world);
        CHECK(g.strike(till, HitKind::Charge).broke);
        CHECK_FALSE(g.props.useCashRegister(50.0, till, g.world));
        CHECK(g.services.destroyed == std::vector<double>{1001.0});
    }
}

namespace {

// A weapon pile of object type `kind` (docs/research/objects.md#weapon-piles).
ObjectType pileType(int kind, std::uint32_t modelHash = 0x1234U) {
    ObjectType type;
    type.name = "dyn_testpile";
    type.className = std::string(coney::world_objects::kDynPileClass);
    type.objectKind = kind;
    type.modelHash = modelHash;
    return type;
}

} // namespace

TEST_CASE("a beer pile hands out a new bottle with its cue on every take and never runs out",
          "[world_objects][props][piles]") {
    Fixture f;
    const ObjectType beer = pileType(19);
    const coney::world_objects::PropPose pose{.position = {1.0F, 2.0F, 3.0F}, .rotation = {0.0F, 0.0F, 0.0F, 1.0F}};
    constexpr int kTakes = 12;
    for (int take = 0; take < kTakes; ++take) {
        const std::optional<double> made = f.props.takeFromPile(50.0, beer, pose, f.world);
        REQUIRE(made.has_value());
        if (!made) {
            return;
        }
        CHECK(*made == 1001.0 + take);
    }
    CHECK(f.services.spawned == std::vector<std::string>(kTakes, "dyn_beerbottle"));
    CHECK(f.services.cues == std::vector<int>(kTakes, 27));
    CHECK_FALSE(f.props.pileSpent(50.0));
    for (int tick = 0; tick < 2 * Props::kSpentPileTicks; ++tick) {
        f.props.tick(f.world);
    }
    CHECK(f.props.takeRemoved().empty());
}

TEST_CASE("the second molotov pile runs out after five takes and goes two of its updates later",
          "[world_objects][props][piles]") {
    Fixture f;
    const ObjectType molotovs = pileType(22, 0x69b9d1a0U);
    const coney::world_objects::PropPose pose{};
    for (int take = 0; take < Props::kMolotovPileTakes; ++take) {
        CHECK(f.props.takeFromPile(50.0, molotovs, pose, f.world).value_or(0.0) != coney::world_objects::kNoObject);
    }
    CHECK(f.services.spawned == std::vector<std::string>(5, "dyn_molotv"));
    CHECK(f.props.pileSpent(50.0));
    CHECK(f.props.takeFromPile(50.0, molotovs, pose, f.world) == coney::world_objects::kNoObject);
    CHECK(f.services.spawned.size() == 5);
    for (int tick = 1; tick < 2 * Props::kSpentPileTicks; ++tick) {
        f.props.tick(f.world);
    }
    CHECK(f.props.takeRemoved().empty());
    f.props.tick(f.world);
    CHECK(f.props.takeRemoved() == std::vector<double>{50.0});
    // The first molotov pile never runs out.
    const ObjectType first = pileType(22);
    for (int take = 0; take < 2 * Props::kMolotovPileTakes; ++take) {
        static_cast<void>(f.props.takeFromPile(60.0, first, pose, f.world));
    }
    CHECK_FALSE(f.props.pileSpent(60.0));
}

TEST_CASE("a pile with no take of its own is picked up itself", "[world_objects][props][piles]") {
    Fixture f;
    CHECK_FALSE(f.props.takeFromPile(50.0, pileType(2), coney::world_objects::PropPose{}, f.world).has_value());
    CHECK(f.services.spawned.empty());
    // The spray-can box makes nothing in the hand, and is not taken itself either.
    CHECK(f.props.takeFromPile(51.0, pileType(44), coney::world_objects::PropPose{}, f.world) ==
          coney::world_objects::kNoObject);
}

TEST_CASE("a broken prop goes after its update interval", "[world_objects][props]") {
    Fixture f;
    f.strike(masksType(0, 1));
    for (int tick = 1; tick < Props::kRemovalTicks; ++tick) {
        f.props.tick(f.world);
    }
    CHECK(f.props.takeRemoved().empty());
    f.props.tick(f.world);
    CHECK(f.props.takeRemoved() == std::vector<double>{50.0});
    f.props.tick(f.world);
    CHECK(f.props.takeRemoved().empty());
    f.props.clear();
    CHECK_FALSE(f.props.broken(50.0));
}

TEST_CASE("the hit counters: the second first while flying or held, 4 + 6k or one point", "[world_objects][props]") {
    using coney::world_objects::takeHit;
    std::uint8_t counter = 20;
    std::uint8_t second = 2;
    takeHit(counter, second, 0, false, true);
    CHECK(second == 1);
    CHECK(counter == 20);
    takeHit(counter, second, 2, false, false);
    CHECK(counter == 4);
    takeHit(counter, second, 0, true, false);
    CHECK(counter == 3);
    takeHit(counter, second, -1, false, false);
    CHECK(counter == 2);
    takeHit(counter, second, 3, false, false);
    CHECK(counter == 0);
    std::uint8_t unbreakable = 0xff;
    takeHit(unbreakable, second, 0, false, false);
    CHECK(unbreakable == 0xff);
}

TEST_CASE("body flags from the type's word and which make a strike target", "[world_objects][props]") {
    using coney::world_objects::bodyFlagsOf;
    using coney::world_objects::isStrikeTarget;
    CHECK(bodyFlagsOf(0) == 0x80000500U);
    CHECK(bodyFlagsOf(0x1) == 0x80000502U);
    CHECK(bodyFlagsOf(0x4) == 0x80000510U);
    CHECK(bodyFlagsOf(0x100) == 0x80020500U);
    CHECK(bodyFlagsOf(0xbf) == 0x8001057eU);
    CHECK_FALSE(isStrikeTarget(bodyFlagsOf(0x3)));
    CHECK(isStrikeTarget(bodyFlagsOf(0x4)));
    CHECK(isStrikeTarget(bodyFlagsOf(0x8)));
    CHECK(isStrikeTarget(bodyFlagsOf(0x20)));
}

TEST_CASE("a prop's body: a turned box about its centre, or a sphere of half its x", "[world_objects][props]") {
    using coney::world_objects::bodyTouches;
    ObjectType box;
    box.bodyShape = coney::world_objects::kBodyBox;
    box.bodySize = {2.0F, 0.5F, 1.0F};
    box.bodyCentre = {0.0F, 0.0F, 0.5F};
    const Vec3 at{10, 0, 0};
    const Quat none{0, 0, 0, 1};
    CHECK(bodyTouches(box, at, none, Vec3{10.9F, 0, 0.5F}, 0.05F));
    CHECK_FALSE(bodyTouches(box, at, none, Vec3{10, 0.5F, 0.5F}, 0.1F));
    CHECK(bodyTouches(box, at, none, Vec3{10, 0.3F, 0.5F}, 0.1F));
    // Turned 90 degrees about z, its long side runs along y.
    const Quat quarter{0, 0, 0.70710678F, 0.70710678F};
    CHECK(bodyTouches(box, at, quarter, Vec3{10, 0.9F, 0.5F}, 0.05F));
    CHECK_FALSE(bodyTouches(box, at, quarter, Vec3{10.9F, 0, 0.5F}, 0.05F));
    ObjectType ball;
    ball.bodyShape = coney::world_objects::kBodySphere;
    ball.bodySize = {1.0F, 0, 0};
    CHECK(bodyTouches(ball, at, none, Vec3{10.55F, 0, 0}, 0.1F));
    CHECK_FALSE(bodyTouches(ball, at, none, Vec3{10.7F, 0, 0}, 0.1F));
    ObjectType bare;
    bare.bodySize = {1.0F, 1.0F, 1.0F};
    CHECK_FALSE(bodyTouches(bare, at, none, at, 1.0F));
}

TEST_CASE("every hit raises two dust bursts; the crate stack and the bench leave their pieces",
          "[world_objects][props]") {
    // docs/research/objects.md#riot-prop-breaks: the dust at every hit, the crate's dyn_wooddmg_a 0.604 m below it, the
    // bench's 25 splinters and dyn_parkbench_aa 0.395 m below it.
    SECTION("the bench: dust on each blow, its piece on the break") {
        Fixture f;
        const ObjectType bench = masksType(10, 2, 0x7852cedbU);
        f.strike(bench);
        CHECK(f.services.dusts == 2);
        CHECK(f.services.spawned.empty());
        f.strike(bench);
        f.strike(bench);
        CHECK(f.services.dusts == 6);
        CHECK(f.services.splinterCount == Props::kBenchSplinters);
        CHECK(f.services.spawned == std::vector<std::string>{"dyn_parkbench_aa"});
    }
    SECTION("the crate stack: its piece on the first blow") {
        Fixture f;
        f.strike(masksType(0, 1, 0x1a0686f7U));
        CHECK(f.services.spawned == std::vector<std::string>{"dyn_wooddmg_a"});
        CHECK(f.services.splinterCount == 0);
    }
    SECTION("a newsstand: no piece") {
        Fixture f;
        f.strike(masksType(0, 1, 0x18f31e91U));
        CHECK(f.services.spawned.empty());
        CHECK(f.services.dusts == 2);
    }
    SECTION("one model breaks against tin") {
        Fixture f;
        f.strike(masksType(0, 1, 0xe42da444U));
        CHECK(f.services.pairs.back() == std::pair<std::uint8_t, std::uint8_t>{47, 23});
    }
}
