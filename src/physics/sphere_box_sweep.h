// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/shapes.h"

// A sphere swept against an oriented box: the sweep table's (sphere, box) and (box, sphere) entries, which is how a
// strike's hand, head or foot sphere reaches a car's or an object's box. The start centre's region (inside, a face's,
// an edge's or a corner's) picks the step and the feature the contact names; the step finds the first touch.
// Research: docs/research/physics.md#unit-5

namespace coney::physics {

/// The box feature a sphere-box contact names (contact `+0x70`, or `+0x30` for the mirrored entry).
inline constexpr int kFeatureFace = 0;
inline constexpr int kFeatureCorner = 1;
inline constexpr int kFeatureEdge = 2;
inline constexpr int kFeatureInside = 4;

/// What the sphere-box sweep finds.
struct SphereBoxHit {
    float fraction = 0.0F; ///< Of the move, at first contact (0 for a start centre inside the box).
    anim::Vec3 point;      ///< The contact point in the world (the start centre when it starts inside).
    anim::Vec3 localPoint; ///< The point along the box's axes from its centre (the inside case: world z, a quirk).
    Vec4 featureNormal;    ///< The start region's outward normal in the world, from the original's tables.
    Vec4 directionToBox;   ///< Horizontal unit vector from the sphere's start centre to the box centre, w 1.
    int featureType = 0;   ///< kFeatureFace, kFeatureEdge, kFeatureCorner or kFeatureInside.
    int featureIndex = 0; ///< The face, edge or corner (docs/research/physics.md#u4b-sweep-sphereorientedbox-0034bbe8).
};

/// A sphere of `radius` whose centre starts at `centre` and moves by `sphereMove`, against `box` moving by `boxMove`.
/// A hit when the centre starts inside the box or on its surface (fraction 0, whatever the move), or when the swept
/// sphere first touches the box at a fraction ≤ `maxFraction`. A sphere that already touches the box with its centre
/// outside never hits. The normal is the start region's, not the touched feature's, and six region normals are the
/// original's wrong table entries (kept, docs/research/physics.md#the-normal-tables). Nothing on a miss.
/// @orig 0x0034bbe8 Sweep_SphereOrientedBox (unknown)
/// @orig 0x0034baf0 SphereBox_ToBoxSpace (unknown)
[[nodiscard]] std::optional<SphereBoxHit> sweepSphereOrientedBox(float radius, float maxFraction, const Box& box,
                                                                 anim::Vec3 boxMove, anim::Vec3 centre,
                                                                 anim::Vec3 sphereMove);

/// Sweeps `sphere` by `move` against the resting `box` (sweep table (2, 1)) and, on a hit, fills `contact` as a strike
/// hands it to `Strike_Contact` and `Car_OnHit`: `+0x10` the direction to the box, `+0x50` the feature normal, `+0x60`
/// the point in the box's frame, `+0x70`/`+0x74` the feature, `+0x80` the fraction, `+0x90` the world point (w 1) and
/// `+0xb0` cleared. Returns false and leaves `contact` untouched on a miss.
/// @orig 0x003453f8 PhysicsSweep_SphereBox (unknown)
bool sweepSphereBox(Contact& contact, const Sphere& sphere, const Box& box, anim::Vec3 move);

/// The mirror: `box` moves by `move` against the resting `sphere` (sweep table (1, 2)). The box's frame is its start
/// pose, so the world point is on the box's start pose. Fills the same values with the halves swapped (`+0x10` the
/// feature normal, `+0x20` the local point, `+0x30`/`+0x34` the feature, `+0x50` the direction to the box).
/// @orig 0x003454c0 PhysicsSweep_BoxSphere (unknown)
bool sweepBoxSphere(Contact& contact, const Box& box, const Sphere& sphere, anim::Vec3 move);

/// The smaller `t` with |q + t·m|² = `rr`, as C / (D − B) with C = |q|² − rr, B = q·m, D = sqrt(|B² − |m|²·C|). No
/// miss test: the callers screen misses first, and a negative discriminant is taken as its absolute value.
/// @orig 0x0034af50 Ray_SphereRoot (unknown)
[[nodiscard]] float raySphereRoot(anim::Vec3 q, anim::Vec3 m, float rr);

/// The 2D form of raySphereRoot(), with `a` (normally |m|²) passed in.
/// @orig 0x0034afd8 Ray_CircleRoot (unknown)
[[nodiscard]] float rayCircleRoot(float qx, float qy, float mx, float my, float a, float rr);

} // namespace coney::physics
