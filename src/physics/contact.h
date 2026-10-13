// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "animation/anim_math.h"

// The contact record the shape-pair tests and sweeps fill (0xc0 bytes in the original): two halves, one per shape,
// then the fraction, depth, point, slide, flags and triangle. A test writes only the fields its research section
// names; the others keep what the record held, as the original's uncleared stack record does, so the tests can check
// what was written. The moving body's resolution reads the same record.
// Research: docs/research/physics.md#contacts, docs/research/physics.md#u4-summary-for-the-implementer,
// docs/research/physics.md#u4b-summary-for-the-implementer, docs/research/physics-bodies.md#u2-mapping-into-the-c0-contact

namespace coney::physics {

class BodyOwner;

/// Contact flags (`+0xb0`).
inline constexpr std::uint32_t kContactAddSlide = 0x1;   ///< The capsule's corner slide: add `slide` to the move.
inline constexpr std::uint32_t kContactStoreSlide = 0x2; ///< Keep `slide` as the next move's push-out.
inline constexpr std::uint32_t kContactBehind = 0x10;    ///< Sphere sweep, option 0x40: the sphere started behind.
inline constexpr std::uint32_t kContactAway = 0x20;      ///< Sphere sweep, option 0x40: the move leaves the face.
inline constexpr std::uint32_t kContactAlong = 0x40;     ///< Sphere sweep, option 0x40: the move runs along it.
inline constexpr std::uint32_t kContactLanding = 0x80;   ///< An airborne human's landing segment.

/// A 16-byte vector as the contact holds it; `w` matters only where a test writes it (1 for a point or most normals,
/// 0 for a sphere-box feature normal).
struct Vec4 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    friend bool operator==(const Vec4&, const Vec4&) = default;
};

/// The x, y and z of a contact vector.
[[nodiscard]] inline anim::Vec3 xyz(const Vec4& v) { return anim::Vec3{v.x, v.y, v.z}; }

/// Writes x, y and z of a contact vector and keeps its `w`, for the fields whose `w` the research does not name.
inline void setXyz(Vec4& v, const anim::Vec3& value) {
    v.x = value.x;
    v.y = value.y;
    v.z = value.z;
}

/// A contact point: the position with `w` 1, as the sweeps write it.
[[nodiscard]] inline Vec4 pointW1(const anim::Vec3& p) { return Vec4{p.x, p.y, p.z, 1.0F}; }

/// One 0x40-byte half of a contact. The owner and shape words are written by whoever runs the test (`insertSorted`
/// for the body sweep, the overlap query, the strike loop).
struct ContactHalf {
    BodyOwner* owner = nullptr; ///< `+0x00`: this shape's owner; null for the level.
    bool shape = false;         ///< `+0x04` non-zero: this half has a body's shape (the level's word is 0).
    /// `+0x10`: this half's normal, pointing from this half's shape towards the other one. The mesh sweeps write the
    /// unit move into the own half's (docs/research/physics-bodies.md#u2-mapping-into-the-c0-contact); the other
    /// half's is the surface normal, out of the surface toward the mover.
    Vec4 normal;
    Vec4 localPoint;      ///< `+0x20`: the point in this half's box frame (sphere-box sweeps only).
    int featureType = 0;  ///< `+0x30`: the box feature: 0 face, 2 edge, 1 corner, 4 start inside.
    int featureIndex = 0; ///< `+0x34`: which face, edge or corner (docs/research/physics.md#the-normal-tables).

    friend bool operator==(const ContactHalf&, const ContactHalf&) = default;
};

/// A contact between the querying (own, half 0) shape and another (half 1). A mesh contact has no other owner and a
/// triangle; a body contact has both owners and no triangle.
struct Contact {
    std::array<ContactHalf, 2> halves{}; ///< `+0x00` own, `+0x40` other.
    float fraction = 0.0F;               ///< `+0x80`: the fraction of the move at first contact (an overlap's depth).
    /// `+0x88`: a penetration depth (overlap tests that say so); the sphere sweep writes its signed start distance from
    /// the plane here.
    float depth = 0.0F;
    Vec4 point;                            ///< `+0x90`: the contact point in the world.
    Vec4 slide;                            ///< `+0xa0`: a slide vector, read with kContactAddSlide or kContactStoreSlide.
    std::uint32_t flags = 0;               ///< `+0xb0`: kContact* bits.
    std::optional<std::uint32_t> triangle; ///< `+0xb4`: the level triangle met, nothing for a body.

    friend bool operator==(const Contact&, const Contact&) = default;
};

/// Puts `contact` into `sorted` before the first contact whose fraction is strictly greater (so after any of equal
/// fraction), after writing its owners and shape words. No cap is checked (the callers keep to their 50).
/// @orig 0x0033d718 Contact_InsertSorted (unknown)
void insertSorted(Contact& contact, std::vector<Contact*>& sorted, BodyOwner* ownOwner, BodyOwner* otherOwner,
                  bool otherShape);

} // namespace coney::physics
