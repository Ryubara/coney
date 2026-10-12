// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "graphics/render_device.h"

// Coloured 3D line segments the Debug draw page asks a play mode to draw (docs/guides/debug-menu.md#debug-draw):
// built here, platform-neutral and testable, and handed to the platform's line renderer as a list. Coney's own debug
// tooling: the original has nothing like it.

namespace coney::debug {

/// One line from `a` to `b` in the game's axes (z up, metres).
struct DebugLine {
    anim::Vec3 a;
    anim::Vec3 b;
    graphics::Rgba colour;
};

/// A list of debug lines with helpers for the shapes the physics uses: spheres, upright capsules, swept spheres,
/// arrows and crosses.
class DebugLines {
  public:
    /// The segments a circle or a sphere's ring is drawn with.
    static constexpr int kCircleSegments = 16;

    /// Adds the line from `a` to `b`.
    void add(anim::Vec3 a, anim::Vec3 b, graphics::Rgba colour);
    /// Adds a cross of three axis-aligned lines `size` long, centred on `p`.
    void cross(anim::Vec3 p, float size, graphics::Rgba colour);
    /// Adds a circle of `radius` round `centre`, in the plane whose normal is `axis` (any length but zero).
    void circle(anim::Vec3 centre, float radius, anim::Vec3 axis, graphics::Rgba colour);
    /// Adds a sphere as its three great circles: about the vertical, and in the two upright planes through the axes.
    void sphere(anim::Vec3 centre, float radius, graphics::Rgba colour);
    /// Adds an upright cylinder from `base` to `height` above it of `radius`: a ring at each end and four uprights.
    /// The physics treats a human's capsule as one (docs/research/physics.md#shapes).
    void uprightCapsule(anim::Vec3 base, float height, float radius, graphics::Rgba colour);
    /// Adds a sphere of `radius` swept from `a` to `b` (a strike shape's segment): a sphere at each end and four lines
    /// along the sides. A zero-length segment is one sphere.
    void sweptSphere(anim::Vec3 a, anim::Vec3 b, float radius, graphics::Rgba colour);
    /// Adds an arrow from `from` along `along` (its length is the arrow's), with a small head at the far end.
    void arrow(anim::Vec3 from, anim::Vec3 along, graphics::Rgba colour);
    /// Appends every line of `other`.
    void append(const DebugLines& other);
    /// Appends every line of `other` moved by `offset`.
    void appendOffset(const DebugLines& other, anim::Vec3 offset);

    /// The lines so far.
    [[nodiscard]] std::span<const DebugLine> lines() const { return m_lines; }
    [[nodiscard]] std::size_t size() const { return m_lines.size(); }
    [[nodiscard]] bool empty() const { return m_lines.empty(); }

  private:
    std::vector<DebugLine> m_lines;
};

} // namespace coney::debug
