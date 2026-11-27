// SPDX-License-Identifier: GPL-3.0-or-later
// The physics world's bodies, x-sorted list, box queries, overlap query, ignore slots and shape pools
// (physics/physics_world.h): docs/research/physics-bodies.md's Unit 7 and Unit 8 test ideas, on made-up bodies with
// boxes only (y and z [0, 1] unless a test says otherwise) and owners that are distinct tags.
#include "physics/physics_world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/engine_limits.h"
#include "physics/contact.h"
#include "physics/shapes.h"

using Catch::Approx;
using coney::EngineLimits;
using coney::anim::Mat34;
using coney::anim::Vec3;
using namespace coney::physics;

namespace {

constexpr double kEps = 1e-5;
constexpr std::uint32_t kAnyType = 0xffffffffU;

// Owners as tags: distinct owner ids, from 1 (0 is no owner), as the world matches owners by id only.
struct Tags {
    [[nodiscard]] OwnerId operator()(std::size_t i) const { return static_cast<OwnerId>(i + 1); }
};

// A world of named bodies whose boxes span `[minX, maxX]` in x and [0, 1] in y and z.
struct Bodies {
    PhysicsWorld world;
    std::vector<std::string> names;
    std::vector<BodyId> ids;

    explicit Bodies(EngineLimits limits = EngineLimits::Coney) : world(limits) {}

    // Adds a body called `name` owned by `owner`, of type mask 1, and adds it to the world.
    BodyId add(const std::string& name, float minX, float maxX, OwnerId owner) {
        const BodyId id = world.createBodyForOwner(owner, 1);
        world.body(id).box = Aabb{.min = {minX, 0.0F, 0.0F}, .max = {maxX, 1.0F, 1.0F}};
        names.push_back(name);
        ids.push_back(id);
        world.addBody(id);
        return id;
    }

    // The name of body `id`.
    [[nodiscard]] std::string name(BodyId id) const {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) {
                return names[i];
            }
        }
        return "?";
    }

    // The x-sorted list from its head, as a string of names.
    [[nodiscard]] std::string order() const {
        std::string out;
        for (BodyId id = world.head(); id != kNoBody; id = world.body(id).next) {
            out += name(id);
        }
        return out;
    }

    // The names of `found`, in order.
    [[nodiscard]] std::string named(const std::vector<BodyId>& found) const {
        std::string out;
        for (const BodyId id : found) {
            out += name(id);
        }
        return out;
    }

    // Moves body `id`'s box to `[minX, maxX]` in x.
    void move(BodyId id, float minX, float maxX) {
        world.body(id).box.min.x = minX;
        world.body(id).box.max.x = maxX;
    }
};

// The box x `[minX, maxX]`, y and z [0, 1].
Aabb boxX(float minX, float maxX) { return Aabb{.min = {minX, 0.0F, 0.0F}, .max = {maxX, 1.0F, 1.0F}}; }

// The six bodies of test idea 6: W[0, 10] P[1, 2] Q[6, 7] S[8, 9] R[8.5, 9.5] T[11, 12].
struct SixBodies {
    Tags tags;
    Bodies b;
    BodyId w, p, q, s, r, t;
    SixBodies()
        : w(b.add("W", 0, 10, tags(0))), p(b.add("P", 1, 2, tags(1))), q(b.add("Q", 6, 7, tags(2))),
          s(b.add("S", 8, 9, tags(3))), r(b.add("R", 8.5F, 9.5F, tags(4))), t(b.add("T", 11, 12, tags(5))) {}
};

// An owner at a position set by the test, for the bodies' updates.
struct PlacedOwner final : ShapeOwner {
    Vec3 at;
    [[nodiscard]] Transform transformNow() const override { return Transform{.position = at, .rotation = {}}; }
    [[nodiscard]] Transform transformStored() const override { return transformNow(); }
    [[nodiscard]] std::optional<Transform> boneTransform(int /*bone*/) const override { return std::nullopt; }
    [[nodiscard]] std::optional<HumanCapsuleState> humanCapsule() const override { return std::nullopt; }
};

// A sphere of `radius` at the origin, added to `world`.
ShapeId addSphere(PhysicsWorld& world, float radius) {
    ShapeRecord shape = makeShape(ShapeType::Sphere);
    std::get<Sphere>(shape.posed).radius = radius;
    return world.addShapeRecord(shape);
}

} // namespace

// ---- Unit 7: bodies and the sort-and-sweep world ----

TEST_CASE("a body inserted from the head goes after every body of equal min x") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    Tags tags;
    Bodies b;
    b.add("A", 0, 0.5F, tags(0));
    b.add("B", 1, 1.5F, tags(1));
    b.add("C", 1, 1.5F, tags(2));
    b.add("D", 2, 2.5F, tags(3));
    CHECK(b.order() == "ABCD");
    b.add("E", 1, 1.5F, tags(4));
    CHECK(b.order() == "ABCED");
    CHECK(b.world.bound() == Approx(0.5).margin(kEps));
}

TEST_CASE("a reinserted body walks forward past its equals, or lands before the equals it passes going back") {
    // Expected: physics-bodies.md#u7-reinsert, confirmed (code)
    Tags tags;
    Bodies b;
    b.add("A", 0, 0.5F, tags(0));
    const BodyId bb = b.add("B", 1, 1.5F, tags(1));
    b.add("C", 1, 1.5F, tags(2));
    b.add("E", 1, 1.5F, tags(3));
    b.add("D", 2, 2.5F, tags(4));
    b.world.reinsert(bb);
    CHECK(b.order() == "ACEBD");

    Bodies back;
    back.add("A", 0, 0.5F, tags(5));
    back.add("B", 1, 1.5F, tags(6));
    back.add("C", 1, 1.5F, tags(7));
    const BodyId d = back.add("D", 2, 2.5F, tags(8));
    back.move(d, 0, 0.5F);
    back.world.reinsert(d);
    CHECK(back.order() == "DABC");
    back.move(d, 1, 1.5F);
    back.world.reinsert(d);
    CHECK(back.order() == "ABCD");
}

TEST_CASE("removing a body returns its old previous body, and the head's successor becomes the head") {
    // Expected: physics-bodies.md#u7-remove, confirmed (code)
    Tags tags;
    Bodies b;
    const BodyId a = b.add("A", 0, 1, tags(0));
    const BodyId bb = b.add("B", 1, 2, tags(1));
    b.add("C", 2, 3, tags(2));
    CHECK(b.world.remove(bb) == a);
    CHECK(b.order() == "AC");
    CHECK(b.world.body(bb).prev == kNoBody);
    CHECK(b.world.body(bb).next == kNoBody);

    Bodies h;
    const BodyId ha = h.add("A", 0, 1, tags(3));
    const BodyId hb = h.add("B", 1, 2, tags(4));
    h.add("C", 2, 3, tags(5));
    CHECK(h.world.remove(ha) == kNoBody);
    CHECK(h.world.head() == hb);
    CHECK(h.world.body(hb).prev == kNoBody);
}

TEST_CASE("removing a body from the world moves the last body into its place in the vector") {
    // Expected: physics-bodies.md#u7-addremove, confirmed (code)
    Tags tags;
    Bodies b;
    const BodyId a = b.add("A", 0, 1, tags(0));
    const BodyId bb = b.add("B", 1, 2, tags(1));
    const BodyId c = b.add("C", 2, 3, tags(2));
    CHECK(b.world.body(c).vectorIndex == 2);
    b.world.removeBody(a);
    CHECK(b.world.bodyVector() == std::vector<BodyId>{c, bb});
    CHECK(b.world.body(c).vectorIndex == 0);
    CHECK(b.world.body(a).vectorIndex == 0);
    CHECK(b.order() == "BC");
}

TEST_CASE("a box query returns the backward hits from its start in reverse order, then the forward hits") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    SixBodies six;
    CHECK(six.b.world.bound() == Approx(10.0).margin(kEps));
    std::vector<BodyId> found;
    CHECK(six.b.world.queryBox(found, boxX(8, 9), six.s, six.tags(3), kAnyType, 0) == 2);
    CHECK(six.b.named(found) == "WR");

    found.clear();
    std::ignore = six.b.world.queryBox(found, boxX(8, 9), kNoBody, six.tags(3), kAnyType, 0);
    CHECK(six.b.named(found) == "WR");

    found.clear();
    std::ignore = six.b.world.queryBox(found, boxX(8, 12), six.s, kNoOwner, kAnyType, 0);
    CHECK(six.b.named(found) == "SWRT");
}

TEST_CASE("the backward walk keeps bodies past the box's upper x") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    SixBodies six;
    std::vector<BodyId> found;
    std::ignore = six.b.world.queryBox(found, boxX(0, 0.2F), six.s, six.tags(3), kAnyType, 0);
    CHECK(six.b.named(found) == "QPW");
}

TEST_CASE("a stale bound stops the backward walk") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    Tags tags;
    Bodies b;
    b.add("W", 0, 10, tags(0));
    b.add("Q", 6, 7, tags(1));
    const BodyId s = b.add("S", 8, 9, tags(2));
    std::vector<BodyId> found;
    b.world.setBound(1.0F);
    std::ignore = b.world.queryBox(found, boxX(8, 9), s, tags(2), kAnyType, 0);
    CHECK(found.empty());
    b.world.setBound(10.0F);
    std::ignore = b.world.queryBox(found, boxX(8, 9), s, tags(2), kAnyType, 0);
    CHECK(b.named(found) == "W");
}

TEST_CASE("a box query drops bodies by owner, type mask and flag mask, and finds touching boxes") {
    // Expected: physics-bodies.md#u7-querybox, confirmed (code)
    Tags tags;
    Bodies b;
    const BodyId typeless = b.add("N", 0, 1, tags(0));
    b.world.body(typeless).typeMask = 0;
    const BodyId flagged = b.add("F", 0, 1, tags(1));
    b.world.body(flagged).flags = 0x4;
    const BodyId plain = b.add("P", 0, 1, tags(2));
    b.world.body(plain).flags = 0;
    const BodyId ownerless = b.add("O", 0, 1, kNoOwner);
    b.world.body(ownerless).typeMask = 1;
    const BodyId touching = b.add("T", 0, 1, tags(3));
    b.world.body(touching).flags = 0;
    b.world.body(touching).box.min.y = 2.0F;
    b.world.body(touching).box.max.y = 3.0F;

    std::vector<BodyId> found;
    std::ignore = b.world.queryBox(found, boxX(0, 1), kNoBody, kNoOwner, kAnyType, 0);
    CHECK(b.named(found) == "FP");

    found.clear();
    std::ignore = b.world.queryBox(found, boxX(0, 1), kNoBody, kNoOwner, 0, 0);
    CHECK(found.empty());

    found.clear();
    std::ignore = b.world.queryBox(found, boxX(0, 1), kNoBody, kNoOwner, kAnyType, 0x4);
    CHECK(b.named(found) == "F");

    found.clear();
    std::ignore = b.world.queryBox(found, boxX(0, 1), kNoBody, tags(9), kAnyType, 0);
    CHECK(b.named(found) == "FPO");

    found.clear();
    Aabb up = boxX(0, 1);
    up.min.y = 1.0F;
    up.max.y = 2.0F;
    std::ignore = b.world.queryBox(found, up, kNoBody, kNoOwner, kAnyType, 0);
    CHECK(b.named(found) == "FPT");
}

TEST_CASE("the filtered query drops by flags, owner state and the keep test, asking every candidate's state") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    SixBodies six;
    PhysicsWorld& world = six.b.world;
    int stateCalls = 0;
    BodyFilter filter;
    filter.ignored = six.tags(3);
    filter.stateOf = [&](OwnerId owner) -> std::uint64_t {
        ++stateCalls;
        return owner == six.tags(0) ? 0x4000000U : 0U;
    };
    std::vector<BodyId> found;
    std::ignore = world.queryBoxFiltered(found, six.s, boxX(8, 9), six.s, filter);
    CHECK(six.b.named(found) == "WR");
    CHECK(stateCalls == 2);

    world.body(six.r).flags = 0x200;
    filter.excludedFlags = 0x200;
    found.clear();
    std::ignore = world.queryBoxFiltered(found, six.s, boxX(8, 9), six.s, filter);
    CHECK(six.b.named(found) == "W");

    filter.excludedFlags = 0;
    filter.excludedState = 0x4000000;
    found.clear();
    std::ignore = world.queryBoxFiltered(found, six.s, boxX(8, 9), six.s, filter);
    CHECK(six.b.named(found) == "R");

    filter.excludedState = 0;
    const OwnerId refused = six.tags(0);
    OwnerId asker = kNoOwner;
    filter.keep = [&](OwnerId mine, OwnerId theirs) {
        asker = mine;
        return theirs != refused;
    };
    found.clear();
    std::ignore = world.queryBoxFiltered(found, six.s, boxX(8, 9), six.s, filter);
    CHECK(six.b.named(found) == "R");
    CHECK(asker == six.tags(3));
}

TEST_CASE("the bound only grows between refreshes, and is recomputed on the step's 120th tick") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    Tags tags;
    Bodies b;
    const BodyId a = b.add("A", 0, 4, tags(0));
    b.add("B", 5, 6, tags(1));
    CHECK(b.world.bound() == Approx(4.0).margin(kEps));
    b.move(a, 0, 1);
    b.world.reinsert(a);
    CHECK(b.world.bound() == Approx(4.0).margin(kEps));
    b.world.refreshBound();
    CHECK(b.world.tickCounter() == 1);
    for (int step = 2; step <= 119; ++step) {
        b.world.refreshBound();
    }
    CHECK(b.world.tickCounter() == 119);
    CHECK(b.world.bound() == Approx(4.0).margin(kEps));
    b.world.refreshBound();
    CHECK(b.world.tickCounter() == 0);
    CHECK(b.world.bound() == Approx(1.0).margin(kEps));
    b.world.refreshBound();
    CHECK(b.world.tickCounter() == 1);
    CHECK(b.world.bound() == Approx(1.0).margin(kEps));
}

TEST_CASE("the overlap query tests every shape of each candidate while its max is not zero, and reads no other bit") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    int calls = 0;
    world.overlapTable().at(2).at(2) = [&](Contact& /*contact*/, const Shape& /*own*/, const Shape& /*other*/) {
        ++calls;
        return true;
    };
    // The querying body and one candidate with two spheres, both at the origin.
    const BodyId me = world.createBodyForOwner(tags(0), 1);
    const ShapeId mine = addSphere(world, 0.5F);
    world.addShape(me, mine);
    world.computeBounds(world.body(me).box, mine);
    world.addBody(me);
    const BodyId other = world.createBodyForOwner(tags(1), 1);
    world.addShape(other, addSphere(world, 0.5F));
    world.addShape(other, addSphere(world, 0.5F));
    world.computeBounds(world.body(other).box, world.body(other).firstShape);
    world.addBody(other);

    OverlapQuery q;
    q.body = me;
    q.shape = mine;
    q.options = 0x12;
    q.typeMask = kAnyType;
    std::vector<Contact> out;
    CHECK(world.overlapShape(out, q) == 0);
    CHECK(calls == 0);

    q.max = 8;
    CHECK(world.overlapShape(out, q) == 2);
    CHECK(calls == 2);
    CHECK(out.size() == 2);
    CHECK(out[0].halves[0].ownerId == tags(0));
    CHECK(out[0].halves[1].ownerId == tags(1));
    CHECK_FALSE(out[0].triangle.has_value());

    out.clear();
    q.options = 0x2012;
    CHECK(world.overlapShape(out, q) == 2);

    out.clear();
    q.options = 0x12 | kOverlapSkipGroup;
    world.body(me).group = 7;
    world.body(other).group = 7;
    CHECK(world.overlapShape(out, q) == 0);
    world.body(other).group = 0;
    CHECK(world.overlapShape(out, q) == 2);
}

TEST_CASE("the overlap query hands the mesh the zero box when it was given a box") {
    // Expected: physics-bodies.md#u7-overlapshape, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    std::optional<Aabb> meshBox;
    world.meshOverlap(ShapeType::Sphere) = [&](float /*low*/, float /*high*/, std::vector<Contact>& out,
                                               std::size_t room, const Aabb& box, BodyId /*body*/) -> std::size_t {
        meshBox = box;
        CHECK(room == 3);
        out.emplace_back();
        return 1;
    };
    const BodyId me = world.createBodyForOwner(tags(0), 1);
    const ShapeId mine = addSphere(world, 0.5F);
    std::get<Sphere>(world.shape(mine).posed).centre = Vec3{4.0F, 4.0F, 4.0F};
    world.addShape(me, mine);
    world.addBody(me);

    OverlapQuery q;
    q.body = me;
    q.shape = mine;
    q.options = kOverlapMesh;
    q.max = 3;
    std::vector<Contact> out;
    CHECK(world.overlapShape(out, q) == 1);
    REQUIRE(meshBox.has_value());
    const Aabb posedBox = meshBox.value_or(Aabb{});
    CHECK(posedBox.min.x == Approx(3.5).margin(kEps));
    CHECK(posedBox.max.z == Approx(4.5).margin(kEps));

    q.options = kOverlapMesh | kOverlapGivenBox;
    q.box = boxX(10, 20);
    out.clear();
    CHECK(world.overlapShape(out, q) == 1);
    REQUIRE(meshBox.has_value());
    const Aabb zeroBox = meshBox.value_or(boxX(1, 2));
    CHECK(zeroBox.min.x == Approx(0.0).margin(kEps));
    CHECK(zeroBox.max.x == Approx(0.0).margin(kEps));
}

TEST_CASE("the update sweep's box is the new bound grown forward by the shape's move") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    Tags tags;
    Bodies b;
    PlacedOwner owner;
    const BodyId mover = b.world.createBodyForOwner(tags(0), 1);
    b.world.addShape(mover, addSphere(b.world, 0.35F));
    b.world.body(mover).flags = kBodyFlagSweepBodies | kBodyFlagAlwaysUpdate;
    std::ignore = b.world.updateBody(mover, owner);
    owner.at = Vec3{1.0F, 0.0F, 0.0F};
    const std::optional<UpdateSweep> moved = b.world.updateBody(mover, owner);
    REQUIRE(moved.has_value());
    const UpdateSweep sweep = moved.value_or(UpdateSweep{});
    CHECK(sweep.box.min.x == Approx(0.65).margin(kEps));
    CHECK(sweep.box.max.x == Approx(2.35).margin(kEps));
    CHECK(sweep.move.x == Approx(1.0).margin(kEps));

    const BodyId behind = b.add("B", 0.1F, 0.4F, tags(1));
    const BodyId ahead = b.add("A", 2.0F, 2.2F, tags(2));
    b.world.body(behind).box.min.z = -1.0F;
    b.world.body(ahead).box.min.z = -1.0F;
    std::vector<BodyId> found;
    std::ignore = b.world.queryBox(found, sweep.box, mover, tags(0), kAnyType, 0);
    CHECK(b.named(found) == "A");
}

TEST_CASE("a body is posed and re-sorted only while dirty or always updated") {
    // Expected: physics-bodies.md#u7-tests, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    PlacedOwner owner;
    owner.at = Vec3{5.0F, 0.0F, 0.0F};
    const BodyId idle = world.createBodyForOwner(tags(0), 1);
    const ShapeId idleShape = addSphere(world, 0.5F);
    world.addShape(idle, idleShape);
    world.body(idle).flags = 0;
    CHECK_FALSE(world.updateBody(idle, owner).has_value());
    CHECK(std::get<Sphere>(world.shape(idleShape).posed).centre.x == Approx(0.0).margin(kEps));
    CHECK(world.head() == kNoBody);

    const BodyId always = world.createBodyForOwner(tags(1), 1);
    const ShapeId alwaysShape = addSphere(world, 0.5F);
    world.addShape(always, alwaysShape);
    world.body(always).flags = kBodyFlagAlwaysUpdate;
    std::ignore = world.updateBody(always, owner);
    CHECK(std::get<Sphere>(world.shape(alwaysShape).posed).centre.x == Approx(5.0).margin(kEps));
    CHECK(world.head() == always);
    CHECK(world.body(always).box.min.x == Approx(4.5).margin(kEps));
    owner.at = Vec3{6.0F, 0.0F, 0.0F};
    std::ignore = world.updateBody(always, owner);
    CHECK(world.body(always).box.min.x == Approx(5.5).margin(kEps));
    CHECK(world.body(always).flags == kBodyFlagAlwaysUpdate);

    const BodyId dirty = world.createBodyForOwner(tags(2), 1);
    const ShapeId dirtyShape = addSphere(world, 0.5F);
    world.addShape(dirty, dirtyShape);
    world.body(dirty).flags = kBodyFlagDirty;
    std::ignore = world.updateBody(dirty, owner);
    CHECK(world.body(dirty).flags == 0);
    CHECK(std::get<Sphere>(world.shape(dirtyShape).posed).centre.x == Approx(6.0).margin(kEps));
    owner.at = Vec3{7.0F, 0.0F, 0.0F};
    std::ignore = world.updateBody(dirty, owner);
    CHECK(std::get<Sphere>(world.shape(dirtyShape).posed).centre.x == Approx(6.0).margin(kEps));
}

TEST_CASE("every shape of a body is posed and bounded, and a moved body takes its first shape's bound alone") {
    // Expected: physics-bodies.md#u7-moved, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    PlacedOwner owner;
    const BodyId id = world.createBodyForOwner(tags(0), 1);
    const ShapeId first = addSphere(world, 0.5F);
    ShapeRecord far = makeShape(ShapeType::Sphere);
    far.offset = Vec3{3.0F, 0.0F, 0.0F};
    far.flags = 0;
    std::get<Sphere>(far.posed).radius = 0.5F;
    const ShapeId second = world.addShapeRecord(far);
    world.addShape(id, first);
    world.addShape(id, second);
    CHECK(world.shape(first).next == second);
    std::ignore = world.updateBody(id, owner);
    CHECK(world.body(id).box.min.x == Approx(-0.5).margin(kEps));
    CHECK(world.body(id).box.max.x == Approx(3.5).margin(kEps));

    world.bodyMoved(id, Mat34{.t = {10.0F, 0.0F, 0.0F}});
    CHECK(std::get<Sphere>(world.shape(first).posed).centre.x == Approx(10.0).margin(kEps));
    CHECK(world.body(id).box.min.x == Approx(9.5).margin(kEps));
    CHECK(world.body(id).box.max.x == Approx(10.5).margin(kEps));
}

TEST_CASE("a body built for an owner takes its type mask, a null owner leaves it") {
    // Expected: physics-bodies.md#u7-construct, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    const BodyId owned = world.createBodyForOwner(tags(0), 0x40);
    CHECK(world.body(owned).typeMask == 0x40);
    CHECK(world.body(owned).flags == 0x80000400U);
    CHECK(world.body(owned).owner == tags(0));
    world.setOwner(owned, kNoOwner, 0x8);
    CHECK(world.body(owned).owner == kNoOwner);
    CHECK(world.body(owned).typeMask == 0x40);
    world.setOwner(owned, tags(1), 0x8);
    CHECK(world.body(owned).typeMask == 0x8);
}

// ---- Unit 8: ignore lists and shape pools ----

TEST_CASE("sixty ignores take the slots in order and the original's sixty-first changes nothing") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    Tags tags;
    PhysicsWorld world(EngineLimits::Original);
    const BodyId id = world.createBodyForOwner(tags(0), 1);
    for (std::size_t i = 0; i < 60; ++i) {
        world.ignore(id, static_cast<OwnerId>(100 + i));
    }
    std::vector<IgnoreSlotId> chain;
    for (IgnoreSlotId s = world.body(id).ignoreChain; s != kNoIgnoreSlot; s = world.ignoreSlot(s).next) {
        chain.push_back(s);
    }
    REQUIRE(chain.size() == 60);
    CHECK(chain.front() == 59);
    CHECK(chain.back() == 0);
    world.ignore(id, static_cast<OwnerId>(100 + 60));
    CHECK(world.body(id).ignoreChain == 59);
    CHECK(world.ignoreSlotCount() == 60);
    CHECK(world.findFreeIgnoreSlot() == kNoIgnoreSlot);
    CHECK_FALSE(world.isIgnoring(id, static_cast<OwnerId>(100 + 60)));

    // Past the original's sixty Coney adds a slot.
    PhysicsWorld coney(EngineLimits::Coney);
    const BodyId cid = coney.createBodyForOwner(tags(0), 1);
    for (std::size_t i = 0; i < 61; ++i) {
        coney.ignore(cid, static_cast<OwnerId>(100 + i));
    }
    CHECK(coney.ignoreSlotCount() == 61);
    CHECK(coney.body(cid).ignoreChain == 60);
}

TEST_CASE("a freed ignore slot is the next one taken, at the chain's front") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    const BodyId id = world.createBodyForOwner(tags(0), 1);
    for (std::size_t i = 1; i <= 8; ++i) {
        world.ignore(id, tags(i));
    }
    world.stopIgnoring(id, 5);
    CHECK(world.ignoreSlot(5).owner == kNoOwner);
    CHECK_FALSE(world.isIgnoring(id, tags(6)));
    world.ignore(id, tags(9));
    CHECK(world.body(id).ignoreChain == 5);
    CHECK(world.isIgnoring(id, tags(9)));
}

TEST_CASE("ignoring an owner twice takes two slots and removing that owner's body frees both") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    Tags tags;
    Bodies b;
    const BodyId x = b.add("X", 0, 1, tags(0));
    const BodyId y = b.add("Y", 0, 1, tags(1));
    const BodyId z = b.add("Z", 0, 1, tags(2));
    b.world.ignore(y, tags(0));
    b.world.ignore(y, tags(0));
    b.world.ignore(z, tags(0));
    b.world.ignore(y, tags(2));
    CHECK(b.world.isIgnoring(y, tags(0)));
    b.world.removeBody(x);
    CHECK_FALSE(b.world.isIgnoring(y, tags(0)));
    CHECK_FALSE(b.world.isIgnoring(z, tags(0)));
    CHECK(b.world.isIgnoring(y, tags(2)));
    CHECK(b.world.ignoreSlot(0).owner == kNoOwner);
    CHECK(b.world.ignoreSlot(1).owner == kNoOwner);
    CHECK(b.world.ignoreSlot(2).owner == kNoOwner);
    CHECK(b.world.ignoreSlot(3).owner == tags(2));
}

TEST_CASE("a null owner takes no slot on a body, and stands for the level on a human body") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    const BodyId plain = world.createBodyForOwner(tags(0), 1);
    world.ignore(plain, kNoOwner);
    CHECK(world.body(plain).ignoreChain == kNoIgnoreSlot);
    CHECK_FALSE(world.isIgnoring(plain, kNoOwner));

    const BodyId human = world.createBodyForOwner(tags(1), 0x40);
    world.body(human).human = true;
    world.ignore(human, kNoOwner);
    CHECK(world.body(human).ignoresLevel);
    CHECK(world.isIgnoring(human, kNoOwner));
    world.ignore(human, tags(2));
    CHECK(world.isIgnoring(human, tags(2)));
    world.stopIgnoring(human, kNoIgnoreSlot);
    CHECK_FALSE(world.body(human).ignoresLevel);
    CHECK_FALSE(world.isIgnoring(human, kNoOwner));
    CHECK(world.body(human).ignoreChain == kNoIgnoreSlot);
    CHECK(world.ignoreSlot(0).owner == kNoOwner);
}

TEST_CASE("stopping a slot from another body's chain zeroes the slot and leaves this chain alone") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    const BodyId a = world.createBodyForOwner(tags(0), 1);
    const BodyId b = world.createBodyForOwner(tags(1), 1);
    world.ignore(a, tags(2));
    world.ignore(b, tags(3));
    world.stopIgnoring(a, 1);
    CHECK(world.body(a).ignoreChain == 0);
    CHECK(world.ignoreSlot(0).owner == tags(2));
    CHECK(world.ignoreSlot(1).owner == kNoOwner);
    CHECK(world.ignoreSlot(1).body == kNoBody);
    CHECK(world.body(b).ignoreChain == 1);
}

TEST_CASE("removing a body frees the slots naming its owner in other bodies' chains and keeps the rest") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    Tags tags;
    Bodies b;
    const BodyId x = b.add("X", 0, 1, tags(0));
    const BodyId y = b.add("Y", 0, 1, tags(1));
    const BodyId z = b.add("Z", 0, 1, tags(2));
    b.world.ignore(y, tags(0));
    b.world.ignore(z, tags(0));
    b.world.ignore(y, tags(2));
    b.world.forgetIgnoredOwner(x);
    CHECK_FALSE(b.world.isIgnoring(y, tags(0)));
    CHECK_FALSE(b.world.isIgnoring(z, tags(0)));
    CHECK(b.world.isIgnoring(y, tags(2)));
    CHECK(b.world.body(y).ignoreChain == 2);
    CHECK(b.world.body(z).ignoreChain == kNoIgnoreSlot);
}

TEST_CASE("a human body always passes through its owner's grab partner") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    Tags tags;
    PhysicsWorld world;
    const BodyId human = world.createBodyForOwner(tags(0), 0x40);
    world.body(human).human = true;
    CHECK(world.isIgnoring(human, tags(1), tags(1)));
    CHECK_FALSE(world.isIgnoring(human, tags(1), kNoOwner));
    const BodyId plain = world.createBodyForOwner(tags(2), 1);
    CHECK_FALSE(world.isIgnoring(plain, tags(1), tags(1)));
}

TEST_CASE("the shape pools take the first free shape and the original's refuse one more") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    PhysicsWorld world(EngineLimits::Original);
    std::vector<ShapeId> taken;
    for (int i = 0; i < 416; ++i) {
        const ShapeId id = world.allocObjectSphere();
        REQUIRE(id != kNoShape);
        taken.push_back(id);
    }
    CHECK(world.allocObjectSphere() == kNoShape);
    CHECK(world.shape(taken[2]).flags == 0xb);
    world.freeShape(taken[2]);
    CHECK(world.shape(taken[2]).flags == 0x3);
    CHECK(world.allocObjectSphere() == taken[2]);
    CHECK(world.shape(taken[2]).flags == 0xb);
    CHECK(std::holds_alternative<Sphere>(world.shape(taken[2]).posed));

    for (int i = 0; i < 416; ++i) {
        REQUIRE(world.allocObjectBox() != kNoShape);
    }
    CHECK(world.allocObjectBox() == kNoShape);
    for (int i = 0; i < 36; ++i) {
        REQUIRE(world.allocCarBox() != kNoShape);
    }
    CHECK(world.allocCarBox() == kNoShape);

    const ShapeId big = world.allocBigSphere();
    REQUIRE(big != kNoShape);
    std::get<Sphere>(world.shape(big).posed).radius = 0.5F;
    world.freeShape(big);
    CHECK(world.allocBigSphere() == big);
    CHECK(std::get<Sphere>(world.shape(big).posed).radius == Approx(2.0).margin(kEps));
    for (int i = 1; i < 30; ++i) {
        REQUIRE(world.allocBigSphere() != kNoShape);
    }
    CHECK(world.allocBigSphere() == kNoShape);

    // Past the original's counts Coney builds one more shape.
    PhysicsWorld coney(EngineLimits::Coney);
    for (int i = 0; i < 36; ++i) {
        REQUIRE(coney.allocCarBox() != kNoShape);
    }
    const ShapeId extra = coney.allocCarBox();
    REQUIRE(extra != kNoShape);
    CHECK(coney.poolSize(ShapePool::CarBox) == 37);
    CHECK(coney.shape(extra).flags == 0xb);
    CHECK(std::holds_alternative<Box>(coney.shape(extra).posed));
}

TEST_CASE("a strike sphere stays free until it is switched on") {
    // Expected: physics-bodies.md#u8-tests, confirmed (code)
    PhysicsWorld world(EngineLimits::Original);
    const ShapeId first = world.findFreeStrikeSphere();
    REQUIRE(first != kNoShape);
    CHECK(world.shape(first).flags == kShapeFlag2);
    CHECK(world.findFreeStrikeSphere() == first);
    setEnabled(world.shape(first), true);
    const ShapeId second = world.findFreeStrikeSphere();
    CHECK(second != first);
    CHECK(second != kNoShape);
    for (std::size_t i = 0; i < world.poolSize(ShapePool::StrikeSphere); ++i) {
        setEnabled(world.shape(world.poolShape(ShapePool::StrikeSphere, i)), true);
    }
    CHECK(world.poolSize(ShapePool::StrikeSphere) == 60);
    CHECK(world.findFreeStrikeSphere() == kNoShape);
}
