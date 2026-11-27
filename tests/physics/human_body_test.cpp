// SPDX-License-Identifier: GPL-3.0-or-later
// Unit 10 of the physics world (docs/research/physics-bodies.md#unit-10): the human body set's strike switch, update
// and broad bound, Moved, push sphere and push, and the strike test, with made-up owners.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "animation/anim_math.h"
#include "body_owners_fake.h"
#include "core/engine_limits.h"
#include "physics/contact.h"
#include "physics/human_body.h"
#include "physics/physics_world.h"
#include "physics/shapes.h"

using Catch::Approx;
using coney::EngineLimits;
using coney::anim::Mat34;
using coney::anim::Vec3;
using namespace coney::physics;
using coney::physics::test::FakeOwners;

namespace {

constexpr double kEps = 1e-5;
constexpr std::uint32_t kHuman = 0x40;
constexpr std::uint32_t kObject = 0x8;

// The bone shape of `bone` on a human body.
ShapeId boneShape(const PhysicsWorld& world, BodyId id, int bone) {
    for (ShapeId s = world.body(id).boneChain; s != kNoShape; s = world.shape(s).next) {
        if (world.shape(s).bone == bone) {
            return s;
        }
    }
    return kNoShape;
}

// A human body for owner `owner` standing at `at` (its capsule posed as a plain owner's, so 2.0 m tall at scale 1).
BodyId human(PhysicsWorld& world, FakeOwners& owners, OwnerId owner, Vec3 at) {
    owners[owner].position = at;
    const BodyId id = world.createHumanBody(owner, kHuman, owners);
    owners[owner].body = id;
    return id;
}

// Puts a switched-on sphere at `now`, having been at `before` at the last pose.
void placeSphere(PhysicsWorld& world, ShapeId s, Vec3 before, Vec3 now) {
    ShapeRecord& shape = world.shape(s);
    std::get<Sphere>(shape.posed).centre = now;
    shape.previous = before;
    shape.flags = static_cast<std::uint16_t>(shape.flags & ~kShapeReset);
}

// An object body for `owner` with one box of half extents `half` centred at `at`, flags `flags`, posed and listed.
BodyId boxBody(PhysicsWorld& world, FakeOwners& owners, OwnerId owner, Vec3 at, Vec3 half, std::uint32_t flags) {
    const BodyId id = world.createObjectBody(owner, kObject);
    const ShapeId box = world.allocObjectBox();
    std::get<Box>(world.shape(box).posed).half = {half.x, half.y, half.z};
    world.addShape(id, box);
    owners[owner].position = at;
    owners[owner].body = id;
    std::ignore = world.updateBody(id, owners[owner]);
    world.body(id).flags = flags;
    return id;
}

// Every strike recorded so far, by the struck owner.
std::vector<OwnerId> struckOwners(const FakeOwners& owners) {
    std::vector<OwnerId> out;
    out.reserve(owners.strikeLog.size());
    for (const auto& hit : owners.strikeLog) {
        out.push_back(hit.contact.halves[1].ownerId);
    }
    return out;
}

} // namespace

// ---- the strike switch ----

TEST_CASE("a strike event switches its bone's bit and first shape, and bones 0 and 32 share a bit") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId id = human(world, owners, 1, Vec3{});
    const ShapeId hand = boneShape(world, id, 25);
    setStrikeShapeEnabled(world, id, 25, true);
    CHECK(world.body(id).strikeBits == 0x800000U);
    CHECK(world.shape(hand).flags == 0x13);
    world.ignore(id, 9);
    setStrikeShapeEnabled(world, id, 25, true);
    CHECK(world.body(id).strikeBits == 0x800000U);
    CHECK_FALSE(world.isIgnoring(id, 9));

    setStrikeShapeEnabled(world, id, 25, false);
    setStrikeShapeEnabled(world, id, 0, true);
    setStrikeShapeEnabled(world, id, 32, true);
    CHECK(world.body(id).strikeBits == 0x40000000U);
    CHECK((world.shape(boneShape(world, id, 32)).flags & kShapeEnabled) == 0);

    setStrikeShapeEnabled(world, id, 19, true);
    setStrikeShapeEnabled(world, id, -1, false);
    CHECK(world.body(id).strikeBits == 0);
    for (ShapeId s = world.body(id).boneChain; s != kNoShape; s = world.shape(s).next) {
        CHECK((world.shape(s).flags & kShapeEnabled) == 0);
    }
}

TEST_CASE("every strike event empties the struck list, even one that changes nothing") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId id = human(world, owners, 1, Vec3{});
    world.ignore(id, 9);
    REQUIRE(world.isIgnoring(id, 9));
    setStrikeShapeEnabled(world, id, 19, false);
    CHECK_FALSE(world.isIgnoring(id, 9));
}

// ---- the update and the broad bound ----

TEST_CASE("the broad bound is as wide as the reach or 0.8 m times the scale, round the capsule") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].reach = 1.37F;
    const BodyId id = human(world, owners, 1, Vec3{10.0F, 5.0F, 0.0F});
    setStrikeShapeEnabled(world, id, 25, true);
    updateHumanBody(world, id, owners);
    const Aabb& bound = world.body(id).broadBound;
    CHECK(bound.min.x == Approx(8.63).margin(kEps));
    CHECK(bound.min.y == Approx(3.63).margin(kEps));
    CHECK(bound.min.z == Approx(0.0).margin(kEps));
    CHECK(bound.max.x == Approx(11.37).margin(kEps));
    CHECK(bound.max.y == Approx(6.37).margin(kEps));
    CHECK(bound.max.z == Approx(2.0).margin(kEps));

    owners[1].reach = 0.0F;
    updateHumanBody(world, id, owners);
    CHECK(world.body(id).broadBound.max.x == Approx(10.8).margin(kEps));

    PhysicsWorld big;
    FakeOwners bigOwners;
    bigOwners[2].scale = 1.3F;
    bigOwners[2].reach = 0.94F;
    const BodyId large = human(big, bigOwners, 2, Vec3{10.0F, 5.0F, 0.0F});
    setStrikeShapeEnabled(big, large, 25, true);
    updateHumanBody(big, large, bigOwners);
    CHECK(big.body(large).broadBound.max.x == Approx(11.04).margin(kEps));
    CHECK(big.body(large).broadBound.min.y == Approx(3.96).margin(kEps));
}

TEST_CASE("with a weapon sphere the broad bound reaches the first sphere's far side and the reach is not used") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].reach = 1.37F;
    const BodyId id = human(world, owners, 1, Vec3{10.0F, 5.0F, 0.0F});
    const ShapeId sphere = world.findFreeStrikeSphere();
    world.shape(sphere).offset = Vec3{0.6F, 0.8F, 1.2F};
    std::get<Sphere>(world.shape(sphere).posed).radius = 0.1F;
    setEnabled(world.shape(sphere), true);
    addStrikeSphere(world, id, sphere);
    world.body(id).weaponTimer = 3;
    updateHumanBody(world, id, owners);
    const Vec3 centre = std::get<Sphere>(world.shape(sphere).posed).centre;
    CHECK(centre.x == Approx(10.6).margin(kEps));
    CHECK(centre.y == Approx(5.8).margin(kEps));
    CHECK(world.body(id).broadBound.max.x == Approx(11.1).margin(kEps));
    CHECK(world.body(id).broadBound.min.y == Approx(3.9).margin(kEps));
}

TEST_CASE("an update keeps the broad bound while nothing asks for it, and a body with flag 0x4000 poses nothing") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId id = human(world, owners, 1, Vec3{10.0F, 5.0F, 0.0F});
    const Aabb before = world.body(id).broadBound;
    owners[1].position = Vec3{20.0F, 5.0F, 0.0F};
    owners[1].reach = 3.0F;
    updateHumanBody(world, id, owners);
    CHECK(world.body(id).broadBound.max.x == before.max.x);

    world.body(id).flags |= 0x4000U | kBodyFlagDirty;
    updateHumanBody(world, id, owners);
    CHECK(std::get<Capsule>(world.shape(world.body(id).firstShape).posed).base.x == Approx(10.0).margin(kEps));
    CHECK((world.body(id).flags & kBodyFlagDirty) != 0);
}

// ---- Moved ----

TEST_CASE("Moved sets the broad bound on the new pose and moves the hand's centre there without resetting it") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId id = human(world, owners, 1, Vec3{10.0F, 5.0F, 0.0F});
    const ShapeId hand = boneShape(world, id, 25);
    setStrikeShapeEnabled(world, id, 25, true);
    placeSphere(world, hand, Vec3{10.5F, 5.0F, 1.4F}, Vec3{10.6F, 5.0F, 1.4F});
    Mat34 m;
    m.t = Vec3{3.0F, 4.0F, 0.5F};
    humanBodyMoved(world, id, m);
    const Aabb& bound = world.body(id).broadBound;
    CHECK(bound.min.x == Approx(2.2).margin(kEps));
    CHECK(bound.min.y == Approx(3.2).margin(kEps));
    CHECK(bound.min.z == Approx(0.5).margin(kEps));
    CHECK(bound.max.x == Approx(3.8).margin(kEps));
    CHECK(bound.max.y == Approx(4.8).margin(kEps));
    CHECK(bound.max.z == Approx(2.5).margin(kEps));
    CHECK(std::get<Sphere>(world.shape(hand).posed).centre == Vec3{3.0F, 4.0F, 0.5F});
    CHECK(world.shape(hand).previous == Vec3{10.5F, 5.0F, 1.4F});

    // The next pose with the hand at (3.6, 4.0, 1.9) moves it from the origin, not across the teleport.
    owners[1].position = Vec3{3.0F, 4.0F, 0.5F};
    owners[1].bones[25] = Transform{.position = {0.52F, 0.0F, 1.4F}, .rotation = {}};
    updateHumanBody(world, id, owners);
    const Vec3 now = std::get<Sphere>(world.shape(hand).posed).centre;
    CHECK(now.x == Approx(3.6).margin(kEps));
    CHECK(now.z == Approx(1.9).margin(kEps));
    CHECK(world.shape(hand).previous == Vec3{3.0F, 4.0F, 0.5F});
}

// ---- the push sphere and the push ----

TEST_CASE("the push sphere is taken once, given back off, and none is left when all are taken") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world(EngineLimits::Original);
    FakeOwners owners;
    const BodyId id = human(world, owners, 1, Vec3{});
    setPushSphere(world, id, true);
    const ShapeId sphere = world.body(id).pushSphere;
    REQUIRE(sphere != kNoShape);
    CHECK(std::get<Sphere>(world.shape(sphere).posed).radius == Approx(2.0).margin(kEps));
    setPushSphere(world, id, true);
    CHECK(world.body(id).pushSphere == sphere);
    setPushSphere(world, id, false);
    CHECK((world.shape(sphere).flags & kShapeAllocated) == 0);
    CHECK(world.body(id).pushSphere == kNoShape);

    for (int i = 0; i < 30; ++i) {
        std::ignore = world.allocBigSphere();
    }
    setPushSphere(world, id, true);
    CHECK(world.body(id).pushSphere == kNoShape);
}

TEST_CASE("the push sphere pushes each human it meets 0.25 m along the line, divided by his weights") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId me = human(world, owners, 1, Vec3{});
    const BodyId other = human(world, owners, 2, Vec3{1.0F, 1.0F, 0.5F});
    owners[1].weight = 1.0F;
    owners[2].weight = 1.0F;
    owners[2].pushWeight = 1.0F;
    setPushSphere(world, me, true);
    updateHumanBody(world, me, owners);
    pushAwayHumans(world, me, owners);
    CHECK(world.body(other).pushOut.x == Approx(0.166667).margin(kEps));
    CHECK(world.body(other).pushOut.y == Approx(0.166667).margin(kEps));
    CHECK(world.body(other).pushOut.z == Approx(0.083333).margin(kEps));
    CHECK(world.body(me).pushOut == Vec3{});

    world.body(other).pushOut = Vec3{};
    owners[2].pushWeight = 2.0F;
    pushAwayHumans(world, me, owners);
    CHECK(world.body(other).pushOut.x == Approx(0.083333).margin(kEps));
    CHECK(world.body(other).pushOut.z == Approx(0.041667).margin(kEps));

    world.body(other).pushOut = Vec3{};
    owners[2].pushWeight = 1.0F;
    owners[2].position = Vec3{0.0F, 2.0F, 0.0F};
    pushAwayHumans(world, me, owners);
    CHECK(world.body(other).pushOut.x == Approx(0.0).margin(kEps));
    CHECK(world.body(other).pushOut.y == Approx(0.25).margin(kEps));
}

TEST_CASE("the push leaves out the target, a player, an unpushable body and a human at the same point") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId me = human(world, owners, 1, Vec3{});
    const BodyId other = human(world, owners, 2, Vec3{1.0F, 0.0F, 0.0F});
    setPushSphere(world, me, true);
    updateHumanBody(world, me, owners);

    owners[1].target = 2;
    pushAwayHumans(world, me, owners);
    owners[1].target = kNoOwner;
    owners[2].player = true;
    pushAwayHumans(world, me, owners);
    owners[2].player = false;
    world.body(other).flags |= 0x200U;
    pushAwayHumans(world, me, owners);
    world.body(other).flags &= ~0x200U;
    CHECK(world.body(other).pushOut == Vec3{});

    owners[2].position = Vec3{};
    pushAwayHumans(world, me, owners);
    CHECK(world.body(other).pushOut == Vec3{});
    CHECK_FALSE(std::isnan(world.body(other).pushOut.x));

    owners[2].position = Vec3{1.0F, 0.0F, 0.0F};
    owners[2].pushWeight = 1e9F;
    pushAwayHumans(world, me, owners);
    CHECK(world.body(other).pushOut.x == Approx(2.5e-10).margin(1e-12));
}

// ---- the strike test ----

TEST_CASE("the strike test's line of sight runs from the chest to a human's chest or an object's first shape") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].reach = 6.0F;
    const BodyId me = human(world, owners, 1, Vec3{});
    setStrikeShapeEnabled(world, me, 25, true);
    updateHumanBody(world, me, owners);
    const BodyId other = human(world, owners, 2, Vec3{3.0F, 4.0F, 0.0F});
    world.body(other).flags |= 0x10U;
    testStrikes(world, me, owners);
    REQUIRE(owners.raysAsked().size() == 1);
    const auto& [ray, options] = owners.raysAsked().front();
    CHECK(ray.origin == Vec3{0.0F, 0.0F, 1.0F});
    CHECK(ray.direction.x == Approx(0.6).margin(kEps));
    CHECK(ray.direction.y == Approx(0.8).margin(kEps));
    CHECK(ray.direction.z == Approx(0.0).margin(kEps));
    CHECK(ray.length == Approx(5.0).margin(kEps));
    CHECK(options == 0x8);

    PhysicsWorld objects;
    FakeOwners objectOwners;
    objectOwners[1].reach = 6.0F;
    const BodyId striker = human(objects, objectOwners, 1, Vec3{});
    setStrikeShapeEnabled(objects, striker, 25, true);
    updateHumanBody(objects, striker, objectOwners);
    std::ignore = boxBody(objects, objectOwners, 2, Vec3{3.0F, 4.0F, 0.5F}, Vec3{0.2F, 0.2F, 0.2F}, 0x10U);
    testStrikes(objects, striker, objectOwners);
    REQUIRE(objectOwners.raysAsked().size() == 1);
    const auto& [boxRay, boxOptions] = objectOwners.raysAsked().front();
    CHECK(boxRay.direction.x == Approx(0.597022).margin(kEps));
    CHECK(boxRay.direction.y == Approx(0.796030).margin(kEps));
    CHECK(boxRay.direction.z == Approx(-0.099504).margin(kEps));
    CHECK(boxRay.length == Approx(5.024938).margin(kEps));
    CHECK(boxOptions == 0x48);
}

TEST_CASE("one hand sweep strikes every box it passes once, not again until a strike event") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].reach = 3.0F;
    const BodyId me = human(world, owners, 1, Vec3{});
    setStrikeShapeEnabled(world, me, 25, true);
    updateHumanBody(world, me, owners);
    std::ignore = boxBody(world, owners, 2, Vec3{1.0F, 0.0F, 1.0F}, Vec3{0.2F, 0.2F, 0.2F}, 0x10U);
    std::ignore = boxBody(world, owners, 3, Vec3{2.0F, 0.0F, 1.0F}, Vec3{0.2F, 0.2F, 0.2F}, 0x10U);
    const ShapeId hand = boneShape(world, me, 25);
    placeSphere(world, hand, Vec3{0.5F, 0.0F, 1.0F}, Vec3{2.5F, 0.0F, 1.0F});
    testStrikes(world, me, owners);
    CHECK(struckOwners(owners) == std::vector<OwnerId>{2, 3});
    CHECK(owners.strikeLog.front().striker == 1);
    CHECK(owners.strikeLog.front().contact.halves[0].shapeId == hand);
    // Put back where it stands now after the sweep.
    CHECK(std::get<Sphere>(world.shape(hand).posed).centre == Vec3{2.5F, 0.0F, 1.0F});

    placeSphere(world, hand, Vec3{0.5F, 0.0F, 1.0F}, Vec3{2.5F, 0.0F, 1.0F});
    testStrikes(world, me, owners);
    CHECK(owners.strikeLog.size() == 2);

    setStrikeShapeEnabled(world, me, 19, false);
    placeSphere(world, hand, Vec3{0.5F, 0.0F, 1.0F}, Vec3{2.5F, 0.0F, 1.0F});
    testStrikes(world, me, owners);
    CHECK(owners.strikeLog.size() == 4);
}

TEST_CASE("the strike test strikes nothing behind a wall, nor a flying object") {
    // Expected: physics-bodies.md#u10-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].reach = 3.0F;
    const BodyId me = human(world, owners, 1, Vec3{});
    setStrikeShapeEnabled(world, me, 25, true);
    updateHumanBody(world, me, owners);
    std::ignore = boxBody(world, owners, 2, Vec3{1.0F, 0.0F, 1.0F}, Vec3{0.2F, 0.2F, 0.2F}, 0x10U);
    const ShapeId hand = boneShape(world, me, 25);
    owners.blocked = true;
    placeSphere(world, hand, Vec3{0.5F, 0.0F, 1.0F}, Vec3{1.5F, 0.0F, 1.0F});
    testStrikes(world, me, owners);
    CHECK(owners.strikeLog.empty());

    owners.blocked = false;
    owners[2].state = 0x4000000U;
    placeSphere(world, hand, Vec3{0.5F, 0.0F, 1.0F}, Vec3{1.5F, 0.0F, 1.0F});
    testStrikes(world, me, owners);
    CHECK(owners.strikeLog.empty());
    CHECK(owners.raysAsked().size() == 1); // the flying object is dropped before its ray
}

TEST_CASE("a human is struck through his target bone shapes, or his capsule alone for a capsule strike") {
    // Expected: physics-bodies.md#u10-strikes, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].reach = 3.0F;
    const BodyId me = human(world, owners, 1, Vec3{});
    setStrikeShapeEnabled(world, me, 25, true);
    updateHumanBody(world, me, owners);
    const BodyId other = human(world, owners, 2, Vec3{1.0F, 0.0F, 0.0F});
    world.body(other).flags |= 0x10U;
    // Every pair this test can try is recorded and missed.
    std::vector<ShapeId> tried;
    auto record = [&](ShapeType a, ShapeType b) {
        world.sweepTest(a, b) = [&tried, &world, other](Contact&, const Shape&, const Shape& target, Vec3) {
            for (ShapeId s = world.body(other).firstShape; s != kNoShape; s = world.shape(s).next) {
                if (&world.shape(s).posed == &target) {
                    tried.push_back(s);
                }
            }
            for (ShapeId s = world.body(other).boneChain; s != kNoShape; s = world.shape(s).next) {
                if (&world.shape(s).posed == &target) {
                    tried.push_back(s);
                }
            }
            return false;
        };
    };
    record(ShapeType::Sphere, ShapeType::Segment);
    record(ShapeType::Sphere, ShapeType::Sphere);
    record(ShapeType::Sphere, ShapeType::Capsule);
    testStrikes(world, me, owners);
    CHECK(tried == std::vector<ShapeId>{boneShape(world, other, 3), boneShape(world, other, 6)});

    tried.clear();
    owners[1].capsuleStrike = true;
    testStrikes(world, me, owners);
    CHECK(tried == std::vector<ShapeId>{world.body(other).firstShape});
}

TEST_CASE("a held weapon's spheres strike a weapon target through the held object's handler") {
    // Expected: physics-bodies.md#u10-strikes, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].held = 9;
    owners[9].weaponAnswer = 5;
    const BodyId me = human(world, owners, 1, Vec3{});
    const ShapeId sphere = world.findFreeStrikeSphere();
    std::get<Sphere>(world.shape(sphere).posed).radius = 0.1F;
    world.shape(sphere).offset = Vec3{1.5F, 0.0F, 1.0F}; // so the broad bound reaches both boxes
    setEnabled(world.shape(sphere), true);
    addStrikeSphere(world, me, sphere);
    world.body(me).weaponTimer = 2;
    owners[1].reach = 3.0F;
    updateHumanBody(world, me, owners);
    // A box target with two boxes: an answer of 5 asks for the next one.
    const BodyId target = boxBody(world, owners, 2, Vec3{1.0F, 0.0F, 1.0F}, Vec3{0.2F, 0.2F, 0.2F}, 0x8U);
    const ShapeId second = world.allocObjectBox();
    std::get<Box>(world.shape(second).posed).half = {0.2F, 0.2F, 0.2F};
    std::get<Box>(world.shape(second).posed).centre = Vec3{1.5F, 0.0F, 1.0F};
    world.addShape(target, second);
    placeSphere(world, sphere, Vec3{0.5F, 0.0F, 1.0F}, Vec3{2.0F, 0.0F, 1.0F});
    testStrikes(world, me, owners);
    REQUIRE(owners.weaponLog.size() == 2);
    CHECK(owners.weaponLog[0].striker == 9);
    CHECK(owners.weaponLog[0].contact.halves[0].ownerId == 9);
    CHECK(owners.weaponLog[1].contact.halves[1].shapeId == second);
    CHECK_FALSE(world.isIgnoring(me, 2)); // both answers asked for more: never struck
    // The timer runs down; at 0 the weapon spheres go.
    CHECK(world.body(me).weaponTimer == 1);
    CHECK((world.body(me).flags & kBodyFlagDirty) != 0);
    testStrikes(world, me, owners);
    CHECK(world.body(me).weaponTimer == 0);
    CHECK(world.body(me).weaponChain == kNoShape);
    CHECK((world.shape(sphere).flags & kShapeEnabled) == 0);
}

TEST_CASE("the strike spheres meet the level first, each hit ending them, with the level's options") {
    // Expected: physics-bodies.md#u10-strikes, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId me = human(world, owners, 1, Vec3{});
    setStrikeShapeEnabled(world, me, 25, true);
    setStrikeShapeEnabled(world, me, 19, true);
    setStrikeShapeEnabled(world, me, 24, true); // a forearm segment: no level sweep for segments
    updateHumanBody(world, me, owners);
    const ShapeId hand = boneShape(world, me, 25);
    placeSphere(world, hand, Vec3{0.0F, 0.0F, 1.0F}, Vec3{0.5F, 0.0F, 1.0F});
    std::vector<std::uint32_t> options;
    Aabb box;
    world.meshSweep(ShapeType::Sphere) = [&](Contact& contact, const ShapeRecord& shape, Vec3 move, const Aabb& b,
                                             BodyId, std::uint32_t o, float low, float high) {
        options.push_back(o);
        box = b;
        CHECK(low == Approx(-0.65));
        CHECK(high == Approx(0.65));
        CHECK(move.x == Approx(0.5).margin(kEps));
        CHECK(std::get<Sphere>(shape.posed).centre == Vec3{0.0F, 0.0F, 1.0F}); // swept from where it was
        contact.halves[0].ownerId = 77;
        return true;
    };
    testStrikes(world, me, owners);
    CHECK(options == std::vector<std::uint32_t>{0x804c});
    CHECK(box.min.x == Approx(-0.09).margin(kEps));
    CHECK(box.max.x == Approx(0.59).margin(kEps));
    REQUIRE(owners.strikeLog.size() == 1);
    CHECK(owners.strikeLog[0].contact.halves[0].ownerId == kNoOwner);
    CHECK(std::get<Sphere>(world.shape(hand).posed).centre == Vec3{0.5F, 0.0F, 1.0F});
}
