// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "animation/anim_math.h"

// The physics world's contact: what a sweep or overlap found, and what the moving body's resolution reads. The fields
// are the original's 0xc0-byte record's, named; the original's offset is given for each.
// Research: docs/research/physics.md#contacts, docs/research/physics-bodies.md#u3-records

namespace coney::physics {

class BodyOwner;

/// Contact flags (`+0xb0`).
inline constexpr std::uint32_t kContactAddSlide = 0x1;   ///< The capsule's corner slide: add `slide` to the move.
inline constexpr std::uint32_t kContactStoreSlide = 0x2; ///< Keep `slide` as the next move's push-out.
inline constexpr std::uint32_t kContactBehind = 0x10;    ///< Sphere sweep, option 0x40: the sphere started behind.
inline constexpr std::uint32_t kContactAway = 0x20;      ///< Sphere sweep, option 0x40: the move leaves the face.
inline constexpr std::uint32_t kContactAlong = 0x40;     ///< Sphere sweep, option 0x40: the move runs along it.
inline constexpr std::uint32_t kContactLanding = 0x80;   ///< An airborne human's landing segment.

/// One contact (the original's 0xc0-byte record). A mesh contact has no other owner and a triangle; a body contact
/// has both owners and no triangle.
struct Contact {
    BodyOwner* ownOwner = nullptr;   ///< `+0x00`: the moving body's owner.
    BodyOwner* otherOwner = nullptr; ///< `+0x40`: the other body's owner; null for the level.
    bool otherShape = false;         ///< `+0x44` non-zero: met another body's shape (the level's is 0).
    anim::Vec3 moveDirection;        ///< `+0x10`: the unit move (sweeps).
    anim::Vec3 normal;               ///< `+0x50`: unit, out of the surface toward the mover.
    float fraction = 0.0F;           ///< `+0x80`: the fraction of the pass's move (an overlap's depth).
    float startDistance = 0.0F;      ///< `+0x88`: the sphere sweep's signed start distance from the plane.
    anim::Vec3 point;                ///< `+0x90`: the shape's position at the contact (sweeps).
    anim::Vec3 slide;                ///< `+0xa0`: a slide vector, read with kContactAddSlide or kContactStoreSlide.
    std::uint32_t flags = 0;         ///< `+0xb0`: kContact* bits.
    std::optional<std::uint32_t> triangle; ///< `+0xb4`: the level triangle met, nothing for a body.
};

/// Puts `contact` into `sorted` before the first contact whose fraction is strictly greater (so after any of equal
/// fraction), after writing its owners and whether the other is a body's shape. No cap is checked (the callers keep to
/// their 50).
/// @orig 0x0033d718 Contact_InsertSorted (unknown)
void insertSorted(Contact& contact, std::vector<Contact*>& sorted, BodyOwner* ownOwner, BodyOwner* otherOwner,
                  bool otherShape);

} // namespace coney::physics
