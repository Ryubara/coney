// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"

// The small kernels the static overlap tests share: the closest point of a segment to a point, the closest points of
// two segments (with the original's absolute parallel threshold), and the capsule-box circle tests.
// Research: docs/research/physics.md#u4-the-kernels

namespace coney::physics {

/// The parameter t in [0, 1] of the point of the segment `p0 + t·d` nearest `q`; 0 for a zero-length segment.
/// @orig 0x0034ac08 Segment_ClosestParam (unknown)
[[nodiscard]] float segmentClosestParam(anim::Vec3 p0, anim::Vec3 d, anim::Vec3 q);

/// The point of the segment `p0 + t·d` nearest `q`.
/// @orig 0x0034acc8 Segment_ClosestPoint (unknown)
[[nodiscard]] anim::Vec3 segmentClosestPoint(anim::Vec3 p0, anim::Vec3 d, anim::Vec3 q);

/// The parameters (s, t), each in [0, 1], of the closest points `p1 + s·d1` and `p2 + t·d2` of two segments, given
/// `w = p1 − p2`.
struct SegmentParams {
    float s = 0.0F;
    float t = 0.0F;
};

/// The clamped closest-points solution. Two quirks of the original are kept: the segments count as parallel when
/// |d1|²·|d2|² − (d1·d2)² < 1e-6 (an absolute threshold, so short segments are parallel at a few degrees), and a
/// zero-length second segment leaves s at 0 whatever the first segment's direction.
/// @orig 0x0034ad50 Segment_ClosestPoints (unknown)
[[nodiscard]] SegmentParams segmentClosestPoints(anim::Vec3 d1, anim::Vec3 d2, anim::Vec3 w);

/// A circle of radius `r` at (`x`, `y`), mirrored so that x < −hx and y < −hy, against the rectangle's corner
/// (−hx, −hy): a hit (touching too) gives `depth` = r² − distance², a squared length.
/// @orig 0x0034a2d0 CapsuleBox_CornerCircle (unknown)
[[nodiscard]] bool capsuleBoxCornerCircle(float x, float y, float r, float hx, float hy, float& depth);

/// A circle of radius `r` at `x` < −hx against the face x = −hx: a hit (touching too) gives `depth` = x + hx + r.
/// @orig 0x0034a6a0 CapsuleBox_SideInterval (unknown)
[[nodiscard]] bool capsuleBoxSideInterval(float x, float r, float hx, float& depth);

} // namespace coney::physics
