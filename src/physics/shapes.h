// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>

#include "animation/anim_math.h"

// The four physics shapes as posed values in world space (what the shape-pair tests read of each shape record), the
// shape record that carries one with its head (offset, bone, flags, previous position), how each type is posed from
// its owner's transform and bone every update, and the world bound of a posed shape.
// Research: docs/research/physics.md#shapes, docs/research/physics.md#u4-the-shape-fields-read,
// docs/research/physics-bodies.md#unit-6

namespace coney::physics {

/// The shape types, as the original numbers them (shape `+0x30`); also the row and column of the pair tables.
enum class ShapeType : std::uint8_t { Box = 1, Sphere = 2, Capsule = 3, Segment = 4 };

/// An oriented box (type 1): half extents along its three unit axes, around its centre (shape `+0x40` half extents,
/// `+0x50`/`+0x60`/`+0x70` the axes, `+0x80` the centre).
struct Box {
    std::array<float, 3> half{};                                                                  ///< h0, h1, h2.
    std::array<anim::Vec3, 3> axes{{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}}; ///< A0, A1, A2.
    anim::Vec3 centre;                                                                            ///< P.
};

/// A sphere (type 2): shape `+0x40` radius, `+0x50` centre.
struct Sphere {
    anim::Vec3 centre;
    float radius = 0.0F;
};

/// An upright capsule (type 3): shape `+0x40` radius, `+0x44` height, `+0x50` base. Some tests treat it as a flat-ended
/// cylinder from base z to base z + height, others as a true capsule with rounded ends (each test says which).
struct Capsule {
    anim::Vec3 base;
    float radius = 0.0F;
    float height = 0.0F;
};

/// A segment (type 4): a sphere of `radius` swept from `start` to `end` (shape `+0x40`, `+0x50`, `+0x60`).
struct Segment {
    anim::Vec3 start;
    anim::Vec3 end;
    float radius = 0.0F;
};

/// Any of the four, for the pair tables.
using Shape = std::variant<Box, Sphere, Capsule, Segment>;

/// The type of `shape`.
[[nodiscard]] inline ShapeType typeOf(const Shape& shape) { return static_cast<ShapeType>(shape.index() + 1); }

/// The box of half extents `half` posed by `pose`: its axes are the pose's axes and its centre the pose's position
/// (how a car or an object sets a box's matrix directly, docs/research/physics.md#shapes).
[[nodiscard]] inline Box boxAt(const anim::Mat34& pose, anim::Vec3 half) {
    return Box{.half = {half.x, half.y, half.z}, .axes = {pose.x, pose.y, pose.z}, .centre = pose.t};
}

/// An axis-aligned box: the original's 0x20-byte `{min, max}` AABB (its two `w` words are not kept).
struct Aabb {
    anim::Vec3 min;
    anim::Vec3 max;
};

/// A position and rotation as an owner hands them to a pose (the original's 32-byte transform: the position, then
/// the quaternion `(x, y, z, w)`; docs/research/physics-bodies.md#u6-transforms).
struct Transform {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// Shape flags (shape `+0x32`).
inline constexpr std::uint16_t kShapeEnabled = 0x1;      ///< Switched on.
inline constexpr std::uint16_t kShapeFlag2 = 0x2;        ///< Set on every pool shape; its meaning is not known.
inline constexpr std::uint16_t kShapeStrikeTarget = 0x4; ///< A strike target.
inline constexpr std::uint16_t kShapeAllocated = 0x8;    ///< Taken from its pool.
inline constexpr std::uint16_t kShapeReset = 0x10;       ///< The next pose resets the previous position to the new one.

/// A shape's index in the physics world's shape store; kNoShape for none (the original's null shape pointer).
using ShapeId = std::uint32_t;
inline constexpr ShapeId kNoShape = std::numeric_limits<ShapeId>::max();

/// One shape record (docs/research/physics-bodies.md#u6-record): the common head and the posed shape of its type. A
/// new record holds what the original's constructors write (`PhysicsShape_Construct` `0x00342a30` and each type's);
/// the fields they leave unwritten start at zero in Coney.
struct ShapeRecord {
    ShapeId next = kNoShape; ///< `+0x00`: the next shape of the body.
    anim::Vec3 offset;       ///< `+0x10`: the local offset.
    /// `+0x20`: the previous position (sphere centre, box centre, capsule base, segment start), for sweeps.
    anim::Vec3 previous;
    std::int8_t bone = -1;               ///< `+0x31`: the bone, −1 for the body's own frame.
    std::uint16_t flags = kShapeEnabled; ///< `+0x32`: kShape* bits.
    Shape posed;                         ///< The type (`+0x30`) and its posed fields.
    float segmentLength = 0.0F;          ///< A segment's `+0x64`: its length along the bone's x axis.
};

/// A record of type `type` as its constructor leaves it: a box with identity axes at the origin, a sphere of radius 0,
/// a capsule and a segment of zero size; flags kShapeEnabled, bone −1, offset 0.
/// @coney-only the constructors' field defaults (0x00342a30 and each type's), as a value
[[nodiscard]] ShapeRecord makeShape(ShapeType type);

/// What a human owner tells its capsule's pose (docs/research/physics-bodies.md#u6-capsule-pose).
struct HumanCapsuleState {
    float scale = 1.0F;    ///< `Human_GetScale`: human `+0x65c`.
    bool raised = false;   ///< The byte human `+0x3be` is non-zero.
    bool jumping = false;  ///< State flag `0x400000000`.
    bool longFall = false; ///< State flag `0x1000000000`.
    bool cuffed = false;   ///< State flag `0x20000`.
    float takeoffZ = 0.0F; ///< Human `+0x560`: the height the jump left from.
};

/// What a shape's pose reads of the body's owner (the owner vtable slots of
/// docs/research/physics-bodies.md#u6-transforms).
class ShapeOwner {
  public:
    virtual ~ShapeOwner() = default;
    ShapeOwner() = default;
    ShapeOwner(const ShapeOwner&) = delete;
    ShapeOwner& operator=(const ShapeOwner&) = delete;
    ShapeOwner(ShapeOwner&&) = delete;
    ShapeOwner& operator=(ShapeOwner&&) = delete;

    /// `+0xa0`: the body transform the sphere and the box read (a world object's pose extrapolated to now).
    [[nodiscard]] virtual Transform transformNow() const = 0;
    /// `+0xa8`: the body transform the capsule and the segment read (a world object's stored pose).
    [[nodiscard]] virtual Transform transformStored() const = 0;
    /// `+0xb8`: bone `bone`'s transform in the body's frame (a human's position already scaled), or nothing when the
    /// owner has no model: the original then writes nothing into the caller's buffer.
    [[nodiscard]] virtual std::optional<Transform> boneTransform(int bone) const = 0;
    /// `+0x20` with `0x40` (a human): what its capsule's pose reads; nothing for an owner that is not a human.
    [[nodiscard]] virtual std::optional<HumanCapsuleState> humanCapsule() const = 0;
};

/// Switches a shape on (flags `|= 0x11`: enabled, and the next pose resets its previous position) or off (clears
/// only kShapeEnabled).
/// @orig 0x004f11c8 PhysicsShape_SetEnabled (unknown)
void setEnabled(ShapeRecord& shape, bool on);

/// Sizes a box from its full sizes: half extents `0.5 × size` per axis.
/// @orig 0x00342dc0 PhysicsBox_SetSize (unknown)
void setBoxSize(ShapeRecord& shape, float sx, float sy, float sz);

/// Sizes a capsule: `height` then `radius`, the original's argument order.
/// @orig 0x00343508 PhysShapeCapsule_SetSize (unknown)
void setCapsuleSize(ShapeRecord& shape, float height, float radius);

/// Poses a sphere at its offset on its bone (or in the body's frame for bone −1), with the flag 0x10 rule for its
/// previous centre (docs/research/physics-bodies.md#u6-reset).
/// @orig 0x00342b08 PhysicsSphere_Pose (unknown)
void poseSphere(ShapeRecord& shape, const ShapeOwner& owner);

/// Poses a box: its axes from the body's (and the bone's) rotation, its centre at the offset, with the flag 0x10
/// rule. Keeps the original's quirk: on a bone the offset is added to the bone's position without the bone's rotation.
/// @orig 0x00342df8 PhysicsBox_Pose (unknown)
void poseBox(ShapeRecord& shape, const ShapeOwner& owner);

/// Sets a box from a matrix (`m.x`, `m.y`, `m.z` the axes, `m.t` the position): previous = the old centre, always;
/// the centre is `m.t` plus the offset along the new axes. The flags are not touched.
/// @orig 0x00343040 PhysicsBox_SetMatrix (unknown)
void setBoxMatrix(ShapeRecord& shape, const anim::Mat34& m);

/// The eight corners of a box, corner `i` taking `+half` along axis 0 when `i & 1`, along axis 1 when `i & 2` is
/// **clear** (the original's odd sign), along axis 2 when `i & 4`, and `−half` otherwise.
/// @orig 0x003430e8 PhysicsBox_GetCorners (unknown)
[[nodiscard]] std::array<anim::Vec3, 8> boxCorners(const Box& box);

/// Poses a capsule. A human's stands at its position with its top 0.2 m × scale above the head bone, raised or
/// shortened by its state (`+0x3be`, jumping, cuffed); any other owner's at its stored position plus the unrotated
/// offset, its size unchanged. The flag 0x10 rule does not apply: the previous base is always the one before.
/// @orig 0x00343518 PhysicsCapsule_Pose (unknown)
void poseCapsule(ShapeRecord& shape, const ShapeOwner& owner);

/// Poses a segment from its bone along the bone's x axis (bone −1: from the body's position, the offset unused), with
/// the flag 0x10 rule for its start.
/// @orig 0x00343810 PhysShapeSegment_Pose (unknown)
void poseSegment(ShapeRecord& shape, const ShapeOwner& owner);

/// Poses `shape` by its type (the shape vtable's `+0x28` slot).
/// @coney-only the shape vtable's pose dispatch; each type's pose is a port
void poseShape(ShapeRecord& shape, const ShapeOwner& owner);

/// Sets a sphere from a matrix: its centre is `m.t`; the previous centre is not touched.
/// @orig 0x00342d08 PhysicsSphere_SetMatrix (unknown)
void setSphereMatrix(ShapeRecord& shape, const anim::Mat34& m);

/// Sets a capsule from a matrix: previous = the old base, then base = `m.t`.
/// @orig 0x00343798 PhysicsCapsule_SetMatrix (unknown)
void setCapsuleMatrix(ShapeRecord& shape, const anim::Mat34& m);

/// Sets `shape` from a matrix by its type (the shape vtable's `+0x30` slot); a segment's does nothing
/// (`0x00343a88`).
/// @coney-only the shape vtable's set-from-matrix dispatch; each type's is a port
void setShapeMatrix(ShapeRecord& shape, const anim::Mat34& m);

/// The position the shape's previous position follows (shape vtable `+0x20`): a sphere's centre, a box's centre, a
/// capsule's base, a segment's start.
/// @coney-only the shape vtable's position slot, a field read
[[nodiscard]] anim::Vec3 shapePosition(const ShapeRecord& shape);

/// The world bound of a posed shape. Keeps the original's sort, which leaves a segment whose end is below its start
/// on an axis with a wrong bound there: `[min(start − r, end + r), end + r]`. A capsule's has no radius below or
/// above.
/// @orig 0x00341190 PhysicsShape_GetBounds (unknown)
[[nodiscard]] Aabb shapeBounds(const ShapeRecord& shape);

/// The box of a sphere of radius `r` at `centre`, sorted so a negative radius still gives a valid box.
/// @orig 0x00341660 AABB_FromSphere (unknown)
[[nodiscard]] Aabb aabbFromSphere(anim::Vec3 centre, float r);

/// The box of an upright cylinder centred on `centre`, `height` tall, sorted like aabbFromSphere().
/// @orig 0x00341708 AABB_FromUprightCylinder (unknown)
[[nodiscard]] Aabb aabbFromUprightCylinder(anim::Vec3 centre, float height, float radius);

} // namespace coney::physics
