// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <variant>

#include "animation/anim_math.h"

// The four physics shapes as posed values in world space: what the shape-pair tests read of each shape record.
// Only the posed fields are here; the shape head (bone, flags, previous position) and the posing come with Unit 6.
// Research: docs/research/physics.md#shapes, docs/research/physics.md#u4-the-shape-fields-read

namespace coney::physics {

/// The shape types, as the original numbers them (shape `+0x30`); also the row and column of the pair tables.
enum class ShapeType { Box = 1, Sphere = 2, Capsule = 3, Segment = 4 };

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

} // namespace coney::physics
