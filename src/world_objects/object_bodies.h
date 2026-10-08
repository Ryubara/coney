// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

// The physics bodies of the world objects in the world: a box or a sphere from each type's `CfgObj` centre, size and
// shape, at its object's pose, with the type's `PHYFLAG` layers saying who meets it. A walking human slides along the
// `BLOCKHUMANS` ones as along a wall, and strikes meet the `MELEETARGET` ones.
// Research: docs/research/physics.md#bodies, docs/research/physics.md#layers

namespace coney::world_objects {

/// `PHYFLAG` bits (`CfgObj` argument 12, type `+0x5e`): `BLOCKOBJECTS` (a flying object's sweep meets the body),
/// `BLOCKHUMANS` (a walking human's), `MELEETARGET` (strikes), `THROWNWEAPONTARGET` (thrown weapons).
inline constexpr int kPhyBlockObjects = 1;
inline constexpr int kPhyBlockHumans = 2;
inline constexpr int kPhyMeleeTarget = 4;
inline constexpr int kPhyThrownWeaponTarget = 256;

/// One world object's body in the world's axes.
struct ObjectBody {
    double handle = 0;   ///< The object's (its spawn record's) handle.
    int shape = 0;       ///< kBodyBox or kBodySphere.
    anim::Mat34 pose;    ///< The body's frame: the object's pose moved by the type's centre.
    anim::Vec3 half{};   ///< A box's half-extents (size / 2).
    float radius = 0.0F; ///< A sphere's radius (size.x / 2).
    int layers = 0;      ///< The type's `PHYFLAG` bits.
};

/// The body object `record` of `type` has: a box of half-extents size / 2 for shape 1 (`OBB`), a sphere of radius
/// size.x / 2 for shape 2 (`SPHERE`), each offset by the type's centre in the object's frame; nothing for any other
/// shape or a zero size.
/// @orig 0x00391d48 Obj_CreatePhysicsBody (unknown)
[[nodiscard]] std::optional<ObjectBody> bodyOf(const SpawnRecord& record, const ObjectType& type);

/// The push that moves a sphere at `centre` of `radius` out of `body` to just touch it, from the body's closest point
/// to the centre (for a centre inside a box, out through its nearest face); nothing when they do not overlap.
[[nodiscard]] std::optional<anim::Vec3> spherePush(const ObjectBody& body, anim::Vec3 centre, float radius);

/// The bodies of this step's world objects.
class ObjectBodies {
  public:
    /// Starts a new step's set.
    void clear() { m_bodies.clear(); }
    /// Adds one object's body.
    void add(const ObjectBody& body) { m_bodies.push_back(body); }
    /// Every body.
    [[nodiscard]] std::span<const ObjectBody> all() const { return m_bodies; }

    /// The push out of the deepest body with any of `layers` a sphere at `centre` of `radius` overlaps, made
    /// horizontal (a walking body slides in the ground plane), skipping a body the `move` goes away from (`push ·
    /// move` > 0 with a non-zero move); nothing when none is met.
    [[nodiscard]] std::optional<anim::Vec3> pushOut(anim::Vec3 centre, float radius, int layers, anim::Vec3 move) const;

    /// The handles of the bodies with any of `layers` a sphere at `centre` of `radius` touches.
    [[nodiscard]] std::vector<double> touching(anim::Vec3 centre, float radius, int layers) const;

  private:
    std::vector<ObjectBody> m_bodies;
};

} // namespace coney::world_objects
