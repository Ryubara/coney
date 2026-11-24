// SPDX-License-Identifier: GPL-3.0-or-later
// Unit 9 of the physics world (docs/research/physics-bodies.md#unit-9): creating and freeing each kind of body from
// its pool, with made-up owners.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <variant>
#include <vector>

#include "animation/anim_math.h"
#include "body_owners_fake.h"
#include "core/engine_limits.h"
#include "physics/human_body.h"
#include "physics/physics_world.h"
#include "physics/shapes.h"

using Catch::Approx;
using coney::EngineLimits;
using coney::anim::Vec3;
using namespace coney::physics;
using coney::physics::test::FakeOwners;

namespace {

constexpr double kEps = 1e-6;
constexpr std::uint32_t kHuman = 0x40;
constexpr std::uint32_t kObject = 0x8;

// The bone shapes of a human body, in chain order.
std::vector<ShapeId> bones(const PhysicsWorld& world, BodyId id) {
    std::vector<ShapeId> out;
    for (ShapeId s = world.body(id).boneChain; s != kNoShape; s = world.shape(s).next) {
        out.push_back(s);
    }
    return out;
}

// The human list from its head.
std::vector<BodyId> humanList(const PhysicsWorld& world) {
    std::vector<BodyId> out;
    for (BodyId b = world.humanHead(); b != kNoBody; b = world.body(b).humanNext) {
        out.push_back(b);
    }
    return out;
}

// A standing human owner at `at` whose head bone is 1.8 m up, so its posed capsule is 2.0 m tall at scale 1.
void standingHuman(FakeOwners& owners, OwnerId id, Vec3 at) {
    owners[id].position = at;
    owners[id].human = HumanCapsuleState{};
    owners[id].bones[6] = Transform{.position = {0.0F, 0.0F, 1.8F}, .rotation = {}};
}

} // namespace

TEST_CASE("a human body takes the first free set, the human flags and its capsule on with the bone shapes off") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    standingHuman(owners, 1, Vec3{});
    const BodyId id = world.createHumanBody(1, kHuman, owners);
    REQUIRE(id == world.bodyPoolBody(BodyPool::Human, 0));
    const WorldBody& b = world.body(id);
    // 0x8002243f, the dirty bit then cleared by the update the scale step ends with.
    CHECK(b.flags == 0x0002243fU);
    CHECK(b.owner == 1);
    CHECK(b.typeMask == kHuman);
    CHECK(b.vectorIndex == 0);
    CHECK(b.scale == Vec3{1.0F, 1.0F, 1.0F});
    CHECK(world.shape(b.firstShape).flags == 0x11);
    const std::vector<ShapeId> chain = bones(world, id);
    REQUIRE(chain.size() == 10);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const bool target = i == 0 || i == 5; // the spine and the head
        CHECK(world.shape(chain[i]).flags == (target ? 0x6 : 0x2));
    }
    CHECK(humanList(world) == std::vector<BodyId>{id});
    const auto& capsule = std::get<Capsule>(world.shape(b.firstShape).posed);
    CHECK(capsule.height == Approx(2.0).margin(kEps));
    CHECK(capsule.radius == Approx(0.35).margin(kEps));
}

TEST_CASE("a human body's scale step scales its capsule and its ten bone shapes from the tables") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    owners[1].scale = 1.1F; // its capsule posed as a plain owner's, so its height stays as the scale step set it
    const BodyId id = world.createHumanBody(1, kHuman, owners);
    const auto& capsule = std::get<Capsule>(world.shape(world.body(id).firstShape).posed);
    CHECK(capsule.height == Approx(2.2).margin(kEps));
    CHECK(capsule.radius == Approx(0.385).margin(kEps));
    const std::vector<ShapeId> chain = bones(world, id);
    REQUIRE(chain.size() == 10);
    const ShapeRecord& spine = world.shape(chain[0]);
    CHECK(spine.bone == 3);
    CHECK(spine.offset.x == Approx(-0.077).margin(kEps));
    CHECK(spine.offset.z == Approx(0.055).margin(kEps));
    CHECK(std::get<Segment>(spine.posed).radius == Approx(0.198).margin(kEps));
    CHECK(spine.segmentLength == Approx(0.418).margin(kEps));
    for (const std::size_t forearm : {1U, 2U}) {
        CHECK(std::get<Segment>(world.shape(chain[forearm]).posed).radius == Approx(0.077).margin(kEps));
        CHECK(world.shape(chain[forearm]).segmentLength == Approx(0.22).margin(kEps));
    }
    for (const std::size_t shin : {3U, 4U}) {
        CHECK(std::get<Segment>(world.shape(chain[shin]).posed).radius == Approx(0.11).margin(kEps));
        CHECK(world.shape(chain[shin]).segmentLength == Approx(0.44).margin(kEps));
    }
    const ShapeRecord& head = world.shape(chain[5]);
    CHECK(head.bone == 6);
    CHECK(head.offset.x == Approx(0.055).margin(kEps));
    CHECK(head.offset.z == Approx(0.033).margin(kEps));
    CHECK(std::get<Sphere>(head.posed).radius == Approx(0.165).margin(kEps));
    for (const std::size_t hand : {6U, 7U}) {
        CHECK(world.shape(chain[hand]).offset.x == Approx(0.088).margin(kEps));
        CHECK(std::get<Sphere>(world.shape(chain[hand]).posed).radius == Approx(0.099).margin(kEps));
    }
    for (const std::size_t foot : {8U, 9U}) {
        CHECK(std::get<Sphere>(world.shape(chain[foot]).posed).radius == Approx(0.165).margin(kEps));
    }
    // The bones in chain order: 3, 24, 18, 32, 29, then 6, 25, 19, 33, 30.
    const std::vector<int> expected{3, 24, 18, 32, 29, 6, 25, 19, 33, 30};
    for (std::size_t i = 0; i < chain.size(); ++i) {
        CHECK(world.shape(chain[i]).bone == expected[i]);
    }
}

TEST_CASE("each pool holds the original's count under the original's limits, and a full pool returns no body") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world(EngineLimits::Original);
    FakeOwners owners;
    OwnerId next = 1;
    for (int i = 0; i < 60; ++i) {
        CHECK(world.createHumanBody(next++, kHuman, owners) != kNoBody);
    }
    CHECK(world.createHumanBody(next++, kHuman, owners) == kNoBody);
    for (int i = 0; i < 416; ++i) {
        CHECK(world.createObjectBody(next++, kObject) != kNoBody);
    }
    CHECK(world.createObjectBody(next++, kObject) == kNoBody);
    for (int i = 0; i < 18; ++i) {
        CHECK(world.createCarBody(next++, kObject) != kNoBody);
    }
    CHECK(world.createCarBody(next++, kObject) == kNoBody);
    for (int i = 0; i < 255; ++i) {
        CHECK(world.createBoxBody(next++, kObject, 1.0F, 1.0F) != kNoBody);
    }
    CHECK(world.createBoxBody(next++, kObject, 1.0F, 1.0F) == kNoBody);
}

TEST_CASE("past the original's counts Coney's pools build one more body") {
    // Expected: unverified, #376 (Coney's lifted limit, docs/research/physics.md#coneys-implementation)
    PhysicsWorld world;
    FakeOwners owners;
    OwnerId next = 1;
    for (int i = 0; i < 61; ++i) {
        CHECK(world.createHumanBody(next++, kHuman, owners) != kNoBody);
    }
    CHECK(world.bodyPoolSize(BodyPool::Human) == 61);
    CHECK(bones(world, world.bodyPoolBody(BodyPool::Human, 60)).size() == 10);
}

TEST_CASE("a freed slot is the next one taken, the lowest free first") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    const BodyId a = world.createObjectBody(1, kObject);
    const BodyId b = world.createObjectBody(2, kObject);
    const BodyId c = world.createObjectBody(3, kObject);
    CHECK(a == world.bodyPoolBody(BodyPool::Object, 0));
    CHECK(b == world.bodyPoolBody(BodyPool::Object, 1));
    CHECK(c == world.bodyPoolBody(BodyPool::Object, 2));
    world.freeObjectBody(b);
    CHECK(world.createObjectBody(4, kObject) == b);
}

TEST_CASE("freeing a body swap-removes it from the body vector and keeps its own index") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    const BodyId a = world.createObjectBody(1, kObject);
    world.body(a).firstShape = world.allocObjectBox();
    std::ignore = world.createObjectBody(2, kObject);
    const BodyId c = world.createObjectBody(3, kObject);
    world.freeObjectBody(a);
    CHECK(world.bodyVector().size() == 2);
    CHECK(world.bodyVector().at(0) == c);
    CHECK(world.body(c).vectorIndex == 0);
    CHECK(world.body(a).vectorIndex == 0);
}

TEST_CASE("an object body starts with the object flags and no shape, and its free gives its shape back") {
    // Expected: physics-bodies.md#u9-create, confirmed (code)
    PhysicsWorld world;
    const BodyId id = world.createObjectBody(1, kObject);
    CHECK(world.body(id).flags == 0x80000500U);
    CHECK(world.body(id).firstShape == kNoShape);
    CHECK(world.body(id).scale == Vec3{1.0F, 1.0F, 1.0F});
    const ShapeId sphere = world.allocObjectSphere();
    world.addShape(id, sphere);
    world.ignore(id, 7);
    world.freeObjectBody(id);
    CHECK(world.shape(sphere).flags == (kShapeEnabled | kShapeFlag2));
    CHECK(world.body(id).firstShape == kNoShape);
    CHECK(world.body(id).owner == kNoOwner);
    CHECK_FALSE(world.isIgnoring(id, 7));
}

TEST_CASE("a car body starts bare, takes the car flags with its boxes, and its free unchains them") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    const BodyId id = world.createCarBody(1, kObject);
    CHECK(world.body(id).flags == 0x80000000U);
    CHECK(world.body(id).firstShape == kNoShape);
    const ShapeId body = world.allocCarBox();
    const ShapeId cabin = world.allocCarBox();
    world.addShape(id, body);
    world.addShape(id, cabin);
    world.body(id).flags |= 0x2211aU; // what Car_MakeBodies ORs in
    CHECK(world.body(id).flags == 0x8002211aU);
    world.freeCarBody(id);
    CHECK((world.shape(body).flags & kShapeAllocated) == 0);
    CHECK((world.shape(cabin).flags & kShapeAllocated) == 0);
    CHECK(world.shape(body).next == kNoShape);
    CHECK(world.body(id).firstShape == kNoShape);
    CHECK(world.body(id).owner == kNoOwner);
}

TEST_CASE("a pane's box body takes its paired box sized width by 0.5 by height, and its free keeps the box") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    const BodyId id = world.createBoxBody(1, kObject, 1.2F, 2.0F);
    CHECK(id == world.bodyPoolBody(BodyPool::Pane, 0));
    CHECK(world.body(id).flags == 0x8000007aU);
    const ShapeId box = world.body(id).firstShape;
    REQUIRE(box == world.paneBox(0));
    const auto& half = std::get<Box>(world.shape(box).posed).half;
    CHECK(half[0] == Approx(0.6).margin(kEps));
    CHECK(half[1] == Approx(0.25).margin(kEps));
    CHECK(half[2] == Approx(1.0).margin(kEps));
    CHECK(world.shape(box).flags == 0x9);
    world.ignore(id, 7);
    world.freeBoxBody(id);
    CHECK(world.shape(box).flags == 0x1);
    CHECK(world.body(id).firstShape == box);
    CHECK(world.body(id).owner == kNoOwner);
    CHECK(world.isIgnoring(id, 7)); // the pane's free skips StopIgnoring
}

TEST_CASE("the punch bag takes its one set without a scale step: capsule on, its segment a target only") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    const BodyId id = world.createPunchBagBody(1, kHuman);
    CHECK(id == world.bodyPoolBody(BodyPool::PunchBag, 0));
    CHECK(world.body(id).flags == 0x8000243fU);
    CHECK(world.shape(world.body(id).firstShape).flags == 0x11);
    const std::vector<ShapeId> chain = bones(world, id);
    REQUIRE(chain.size() == 1);
    CHECK(world.shape(chain[0]).flags == 0x4);
    CHECK(world.shape(chain[0]).bone == 3);
    CHECK(world.shape(chain[0]).segmentLength == Approx(1.5).margin(kEps));
    CHECK(std::get<Segment>(world.shape(chain[0]).posed).radius == Approx(0.35).margin(kEps));
    CHECK(humanList(world) == std::vector<BodyId>{id});
    // The set is taken again whether or not it is free.
    CHECK(world.createPunchBagBody(2, kHuman) == id);
}

TEST_CASE("human bodies join the human list at its tail and leave it from anywhere") {
    // Expected: physics-bodies.md#u9-human-list, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId a = world.createHumanBody(1, kHuman, owners);
    const BodyId b = world.createHumanBody(2, kHuman, owners);
    const BodyId c = world.createHumanBody(3, kHuman, owners);
    CHECK(humanList(world) == std::vector<BodyId>{a, b, c});
    world.freeHumanBody(b);
    CHECK(humanList(world) == std::vector<BodyId>{a, c});
    CHECK(world.body(c).humanPrev == a);
    world.freeHumanBody(a);
    CHECK(humanList(world) == std::vector<BodyId>{c});
    CHECK(world.body(c).humanPrev == kNoBody);
}

TEST_CASE("freeing a human body frees its push sphere, its ignore slots and the slots naming its owner") {
    // Expected: physics-bodies.md#u9-tests, confirmed (code)
    PhysicsWorld world;
    FakeOwners owners;
    const BodyId id = world.createHumanBody(1, kHuman, owners);
    const BodyId other = world.createHumanBody(2, kHuman, owners);
    setPushSphere(world, id, true);
    const ShapeId sphere = world.body(id).pushSphere;
    REQUIRE(sphere != kNoShape);
    world.ignore(id, 5);
    world.ignore(id, 6);
    world.ignore(id, kNoOwner); // the level
    world.ignore(other, 1);
    world.freeHumanBody(id);
    CHECK((world.shape(sphere).flags & kShapeAllocated) == 0);
    CHECK(world.body(id).pushSphere == kNoShape);
    CHECK_FALSE(world.isIgnoring(id, 5));
    CHECK_FALSE(world.isIgnoring(id, 6));
    CHECK_FALSE(world.body(id).ignoresLevel);
    CHECK_FALSE(world.isIgnoring(other, 1));
    CHECK(world.body(id).owner == kNoOwner);
    CHECK(humanList(world) == std::vector<BodyId>{other});
    for (std::size_t i = 0; i < world.ignoreSlotCount(); ++i) {
        CHECK(world.ignoreSlot(static_cast<IgnoreSlotId>(i)).owner == kNoOwner);
    }
}
