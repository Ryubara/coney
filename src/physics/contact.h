// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

// The contact record the shape-pair tests and sweeps fill (0xc0 bytes in the original): two halves, one per shape,
// then the fraction, depth, point and flags. A test writes only the fields its research section names; the others
// keep what the record held, as the original's uncleared stack record does, so the tests can check what was written.
// Research: docs/research/physics.md#contacts, docs/research/physics.md#u4-summary-for-the-implementer,
// docs/research/physics.md#u4b-summary-for-the-implementer

namespace coney::physics {

/// A 16-byte vector as the contact holds it; `w` matters only where a test writes it (1 for a point or most normals,
/// 0 for a sphere-box feature normal).
struct Vec4 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    friend bool operator==(const Vec4&, const Vec4&) = default;
};

/// One 0x40-byte half of a contact. The owner and shape words (`+0x00`, `+0x04` of a half) are set by whoever runs
/// the test (the overlap query, the strike loop) and are not modelled here yet.
struct ContactHalf {
    Vec4 normal;          ///< `+0x10`: this half's normal, pointing from this half's shape towards the other one.
    Vec4 localPoint;      ///< `+0x20`: the point in this half's box frame (sphere-box sweeps only).
    int featureType = 0;  ///< `+0x30`: the box feature: 0 face, 2 edge, 1 corner, 4 start inside.
    int featureIndex = 0; ///< `+0x34`: which face, edge or corner (docs/research/physics.md#the-normal-tables).

    friend bool operator==(const ContactHalf&, const ContactHalf&) = default;
};

/// A contact between the querying (own, half 0) shape and another (half 1).
struct Contact {
    std::array<ContactHalf, 2> halves{}; ///< `+0x00` own, `+0x40` other.
    float fraction = 0.0F;               ///< `+0x80`: the fraction of the move at first contact, 0 to 1.
    float depth = 0.0F;                  ///< `+0x88`: a penetration depth (overlap tests that say so).
    Vec4 point;                          ///< `+0x90`: the contact point in the world.
    Vec4 slide;                          ///< `+0xa0`: a slide vector (mesh corner slides).
    std::uint32_t flags = 0;             ///< `+0xb0`: flags (`0x80` a human's landing).

    friend bool operator==(const Contact&, const Contact&) = default;
};

} // namespace coney::physics
