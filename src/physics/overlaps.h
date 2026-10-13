// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "physics/contact.h"
#include "physics/shapes.h"

// The static overlap of two shapes: the overlap table's pair tests (`own` the querying shape, `other` the shape met).
// Each returns whether they overlap; touching counts unless a test says otherwise. Most write nothing into the
// contact; the capsule-capsule and capsule-box tests (and the box-capsule mirror) write normals and a depth. Their
// callers are the push sphere, pick-ups and an object touched by a human; strikes use the sweeps instead.
// Research: docs/research/physics.md#unit-4

namespace coney::physics {

/// Two spheres: centres within the sum of the radii. Writes nothing.
/// @orig 0x00345d58 PhysicsOverlap_SphereSphere (unknown)
[[nodiscard]] bool overlapSphereSphere(Contact& contact, const Sphere& own, const Sphere& other);

/// Two oriented boxes by the 15-axis separating-axis test, touching boxes overlapping. Kept as the original builds
/// it: its rotation matrix is the wrong product with the centres mixed in, so it misses boxes that are rotated or far
/// from the origin (no caller pairs two boxes, so nothing in play shows it). Writes nothing.
/// @orig 0x00345dd8 PhysicsOverlap_BoxBox (unknown)
[[nodiscard]] bool overlapBoxBox(Contact& contact, const Box& own, const Box& other);

/// Two capsules as upright flat-ended cylinders: the height ranges overlap (touching counts) and the axes are closer
/// than the sum of the radii (strictly: side by side touching is not a hit). Writes `+0x10` the horizontal unit
/// vector from `own` to `other` ((0, 1, 0) for coincident axes), `+0x50` its opposite, `+0x88` the sum of the radii
/// less the horizontal distance.
/// @orig 0x00346388 PhysicsOverlap_CapsuleCapsule (unknown)
[[nodiscard]] bool overlapCapsuleCapsule(Contact& contact, const Capsule& own, const Capsule& other);

/// Two segments (spheres swept along lines): the closest points within the sum of the radii, by
/// segmentClosestPoints() and its quirks. Writes nothing.
/// @orig 0x00346500 PhysicsOverlap_SegmentSegment (unknown)
[[nodiscard]] bool overlapSegmentSegment(Contact& contact, const Segment& own, const Segment& other);

/// A box and a sphere: the sphere's centre within its radius of the box (a centre inside always hits). Writes nothing.
/// @orig 0x00346680 PhysicsOverlap_BoxSphere (unknown)
[[nodiscard]] bool overlapBoxSphere(Contact& contact, const Box& own, const Sphere& other);

/// The mirror of overlapBoxSphere().
/// @orig 0x00346db8 PhysicsOverlap_SphereBox (unknown)
[[nodiscard]] bool overlapSphereBox(Contact& contact, const Sphere& own, const Box& other);

/// A capsule and a box, in 2D: the capsule's circle against the box's footprint in the plane of its two most
/// horizontal axes; neither height is read. Writes `+0x50` the box's outward normal towards the capsule and `+0x88` the
/// depth (metres on a face, m² at a corner). Keeps the original's quirks: the (−, −) corner's normal is built from the
/// opposite corner, corner normals use axes 0 and 1 whatever the box's vertical axis, and a centre over the box takes
/// its face by comparing the signed coordinates, not by the nearest face.
/// @orig 0x00346808 PhysicsOverlap_CapsuleBox (unknown)
[[nodiscard]] bool overlapCapsuleBox(Contact& contact, const Capsule& own, const Box& other);

/// The mirror of overlapCapsuleBox(), whose contact halves it swaps on a hit: `+0x10` the box's outward normal.
/// `+0x50` gets the capsule-side normal (the box normal negated), where the original leaves whatever was there.
/// @orig 0x00346d18 PhysicsOverlap_BoxCapsule (unknown)
[[nodiscard]] bool overlapBoxCapsule(Contact& contact, const Box& own, const Capsule& other);

/// A segment and a sphere: the sphere's centre within the sum of the radii of the segment. Writes nothing.
/// @orig 0x00346de0 PhysicsOverlap_SegmentSphere (unknown)
[[nodiscard]] bool overlapSegmentSphere(Contact& contact, const Segment& own, const Sphere& other);

/// The mirror of overlapSegmentSphere().
/// @orig 0x00346eb0 PhysicsOverlap_SphereSegment (unknown)
[[nodiscard]] bool overlapSphereSegment(Contact& contact, const Sphere& own, const Segment& other);

/// A capsule and a sphere, the capsule a true capsule with rounded ends reaching its radius below the base and above
/// the top. Writes nothing.
/// @orig 0x00346ed8 PhysicsOverlap_CapsuleSphere (unknown)
[[nodiscard]] bool overlapCapsuleSphere(Contact& contact, const Capsule& own, const Sphere& other);

/// The mirror of overlapCapsuleSphere(): the push sphere's test against another human.
/// @orig 0x00346f90 PhysicsOverlap_SphereCapsule (unknown)
[[nodiscard]] bool overlapSphereCapsule(Contact& contact, const Sphere& own, const Capsule& other);

/// A segment and a capsule, the capsule again with rounded ends. Writes nothing.
/// @orig 0x00346fb8 PhysicsOverlap_SegmentCapsule (unknown)
[[nodiscard]] bool overlapSegmentCapsule(Contact& contact, const Segment& own, const Capsule& other);

/// The mirror of overlapSegmentCapsule().
/// @orig 0x00347148 PhysicsOverlap_CapsuleSegment (unknown)
[[nodiscard]] bool overlapCapsuleSegment(Contact& contact, const Capsule& own, const Segment& other);

/// The overlap table: the pair test for `own`'s type against `other`'s. A segment and a box have no entry, so they
/// never overlap (false, nothing written).
[[nodiscard]] bool overlapShapes(Contact& contact, const Shape& own, const Shape& other);

} // namespace coney::physics
