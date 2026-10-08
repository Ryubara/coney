// SPDX-License-Identifier: GPL-3.0-or-later
// The breakable street props: the hit counters, a strike's order and message 6, a dyn_masks prop's break and removal,
// the body flags and the body test (docs/research/objects.md#breakable-props). Synthetic types only.

#include "world_objects/props.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/object_fixtures.h"

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
    ObjectWorld world{.collision = nullptr, .paths = nullptr, .services = &services, .random = nullptr};
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

TEST_CASE("a strike on a trash can counts only while its counter lasts", "[world_objects][props]") {
    Fixture f;
    ObjectType can;
    can.name = "test_can";
    can.className = "overhead_weapon";
    can.hitpoints = 150;
    can.value = 1;
    CHECK(f.strike(can).intactBefore);
    CHECK(f.props.counter(50.0) == std::uint8_t{0});
    CHECK_FALSE(f.strike(can).intactBefore);
    CHECK(f.services.damage.size() == 1);
    // Not a dyn_masks prop: it never breaks or goes.
    CHECK_FALSE(f.props.broken(50.0));
    CHECK(f.services.bodies.empty());
}

TEST_CASE("a broken prop goes after its update interval", "[world_objects][props]") {
    Fixture f;
    f.strike(masksType(0, 1));
    for (int tick = 1; tick < Props::kRemovalTicks; ++tick) {
        f.props.tick();
    }
    CHECK(f.props.takeRemoved().empty());
    f.props.tick();
    CHECK(f.props.takeRemoved() == std::vector<double>{50.0});
    f.props.tick();
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
