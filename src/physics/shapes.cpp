// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/shapes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <variant>

#include "animation/anim_math.h"
#include "core/assert.h"

namespace coney::physics {

namespace {

using anim::add;
using anim::Mat34;
using anim::Quat;
using anim::scale;
using anim::Vec3;

// The height the capsule's top stands above the head bone, and its floor, both times the scale (0x3e4ccccd).
constexpr float kHeadClearance = 0.2F;
// What a raised human (`+0x3be`) lifts its base by (0x3f1c28f6).
constexpr float kRaisedLift = 0.61F;
// What a jumping human lifts its base by, and the most its base may stand above the take-off height (0x3f333333).
constexpr float kJumpLift = 0.7F;
// A cuffed human's capsule height (0x3eb33333).
constexpr float kCuffedHeight = 0.35F;
// The bone a human's capsule reads its top from: the head.
constexpr int kHeadBone = 6;

// The rows of Quat_ToMatrix(q), row i the image of axis i. Coney's choice: row 3, (0, 0, 0, 1), is not built.
// Research: docs/research/physics-bodies.md#u6-transforms. The quaternion is taken as it is, not normalised.
// @orig 0x00336500 Quat_ToMatrix (unknown)
std::array<Vec3, 3> rows(Quat q) {
    return {
        Vec3{1.0F - 2.0F * (q.y * q.y + q.z * q.z), 2.0F * (q.x * q.y + q.z * q.w), 2.0F * (q.x * q.z - q.y * q.w)},
        Vec3{2.0F * (q.x * q.y - q.z * q.w), 1.0F - 2.0F * (q.x * q.x + q.z * q.z), 2.0F * (q.y * q.z + q.x * q.w)},
        Vec3{2.0F * (q.x * q.z + q.y * q.w), 2.0F * (q.y * q.z - q.x * q.w), 1.0F - 2.0F * (q.x * q.x + q.y * q.y)}};
}

// A vector through a rotation's rows: `r0 v.x + r1 v.y + r2 v.z`, the VU0 product every matrix pose uses.
// @coney-only vector arithmetic the poses share
Vec3 apply(const std::array<Vec3, 3>& r, Vec3 v) {
    return add(add(scale(r[0], v.x), scale(r[1], v.y)), scale(r[2], v.z));
}

// The sphere's no-bone rotation, straight from the quaternion: `(w² − |u|²) v + 2 (u·v) u + 2 w (u × v)`.
// @orig 0x00342b08 PhysicsSphere_Pose (unknown)
Vec3 rotateDirect(Quat q, Vec3 v) {
    const Vec3 u{q.x, q.y, q.z};
    const float uv = anim::dot(u, v);
    return add(add(scale(v, q.w * q.w - anim::dot(u, u)), scale(u, 2.0F * uv)), scale(anim::cross(u, v), 2.0F * q.w));
}

// The bone's transform as a pose reads it: the owner's answer, or, when the owner has no model and writes nothing,
// the body transform the same buffer already holds (docs/research/physics-bodies.md#u6-transforms).
// @coney-only the poses' shared read of the owner's bone slot
Transform boneOrBody(const ShapeOwner& owner, int bone, const Transform& body) {
    return owner.boneTransform(bone).value_or(body);
}

// The flag 0x10 rule's first step: with kShapeReset clear, the previous position is the position before this pose.
// @orig 0x00342b08 PhysicsSphere_Pose (unknown)
// @orig 0x00342df8 PhysicsBox_Pose (unknown)
// @orig 0x00343810 PhysShapeSegment_Pose (unknown)
void keepPrevious(ShapeRecord& shape, Vec3 position) {
    if ((shape.flags & kShapeReset) == 0) {
        shape.previous = position;
    }
}

// The flag 0x10 rule's last step: with kShapeReset set, the previous position is the new one (no move); then the flag
// is cleared, always.
// @orig 0x00342b08 PhysicsSphere_Pose (unknown)
// @orig 0x00342df8 PhysicsBox_Pose (unknown)
// @orig 0x00343810 PhysShapeSegment_Pose (unknown)
void resetPrevious(ShapeRecord& shape, Vec3 position) {
    if ((shape.flags & kShapeReset) != 0) {
        shape.previous = position;
    }
    shape.flags = static_cast<std::uint16_t>(shape.flags & ~kShapeReset);
}

// The record's posed shape as `T`; a pose of the wrong type is a broken invariant.
// @coney-only the variant access of Coney's shape record
template <typename T> T& posedAs(ShapeRecord& shape) {
    T* posed = std::get_if<T>(&shape.posed);
    CONEY_ASSERT(posed != nullptr);
    return *posed;
}

// The per-axis min of two vectors.
// @coney-only vector arithmetic the bounds share
Vec3 minOf(Vec3 a, Vec3 b) { return Vec3{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
// The per-axis max of two vectors.
// @coney-only vector arithmetic the bounds share
Vec3 maxOf(Vec3 a, Vec3 b) { return Vec3{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

} // namespace

ShapeRecord makeShape(ShapeType type) {
    ShapeRecord shape;
    switch (type) {
    case ShapeType::Box:
        shape.posed = Box{};
        break;
    case ShapeType::Sphere:
        shape.posed = Sphere{};
        break;
    case ShapeType::Capsule:
        shape.posed = Capsule{};
        break;
    case ShapeType::Segment:
        shape.posed = Segment{};
        break;
    }
    return shape;
}

void setEnabled(ShapeRecord& shape, bool on) {
    if (on) {
        shape.flags = static_cast<std::uint16_t>(shape.flags | kShapeEnabled | kShapeReset);
        return;
    }
    shape.flags = static_cast<std::uint16_t>(shape.flags & ~kShapeEnabled);
}

void setBoxSize(ShapeRecord& shape, float sx, float sy, float sz) {
    posedAs<Box>(shape).half = {sx * 0.5F, sy * 0.5F, sz * 0.5F};
}

void setCapsuleSize(ShapeRecord& shape, float height, float radius) {
    Capsule& capsule = posedAs<Capsule>(shape);
    capsule.radius = radius;
    capsule.height = height;
}

void poseSphere(ShapeRecord& shape, const ShapeOwner& owner) {
    Sphere& sphere = posedAs<Sphere>(shape);
    keepPrevious(shape, sphere.centre);
    const Transform body = owner.transformNow();
    if (shape.bone == -1) {
        sphere.centre = add(body.position, rotateDirect(body.rotation, shape.offset));
    } else {
        // On a bone: the offset turned by the bone, from the bone's position, then into the world by the body.
        const Transform bone = boneOrBody(owner, shape.bone, body);
        const Vec3 local = add(bone.position, apply(rows(bone.rotation), shape.offset));
        sphere.centre = add(body.position, apply(rows(body.rotation), local));
    }
    resetPrevious(shape, sphere.centre);
}

void poseBox(ShapeRecord& shape, const ShapeOwner& owner) {
    Box& box = posedAs<Box>(shape);
    keepPrevious(shape, box.centre);
    const Transform body = owner.transformNow();
    const std::array<Vec3, 3> m = rows(body.rotation);
    if (shape.bone == -1) {
        box.axes = m;
        box.centre = add(body.position, apply(m, shape.offset));
    } else {
        // On a bone: the bone's axes turned by the body; the offset is added to the bone's position unturned by the
        // bone (the original's quirk at 0x00342f10).
        const Transform bone = boneOrBody(owner, shape.bone, body);
        const std::array<Vec3, 3> b = rows(bone.rotation);
        box.axes = {apply(m, b[0]), apply(m, b[1]), apply(m, b[2])};
        box.centre = add(body.position, apply(m, add(bone.position, shape.offset)));
    }
    resetPrevious(shape, box.centre);
}

void setBoxMatrix(ShapeRecord& shape, const Mat34& m) {
    Box& box = posedAs<Box>(shape);
    shape.previous = box.centre;
    box.axes = {m.x, m.y, m.z};
    box.centre = add(m.t, apply(box.axes, shape.offset));
}

std::array<Vec3, 8> boxCorners(const Box& box) {
    const Vec3 x = scale(box.axes[0], box.half[0]);
    const Vec3 y = scale(box.axes[1], box.half[1]);
    const Vec3 z = scale(box.axes[2], box.half[2]);
    std::array<Vec3, 8> corners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const float sx = (i & 1U) != 0 ? 1.0F : -1.0F;
        const float sy = (i & 2U) != 0 ? -1.0F : 1.0F; // the y sign runs the other way round
        const float sz = (i & 4U) != 0 ? 1.0F : -1.0F;
        corners.at(i) = add(add(add(box.centre, scale(x, sx)), scale(y, sy)), scale(z, sz));
    }
    return corners;
}

void poseCapsule(ShapeRecord& shape, const ShapeOwner& owner) {
    Capsule& capsule = posedAs<Capsule>(shape);
    const std::optional<HumanCapsuleState> human = owner.humanCapsule();
    const Transform body = owner.transformStored();
    shape.previous = capsule.base;
    if (!human.has_value()) {
        // Not a human: the stored position plus the offset, not turned; the size stays as set.
        capsule.base = add(body.position, shape.offset);
        return;
    }
    // A human stands at its position, the offset unused, with its top a little above its head bone. Coney's choice:
    // a human with no model writes no head bone and the original then reads a stale stack word; Coney takes 0.
    const float s = human->scale;
    capsule.base = body.position;
    const float headZ = owner.boneTransform(kHeadBone).value_or(Transform{}).position.z;
    capsule.height = headZ + s * kHeadClearance;
    if (human->raised) {
        capsule.base.z += s * kRaisedLift;
        capsule.height -= s * kRaisedLift;
    } else if (human->jumping && !human->longFall) {
        capsule.base.z += s * kJumpLift;
        capsule.height -= s * kJumpLift;
        // The base stands at most 0.7 m (not scaled) above the take-off height, the top kept where it was.
        if (capsule.base.z - human->takeoffZ > kJumpLift) {
            capsule.base.z = human->takeoffZ + kJumpLift;
            capsule.height = headZ + s * kHeadClearance + body.position.z - capsule.base.z;
        }
    } else if (human->cuffed) {
        capsule.height = s * kCuffedHeight;
    }
    if (capsule.height < s * kHeadClearance) {
        capsule.height = s * kHeadClearance;
    }
}

void poseSegment(ShapeRecord& shape, const ShapeOwner& owner) {
    Segment& segment = posedAs<Segment>(shape);
    keepPrevious(shape, segment.start);
    const Transform body = owner.transformStored();
    const std::array<Vec3, 3> m = rows(body.rotation);
    if (shape.bone == -1) {
        // In the body's frame: from its position along its x axis; the offset is not used.
        segment.start = body.position;
        segment.end = add(body.position, scale(m[0], shape.segmentLength));
    } else {
        // On a bone: from the bone's position plus its turned offset, along the bone's x axis, then into the world.
        const Transform bone = boneOrBody(owner, shape.bone, body);
        const std::array<Vec3, 3> b = rows(bone.rotation);
        const Vec3 a = add(bone.position, apply(b, shape.offset));
        const Vec3 e = add(a, scale(b[0], shape.segmentLength));
        segment.start = add(body.position, apply(m, a));
        segment.end = add(body.position, apply(m, e));
    }
    resetPrevious(shape, segment.start);
}

void poseShape(ShapeRecord& shape, const ShapeOwner& owner) {
    switch (typeOf(shape.posed)) {
    case ShapeType::Box:
        poseBox(shape, owner);
        break;
    case ShapeType::Sphere:
        poseSphere(shape, owner);
        break;
    case ShapeType::Capsule:
        poseCapsule(shape, owner);
        break;
    case ShapeType::Segment:
        poseSegment(shape, owner);
        break;
    }
}

void setSphereMatrix(ShapeRecord& shape, const Mat34& m) { posedAs<Sphere>(shape).centre = m.t; }

void setCapsuleMatrix(ShapeRecord& shape, const Mat34& m) {
    Capsule& capsule = posedAs<Capsule>(shape);
    shape.previous = capsule.base;
    capsule.base = m.t;
}

void setShapeMatrix(ShapeRecord& shape, const Mat34& m) {
    switch (typeOf(shape.posed)) {
    case ShapeType::Box:
        setBoxMatrix(shape, m);
        break;
    case ShapeType::Sphere:
        setSphereMatrix(shape, m);
        break;
    case ShapeType::Capsule:
        setCapsuleMatrix(shape, m);
        break;
    case ShapeType::Segment:
        break; // the segment's slot does nothing (0x00343a88)
    }
}

Vec3 shapePosition(const ShapeRecord& shape) {
    switch (typeOf(shape.posed)) {
    case ShapeType::Box:
        return std::get<Box>(shape.posed).centre;
    case ShapeType::Sphere:
        return std::get<Sphere>(shape.posed).centre;
    case ShapeType::Capsule:
        return std::get<Capsule>(shape.posed).base;
    case ShapeType::Segment:
        return std::get<Segment>(shape.posed).start;
    }
    return {};
}

Aabb shapeBounds(const ShapeRecord& shape) {
    Aabb box;
    if (const auto* sphere = std::get_if<Sphere>(&shape.posed)) {
        const float r = sphere->radius;
        box.min = Vec3{sphere->centre.x - r, sphere->centre.y - r, sphere->centre.z - r};
        box.max = Vec3{sphere->centre.x + r, sphere->centre.y + r, sphere->centre.z + r};
    } else if (const auto* capsule = std::get_if<Capsule>(&shape.posed)) {
        // The cylinder's box: no radius below the base or above the top.
        const float r = capsule->radius;
        box.min = Vec3{capsule->base.x - r, capsule->base.y - r, capsule->base.z};
        box.max = Vec3{capsule->base.x + r, capsule->base.y + r, capsule->base.z + capsule->height};
    } else if (const auto* segment = std::get_if<Segment>(&shape.posed)) {
        const float r = segment->radius;
        box.min = Vec3{segment->start.x - r, segment->start.y - r, segment->start.z - r};
        box.max = Vec3{segment->end.x + r, segment->end.y + r, segment->end.z + r};
    } else {
        // The box: per axis the smallest and largest of its corners, scanned from corner 0 with strict compares.
        const std::array<Vec3, 8> corners = boxCorners(std::get<Box>(shape.posed));
        box.min = corners[0];
        box.max = corners[0];
        for (std::size_t i = 1; i < corners.size(); ++i) {
            box.min = minOf(box.min, corners.at(i));
            box.max = maxOf(box.max, corners.at(i));
        }
    }
    // The original's sort: min against max, then max against the new min, which can never change max. A segment
    // pointing down an axis keeps a wrong bound there (docs/research/physics-bodies.md#u6-bounds).
    box.min = minOf(box.min, box.max);
    box.max = maxOf(box.min, box.max);
    return box;
}

Aabb aabbFromSphere(Vec3 centre, float r) {
    const Vec3 low{centre.x - r, centre.y - r, centre.z - r};
    const Vec3 high{centre.x + r, centre.y + r, centre.z + r};
    return Aabb{.min = minOf(low, high), .max = maxOf(low, high)};
}

Aabb aabbFromUprightCylinder(Vec3 centre, float height, float radius) {
    const float halfHeight = height * 0.5F;
    const Vec3 low{centre.x - radius, centre.y - radius, centre.z - halfHeight};
    const Vec3 high{centre.x + radius, centre.y + radius, centre.z + halfHeight};
    return Aabb{.min = minOf(low, high), .max = maxOf(low, high)};
}

} // namespace coney::physics
