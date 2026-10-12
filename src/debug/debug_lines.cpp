// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/debug_lines.h"

#include <cmath>
#include <numbers>

namespace coney::debug {

namespace {

// Two unit vectors at right angles to each other and to the unit `axis`, spanning the plane it is the normal of.
void planeOf(anim::Vec3 axis, anim::Vec3& u, anim::Vec3& v) {
    // Any vector not along the axis gives the first; up unless the axis is nearly vertical.
    const anim::Vec3 helper = std::abs(axis.z) < 0.9F ? anim::Vec3{0.0F, 0.0F, 1.0F} : anim::Vec3{1.0F, 0.0F, 0.0F};
    u = anim::normalise(anim::cross(axis, helper));
    v = anim::cross(axis, u);
}

} // namespace

void DebugLines::add(anim::Vec3 a, anim::Vec3 b, graphics::Rgba colour) {
    m_lines.push_back(DebugLine{.a = a, .b = b, .colour = colour});
}

void DebugLines::cross(anim::Vec3 p, float size, graphics::Rgba colour) {
    const float h = size * 0.5F;
    add({p.x - h, p.y, p.z}, {p.x + h, p.y, p.z}, colour);
    add({p.x, p.y - h, p.z}, {p.x, p.y + h, p.z}, colour);
    add({p.x, p.y, p.z - h}, {p.x, p.y, p.z + h}, colour);
}

void DebugLines::circle(anim::Vec3 centre, float radius, anim::Vec3 axis, graphics::Rgba colour) {
    if (anim::length(axis) <= 0.0F || radius <= 0.0F) {
        return;
    }
    anim::Vec3 u;
    anim::Vec3 v;
    planeOf(anim::normalise(axis), u, v);
    // Points round the circle, each joined to the next.
    const auto at = [&](int i) {
        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(i) / kCircleSegments;
        return anim::add(centre,
                         anim::add(anim::scale(u, radius * std::cos(angle)), anim::scale(v, radius * std::sin(angle))));
    };
    for (int i = 0; i < kCircleSegments; ++i) {
        add(at(i), at(i + 1), colour);
    }
}

void DebugLines::sphere(anim::Vec3 centre, float radius, graphics::Rgba colour) {
    circle(centre, radius, {0.0F, 0.0F, 1.0F}, colour);
    circle(centre, radius, {1.0F, 0.0F, 0.0F}, colour);
    circle(centre, radius, {0.0F, 1.0F, 0.0F}, colour);
}

void DebugLines::uprightCapsule(anim::Vec3 base, float height, float radius, graphics::Rgba colour) {
    const anim::Vec3 top = anim::add(base, anim::Vec3{0.0F, 0.0F, height});
    circle(base, radius, {0.0F, 0.0F, 1.0F}, colour);
    circle(top, radius, {0.0F, 0.0F, 1.0F}, colour);
    // Four uprights at the compass points.
    for (const anim::Vec3 side : {anim::Vec3{radius, 0.0F, 0.0F}, anim::Vec3{-radius, 0.0F, 0.0F},
                                  anim::Vec3{0.0F, radius, 0.0F}, anim::Vec3{0.0F, -radius, 0.0F}}) {
        add(anim::add(base, side), anim::add(top, side), colour);
    }
}

void DebugLines::sweptSphere(anim::Vec3 a, anim::Vec3 b, float radius, graphics::Rgba colour) {
    sphere(a, radius, colour);
    const anim::Vec3 along = anim::subtract(b, a);
    if (anim::length(along) <= 1e-5F) {
        return;
    }
    sphere(b, radius, colour);
    // The four side lines, offset across the segment in the plane it is the normal of.
    anim::Vec3 u;
    anim::Vec3 v;
    planeOf(anim::normalise(along), u, v);
    for (const anim::Vec3 side : {u, anim::scale(u, -1.0F), v, anim::scale(v, -1.0F)}) {
        const anim::Vec3 offset = anim::scale(side, radius);
        add(anim::add(a, offset), anim::add(b, offset), colour);
    }
}

void DebugLines::arrow(anim::Vec3 from, anim::Vec3 along, graphics::Rgba colour) {
    const float length = anim::length(along);
    const anim::Vec3 tip = anim::add(from, along);
    add(from, tip, colour);
    if (length <= 1e-5F) {
        return;
    }
    // Two barbs a fifth of the arrow long, swept back from the tip.
    anim::Vec3 u;
    anim::Vec3 v;
    const anim::Vec3 unit = anim::scale(along, 1.0F / length);
    planeOf(unit, u, v);
    const float barb = length * 0.2F;
    const anim::Vec3 back = anim::scale(unit, -barb);
    add(tip, anim::add(tip, anim::add(back, anim::scale(u, barb * 0.5F))), colour);
    add(tip, anim::add(tip, anim::add(back, anim::scale(u, -barb * 0.5F))), colour);
}

void DebugLines::append(const DebugLines& other) {
    m_lines.insert(m_lines.end(), other.m_lines.begin(), other.m_lines.end());
}

void DebugLines::appendOffset(const DebugLines& other, anim::Vec3 offset) {
    m_lines.reserve(m_lines.size() + other.m_lines.size());
    for (const DebugLine& line : other.m_lines) {
        m_lines.push_back(DebugLine{anim::add(line.a, offset), anim::add(line.b, offset), line.colour});
    }
}

} // namespace coney::debug
