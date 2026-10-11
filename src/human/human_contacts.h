// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "animation/anim_math.h"

// What a walking human meets of another human: one upright capsule swept against another, and the push the contact
// gives the one met. Pure functions, so each rule can be tested on its own; Human::meetHumans() runs them over the
// step's humans.
// Research: docs/research/physics.md#human-contacts

namespace coney::human {

/// A human's capsule as another human's walking sweep meets it: upright from `feet` to `top` above them, of `radius`
/// (the shape's own radius, 0.35 × the scale, without the walking sphere's body factor).
struct UprightCapsule {
    anim::Vec3 feet;
    float top = 0.0F;
    float radius = 0.0F;
};

/// Where a capsule moving by `move` first meets a standing one: the fraction of the move (0 to 1) and the contact's
/// normal, across the ground from the one met to the mover.
struct CapsuleContact {
    float fraction = 0.0F;
    anim::Vec3 normal;
};

/// Sweeps `mover` by `move` against `other`, across the ground (the capsules are upright, so only their axes' distance
/// matters): they meet where the axes are the two radii apart, and only while their heights overlap. Already closer
/// than that, a move that goes in is met at fraction 0 and one that leaves is not met (**Coney's choice**: the original
/// capsule sweep's start case is not traced; the walls' own sweep meets a face it already touches at fraction 0,
/// docs/research/characters.md#slopes). Two axes on top of each other give no normal and are not met.
/// @orig 0x00343cd8 PhysicsSweep_CapsuleCapsule (unknown)
[[nodiscard]] std::optional<CapsuleContact> sweepCapsules(const UprightCapsule& mover, anim::Vec3 move,
                                                          const UprightCapsule& other);

/// The push a contact (code 4) adds to the pending push-out of the human met: half of the mover's move `move` into it,
/// along the `normal` (pointing at the mover), over the met human's push weight `pushWeight` (attribute 7: 1, or 1e9
/// while it must not move). The movers' weights (attribute 1, 82 for every human) cancel. Zero when the move does not
/// go in.
/// @orig 0x0033d9d8 PhysicsBody_ResolveContacts (unknown)
[[nodiscard]] anim::Vec3 contactPush(anim::Vec3 move, anim::Vec3 normal, float pushWeight);

} // namespace coney::human
