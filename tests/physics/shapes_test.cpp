// SPDX-License-Identifier: GPL-3.0-or-later
// The shape records, their poses and bounds (physics/shapes.h): docs/research/physics-bodies.md's Unit 6 test ideas,
// each shape posed from a made-up body and bone transform, with the numbers the research section computes.
#include "physics/shapes.h"

#include <array>
#include <cmath>
#include <optional>
#include <variant>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"

using Catch::Approx;
using coney::anim::Mat34;
using coney::anim::Quat;
using coney::anim::Vec3;
using namespace coney::physics;

namespace {

constexpr double kEps = 1e-5;
const float kHalfRoot2 = std::sqrt(0.5F);
// 90 degrees about z and about y, the research section's qz and qy.
const Quat kQz{0.0F, 0.0F, kHalfRoot2, kHalfRoot2};
const Quat kQy{0.0F, kHalfRoot2, 0.0F, kHalfRoot2};

// An owner whose transforms are set by the test: one body transform for both slots unless `stored` is set apart, one
// bone transform for every bone (none: the owner has no model), and a human's capsule state or none.
struct FakeOwner final : ShapeOwner {
    Transform now{.position = {10.0F, 5.0F, 0.0F}, .rotation = kQz};
    std::optional<Transform> stored;
    std::optional<Transform> bone = Transform{.position = {0.2F, 0.0F, 1.5F}, .rotation = kQy};
    std::optional<HumanCapsuleState> human;

    [[nodiscard]] Transform transformNow() const override { return now; }
    [[nodiscard]] Transform transformStored() const override { return stored.value_or(now); }
    [[nodiscard]] std::optional<Transform> boneTransform(int /*bone*/) const override { return bone; }
    [[nodiscard]] std::optional<HumanCapsuleState> humanCapsule() const override { return human; }
};

// Checks the three components of `v` against x, y, z.
void checkVec(Vec3 v, double x, double y, double z) {
    CHECK(v.x == Approx(x).margin(kEps));
    CHECK(v.y == Approx(y).margin(kEps));
    CHECK(v.z == Approx(z).margin(kEps));
}

// A shape of `type` with `offset` on bone `bone`.
ShapeRecord shapeOn(ShapeType type, Vec3 offset, int bone) {
    ShapeRecord shape = makeShape(type);
    shape.offset = offset;
    shape.bone = static_cast<std::int8_t>(bone);
    return shape;
}

// The capsule posed for a human with `state`, the head bone at `headZ`, the body at (0, 0, 10).
Capsule humanCapsule(const HumanCapsuleState& state, float headZ) {
    FakeOwner owner;
    owner.now = Transform{.position = {0.0F, 0.0F, 10.0F}, .rotation = {}};
    owner.bone = Transform{.position = {0.0F, 0.0F, headZ}, .rotation = {}};
    owner.human = state;
    ShapeRecord shape = makeShape(ShapeType::Capsule);
    setCapsuleSize(shape, 1.0F, 0.35F);
    poseCapsule(shape, owner);
    return std::get<Capsule>(shape.posed);
}

} // namespace

TEST_CASE("a sphere is posed at its offset in the body's frame or on its bone") {
    // Expected: physics-bodies.md#u6-tests, confirmed (code)
    FakeOwner owner;
    ShapeRecord own = shapeOn(ShapeType::Sphere, {1.0F, 0.0F, 0.5F}, -1);
    poseSphere(own, owner);
    checkVec(std::get<Sphere>(own.posed).centre, 10.0, 6.0, 0.5);

    ShapeRecord hand = shapeOn(ShapeType::Sphere, {0.08F, 0.0F, 0.0F}, 3);
    poseSphere(hand, owner);
    checkVec(std::get<Sphere>(hand.posed).centre, 10.0, 5.2, 1.42);
}

TEST_CASE("a segment runs from its bone along the bone's x axis, from the body's position without one") {
    // Expected: physics-bodies.md#u6-tests, confirmed (code)
    FakeOwner owner;
    ShapeRecord spine = shapeOn(ShapeType::Segment, {-0.07F, 0.0F, 0.05F}, 2);
    spine.segmentLength = 0.38F;
    poseSegment(spine, owner);
    checkVec(std::get<Segment>(spine.posed).start, 10.0, 5.25, 1.57);
    checkVec(std::get<Segment>(spine.posed).end, 10.0, 5.25, 1.19);

    ShapeRecord own = shapeOn(ShapeType::Segment, {3.0F, 2.0F, 1.0F}, -1);
    own.segmentLength = 0.38F;
    poseSegment(own, owner);
    checkVec(std::get<Segment>(own.posed).start, 10.0, 5.0, 0.0);
    checkVec(std::get<Segment>(own.posed).end, 10.0, 5.38, 0.0);
}

TEST_CASE("a box takes the body's axes, and on a bone adds its offset unrotated by the bone") {
    // Expected: physics-bodies.md#u6-tests, confirmed (code)
    FakeOwner owner;
    ShapeRecord own = shapeOn(ShapeType::Box, {1.0F, 0.0F, 0.0F}, -1);
    poseBox(own, owner);
    const Box& box = std::get<Box>(own.posed);
    checkVec(box.axes[0], 0.0, 1.0, 0.0);
    checkVec(box.axes[1], -1.0, 0.0, 0.0);
    checkVec(box.axes[2], 0.0, 0.0, 1.0);
    checkVec(box.centre, 10.0, 6.0, 0.0);

    ShapeRecord onBone = shapeOn(ShapeType::Box, {0.1F, 0.0F, 0.0F}, 4);
    poseBox(onBone, owner);
    const Box& boned = std::get<Box>(onBone.posed);
    checkVec(boned.axes[0], 0.0, 0.0, -1.0);
    checkVec(boned.axes[1], -1.0, 0.0, 0.0);
    checkVec(boned.axes[2], 0.0, 1.0, 0.0);
    checkVec(boned.centre, 10.0, 5.3, 1.5);
}

TEST_CASE("the first pose after a shape is switched on has no move, later poses keep the pose before") {
    // Expected: physics-bodies.md#u6-reset, confirmed (code)
    FakeOwner owner;
    owner.now = Transform{};
    ShapeRecord sphere = shapeOn(ShapeType::Sphere, {}, -1);
    poseSphere(sphere, owner);
    checkVec(std::get<Sphere>(sphere.posed).centre, 0.0, 0.0, 0.0);

    setEnabled(sphere, true);
    CHECK(sphere.flags == 0x11);
    owner.now.position = Vec3{1.0F, 0.0F, 0.0F};
    poseSphere(sphere, owner);
    checkVec(sphere.previous, 1.0, 0.0, 0.0);
    CHECK(sphere.flags == 0x01);

    owner.now.position = Vec3{2.0F, 0.0F, 0.0F};
    poseSphere(sphere, owner);
    checkVec(sphere.previous, 1.0, 0.0, 0.0);
    checkVec(std::get<Sphere>(sphere.posed).centre, 2.0, 0.0, 0.0);

    setEnabled(sphere, false);
    CHECK(sphere.flags == 0x00);
    setEnabled(sphere, true);
    owner.now.position = Vec3{3.0F, 0.0F, 0.0F};
    poseSphere(sphere, owner);
    checkVec(sphere.previous, 3.0, 0.0, 0.0);

    // The capsule (an owner that is not a human) keeps the base before, enabled or not.
    ShapeRecord capsule = shapeOn(ShapeType::Capsule, {}, -1);
    owner.now.position = Vec3{1.0F, 0.0F, 0.0F};
    poseCapsule(capsule, owner);
    setEnabled(capsule, true);
    owner.now.position = Vec3{2.0F, 0.0F, 0.0F};
    poseCapsule(capsule, owner);
    checkVec(capsule.previous, 1.0, 0.0, 0.0);
    CHECK((capsule.flags & kShapeReset) != 0);
}

TEST_CASE("a box set from a matrix keeps its old centre as the previous and puts its offset along the new axes") {
    // Expected: physics-bodies.md#u6-box-set-matrix, confirmed (code)
    ShapeRecord shape = shapeOn(ShapeType::Box, {0.0F, 0.0F, 0.5F}, -1);
    std::get<Box>(shape.posed).centre = Vec3{1.0F, 1.0F, 1.0F};
    const Mat34 m{.x = {0.0F, 1.0F, 0.0F}, .y = {-1.0F, 0.0F, 0.0F}, .z = {0.0F, 0.0F, 1.0F}, .t = {5.0F, 5.0F, 0.0F}};
    setBoxMatrix(shape, m);
    checkVec(shape.previous, 1.0, 1.0, 1.0);
    checkVec(std::get<Box>(shape.posed).centre, 5.0, 5.0, 0.5);
    checkVec(std::get<Box>(shape.posed).axes[1], -1.0, 0.0, 0.0);

    shape.offset = Vec3{1.0F, 0.0F, 0.0F};
    setBoxMatrix(shape, m);
    checkVec(shape.previous, 5.0, 5.0, 0.5);
    checkVec(std::get<Box>(shape.posed).centre, 5.0, 6.0, 0.0);
}

TEST_CASE("a sphere set from a matrix moves its centre only, a capsule keeps its old base as the previous") {
    // Expected: physics-bodies.md#u6-record, confirmed (code)
    const Mat34 m{.t = {4.0F, 5.0F, 6.0F}};
    ShapeRecord sphere = makeShape(ShapeType::Sphere);
    sphere.previous = Vec3{9.0F, 9.0F, 9.0F};
    setSphereMatrix(sphere, m);
    checkVec(std::get<Sphere>(sphere.posed).centre, 4.0, 5.0, 6.0);
    checkVec(sphere.previous, 9.0, 9.0, 9.0);

    ShapeRecord capsule = makeShape(ShapeType::Capsule);
    std::get<Capsule>(capsule.posed).base = Vec3{1.0F, 2.0F, 3.0F};
    setCapsuleMatrix(capsule, m);
    checkVec(capsule.previous, 1.0, 2.0, 3.0);
    checkVec(std::get<Capsule>(capsule.posed).base, 4.0, 5.0, 6.0);

    ShapeRecord segment = makeShape(ShapeType::Segment);
    setShapeMatrix(segment, m);
    checkVec(std::get<Segment>(segment.posed).start, 0.0, 0.0, 0.0);
}

TEST_CASE("a box's corners take the y axis's sign the other way round") {
    // Expected: physics-bodies.md#u6-box-corners, confirmed (code)
    ShapeRecord shape = makeShape(ShapeType::Box);
    setBoxSize(shape, 4.0F, 2.0F, 1.0F);
    Box& box = std::get<Box>(shape.posed);
    box.centre = Vec3{0.0F, 0.0F, 0.5F};
    const std::array<Vec3, 8> corners = boxCorners(box);
    const std::array<Vec3, 8> expected{
        {{-2, 1, 0}, {2, 1, 0}, {-2, -1, 0}, {2, -1, 0}, {-2, 1, 1}, {2, 1, 1}, {-2, -1, 1}, {2, -1, 1}}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        checkVec(corners.at(i), expected.at(i).x, expected.at(i).y, expected.at(i).z);
    }
}

TEST_CASE("a turned box's bound is the min and max of its eight corners") {
    // Expected: physics-bodies.md#u6-tests, confirmed (code)
    ShapeRecord shape = makeShape(ShapeType::Box);
    setBoxSize(shape, 4.0F, 2.0F, 1.0F);
    Box& box = std::get<Box>(shape.posed);
    box.centre = Vec3{0.0F, 0.0F, 0.5F};
    box.axes = {Vec3{kHalfRoot2, kHalfRoot2, 0.0F}, Vec3{-kHalfRoot2, kHalfRoot2, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}};
    const Aabb bound = shapeBounds(shape);
    checkVec(bound.min, -2.12132, -2.12132, 0.0);
    checkVec(bound.max, 2.12132, 2.12132, 1.0);
}

TEST_CASE("a sphere's and a capsule's bounds, the capsule's with no radius below or above") {
    // Expected: physics-bodies.md#u6-bounds, confirmed (code)
    ShapeRecord sphere = makeShape(ShapeType::Sphere);
    std::get<Sphere>(sphere.posed) = Sphere{.centre = {1.0F, 2.0F, 3.0F}, .radius = 0.5F};
    Aabb bound = shapeBounds(sphere);
    checkVec(bound.min, 0.5, 1.5, 2.5);
    checkVec(bound.max, 1.5, 2.5, 3.5);

    ShapeRecord capsule = makeShape(ShapeType::Capsule);
    std::get<Capsule>(capsule.posed) = Capsule{.base = {1.0F, 2.0F, 0.0F}, .radius = 0.35F, .height = 1.8F};
    bound = shapeBounds(capsule);
    checkVec(bound.min, 0.65, 1.65, 0.0);
    checkVec(bound.max, 1.35, 2.35, 1.8);
}

TEST_CASE("a segment pointing down gets the original's wrong bound on that axis") {
    // Expected: physics-bodies.md#u6-bounds, confirmed (code)
    ShapeRecord segment = makeShape(ShapeType::Segment);
    std::get<Segment>(segment.posed) =
        Segment{.start = {0.0F, 0.0F, 2.15F}, .end = {0.0F, 0.0F, 0.65F}, .radius = 0.35F};
    Aabb bound = shapeBounds(segment);
    checkVec(bound.min, -0.35, -0.35, 1.0);
    checkVec(bound.max, 0.35, 0.35, 1.0);

    std::get<Segment>(segment.posed) =
        Segment{.start = {0.0F, 0.0F, 0.65F}, .end = {0.0F, 0.0F, 2.15F}, .radius = 0.35F};
    bound = shapeBounds(segment);
    CHECK(bound.min.z == Approx(0.3).margin(kEps));
    CHECK(bound.max.z == Approx(2.5).margin(kEps));

    std::get<Segment>(segment.posed) = Segment{.start = {0.0F, 0.0F, 1.0F}, .end = {0.0F, 0.0F, 0.8F}, .radius = 0.35F};
    bound = shapeBounds(segment);
    CHECK(bound.min.z == Approx(0.65).margin(kEps));
    CHECK(bound.max.z == Approx(1.15).margin(kEps));
}

TEST_CASE("the sphere and upright cylinder boxes sort a negative size into a valid box") {
    // Expected: physics-bodies.md#u6-aabb, confirmed (code)
    Aabb box = aabbFromSphere({1.0F, 2.0F, 3.0F}, 0.5F);
    checkVec(box.min, 0.5, 1.5, 2.5);
    checkVec(box.max, 1.5, 2.5, 3.5);
    box = aabbFromSphere({1.0F, 2.0F, 3.0F}, -0.5F);
    checkVec(box.min, 0.5, 1.5, 2.5);
    checkVec(box.max, 1.5, 2.5, 3.5);

    box = aabbFromUprightCylinder({1.0F, 2.0F, 3.0F}, 2.0F, 0.8F);
    checkVec(box.min, 0.2, 1.2, 2.0);
    checkVec(box.max, 1.8, 2.8, 4.0);
}

TEST_CASE("a human's capsule height follows its head and its state") {
    // Expected: physics-bodies.md#u6-tests, confirmed (code)
    Capsule c = humanCapsule({}, 1.6F);
    CHECK(c.base.z == Approx(10.0).margin(kEps));
    CHECK(c.height == Approx(1.8).margin(kEps));
    CHECK(c.radius == Approx(0.35).margin(kEps));

    c = humanCapsule({.raised = true}, 1.6F);
    CHECK(c.base.z == Approx(10.61).margin(kEps));
    CHECK(c.height == Approx(1.19).margin(kEps));

    c = humanCapsule({.jumping = true, .takeoffZ = 9.8F}, 1.6F);
    CHECK(c.base.z == Approx(10.5).margin(kEps));
    CHECK(c.height == Approx(1.3).margin(kEps));

    c = humanCapsule({.jumping = true, .takeoffZ = 10.2F}, 1.6F);
    CHECK(c.base.z == Approx(10.7).margin(kEps));
    CHECK(c.height == Approx(1.1).margin(kEps));

    c = humanCapsule({.jumping = true, .longFall = true}, 1.6F);
    CHECK(c.height == Approx(1.8).margin(kEps));

    c = humanCapsule({.cuffed = true}, 1.6F);
    CHECK(c.base.z == Approx(10.0).margin(kEps));
    CHECK(c.height == Approx(0.35).margin(kEps));

    c = humanCapsule({.raised = true, .cuffed = true}, 1.6F);
    CHECK(c.height == Approx(1.19).margin(kEps));

    c = humanCapsule({}, -0.1F);
    CHECK(c.height == Approx(0.2).margin(kEps));

    c = humanCapsule({.scale = 0.97F}, 1.552F);
    CHECK(c.height == Approx(1.746).margin(kEps));
    c = humanCapsule({.scale = 0.97F, .cuffed = true}, 1.552F);
    CHECK(c.height == Approx(0.3395).margin(kEps));
    c = humanCapsule({.scale = 0.97F}, -0.1F);
    CHECK(c.height == Approx(0.194).margin(kEps));
}

TEST_CASE("a capsule whose owner is not a human stands at the stored position plus its offset, not rotated") {
    // Expected: physics-bodies.md#u6-tests, confirmed (code)
    FakeOwner owner;
    owner.now = Transform{.position = {3.0F, 4.0F, 5.0F}, .rotation = kQz};
    ShapeRecord shape = shapeOn(ShapeType::Capsule, {1.0F, 0.0F, 0.0F}, -1);
    setCapsuleSize(shape, 1.5F, 0.4F);
    poseCapsule(shape, owner);
    const Capsule& c = std::get<Capsule>(shape.posed);
    checkVec(c.base, 4.0, 4.0, 5.0);
    CHECK(c.height == Approx(1.5).margin(kEps));
    CHECK(c.radius == Approx(0.4).margin(kEps));
}
