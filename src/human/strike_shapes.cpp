// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/strike_shapes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace coney::human {

namespace {

// The bones whose ids name a shape bit: 2 up to 33.
constexpr int kFirstBit = 2;
constexpr int kLastBit = 33;

// The ten shapes (combat.md#moving-strikes): bone, segment, offset, radius, length, target.
constexpr std::array<StrikeShapeDef, 10> kDefs{{
    {.bone = 3, .segment = true, .offset = {-0.07F, 0.0F, 0.05F}, .radius = 0.18F, .length = 0.38F, .target = true},
    {.bone = 6, .segment = false, .offset = {0.05F, 0.0F, 0.03F}, .radius = 0.15F, .length = 0.0F, .target = true},
    {.bone = 18, .segment = true, .offset = {}, .radius = 0.07F, .length = 0.20F, .target = false},
    {.bone = 19, .segment = false, .offset = {0.08F, 0.0F, 0.0F}, .radius = 0.09F, .length = 0.0F, .target = false},
    {.bone = 24, .segment = true, .offset = {}, .radius = 0.07F, .length = 0.20F, .target = false},
    {.bone = 25, .segment = false, .offset = {0.08F, 0.0F, 0.0F}, .radius = 0.09F, .length = 0.0F, .target = false},
    {.bone = 29, .segment = true, .offset = {}, .radius = 0.10F, .length = 0.40F, .target = false},
    {.bone = 30, .segment = false, .offset = {}, .radius = 0.15F, .length = 0.0F, .target = false},
    {.bone = 32, .segment = true, .offset = {}, .radius = 0.10F, .length = 0.40F, .target = false},
    {.bone = 33, .segment = false, .offset = {}, .radius = 0.15F, .length = 0.0F, .target = false},
}};

// A point of the character's space carried into the world as the skin carries the drawn body: leaned about the forward
// axis at the feet, turned by the heading about z, moved to the feet.
anim::Vec3 toWorld(const BodyPlacement& placement, anim::Vec3 p) {
    const float lc = std::cos(-placement.lean);
    const float ls = std::sin(-placement.lean);
    const anim::Vec3 q{(p.x * lc) + (p.z * ls), p.y, (-p.x * ls) + (p.z * lc)};
    const float c = std::cos(placement.heading);
    const float s = std::sin(placement.heading);
    return anim::Vec3{(q.x * c) - (q.y * s) + placement.feet.x, (q.x * s) + (q.y * c) + placement.feet.y,
                      q.z + placement.feet.z};
}

// The squared distance between segments p1-q1 and p2-q2 (points when an end equals the start).
float segmentDistanceSquared(anim::Vec3 p1, anim::Vec3 q1, anim::Vec3 p2, anim::Vec3 q2) {
    constexpr float kTiny = 1e-8F;
    const anim::Vec3 d1 = anim::subtract(q1, p1);
    const anim::Vec3 d2 = anim::subtract(q2, p2);
    const anim::Vec3 r = anim::subtract(p1, p2);
    const float a = anim::dot(d1, d1);
    const float e = anim::dot(d2, d2);
    const float f = anim::dot(d2, r);
    float s = 0.0F;
    float t = 0.0F;
    if (a <= kTiny && e <= kTiny) {
        return anim::dot(r, r);
    }
    if (a <= kTiny) {
        t = std::clamp(f / e, 0.0F, 1.0F);
    } else {
        const float c = anim::dot(d1, r);
        if (e <= kTiny) {
            s = std::clamp(-c / a, 0.0F, 1.0F);
        } else {
            const float b = anim::dot(d1, d2);
            const float denom = (a * e) - (b * b);
            s = denom > kTiny ? std::clamp(((b * f) - (c * e)) / denom, 0.0F, 1.0F) : 0.0F;
            t = ((b * s) + f) / e;
            if (t < 0.0F) {
                t = 0.0F;
                s = std::clamp(-c / a, 0.0F, 1.0F);
            } else if (t > 1.0F) {
                t = 1.0F;
                s = std::clamp((b - c) / a, 0.0F, 1.0F);
            }
        }
    }
    const anim::Vec3 c1 = anim::add(p1, anim::scale(d1, s));
    const anim::Vec3 c2 = anim::add(p2, anim::scale(d2, t));
    const anim::Vec3 gap = anim::subtract(c1, c2);
    return anim::dot(gap, gap);
}

// The point of triangle p0 p1 p2 nearest `p`.
anim::Vec3 closestOnTriangle(anim::Vec3 p, anim::Vec3 a, anim::Vec3 b, anim::Vec3 c) {
    const anim::Vec3 ab = anim::subtract(b, a);
    const anim::Vec3 ac = anim::subtract(c, a);
    const anim::Vec3 ap = anim::subtract(p, a);
    const float d1 = anim::dot(ab, ap);
    const float d2 = anim::dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return a;
    }
    const anim::Vec3 bp = anim::subtract(p, b);
    const float d3 = anim::dot(ab, bp);
    const float d4 = anim::dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return b;
    }
    const float vc = (d1 * d4) - (d3 * d2);
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return anim::add(a, anim::scale(ab, d1 / (d1 - d3)));
    }
    const anim::Vec3 cp = anim::subtract(p, c);
    const float d5 = anim::dot(ab, cp);
    const float d6 = anim::dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return c;
    }
    const float vb = (d5 * d2) - (d1 * d6);
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return anim::add(a, anim::scale(ac, d2 / (d2 - d6)));
    }
    const float va = (d3 * d6) - (d5 * d4);
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        return anim::add(b, anim::scale(anim::subtract(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }
    const float denom = 1.0F / (va + vb + vc);
    return anim::add(a, anim::add(anim::scale(ab, vb * denom), anim::scale(ac, vc * denom)));
}

} // namespace

std::span<const StrikeShapeDef> strikeShapeDefs() { return kDefs; }

std::vector<PosedShape> poseStrikeShapes(std::span<const StrikeShapeDef> defs,
                                         std::span<const anim::Mat34, anim::kPoseBones> bones,
                                         const BodyPlacement& placement) {
    std::vector<PosedShape> posed;
    posed.reserve(defs.size());
    for (const StrikeShapeDef& def : defs) {
        const anim::Mat34& bone = bones[static_cast<std::size_t>(def.bone)];
        // The bone's position plus its rotation of the (scaled) offset; a segment runs along the bone's local x.
        const anim::Vec3 start = anim::transformPoint(bone, anim::scale(def.offset, placement.scale));
        const anim::Vec3 along = anim::scale(anim::transformDirection(bone, {1.0F, 0.0F, 0.0F}), def.length);
        const anim::Vec3 end = def.segment ? anim::add(start, anim::scale(along, placement.scale)) : start;
        posed.push_back(PosedShape{.a = toWorld(placement, start),
                                   .b = toWorld(placement, end),
                                   .radius = def.radius * placement.scale,
                                   .bone = def.bone});
    }
    return posed;
}

bool shapesOverlap(const PosedShape& a, const PosedShape& b) {
    const float reach = a.radius + b.radius;
    return segmentDistanceSquared(a.a, a.b, b.a, b.b) < reach * reach;
}

bool sweptShapesMeet(const PosedShape& from, const PosedShape& to, const PosedShape& part) {
    if (shapesOverlap(to, part)) {
        return true;
    }
    // Each end's path from its old place to its new one, at the shape's radius.
    const auto path = [&](anim::Vec3 start, anim::Vec3 end) {
        return shapesOverlap(PosedShape{.a = start, .b = end, .radius = to.radius, .bone = to.bone}, part);
    };
    return path(from.a, to.a) || path(from.b, to.b);
}

bool shapeTouchesTriangle(const PosedShape& shape, anim::Vec3 p0, anim::Vec3 p1, anim::Vec3 p2) {
    // A segment crossing the triangle's plane inside it touches it; otherwise the nearest of the segment's ends and
    // the triangle's edges decides.
    const anim::Vec3 normal = anim::cross(anim::subtract(p1, p0), anim::subtract(p2, p0));
    const float da = anim::dot(normal, anim::subtract(shape.a, p0));
    const float db = anim::dot(normal, anim::subtract(shape.b, p0));
    if ((da < 0.0F) != (db < 0.0F) && std::fabs(da - db) > 1e-12F) {
        const anim::Vec3 hit = anim::add(shape.a, anim::scale(anim::subtract(shape.b, shape.a), da / (da - db)));
        if (anim::distance(closestOnTriangle(hit, p0, p1, p2), hit) < 1e-4F) {
            return true;
        }
    }
    const float r2 = shape.radius * shape.radius;
    for (const anim::Vec3 end : {shape.a, shape.b}) {
        const anim::Vec3 near = closestOnTriangle(end, p0, p1, p2);
        const anim::Vec3 gap = anim::subtract(end, near);
        if (anim::dot(gap, gap) < r2) {
            return true;
        }
    }
    const std::array<std::array<anim::Vec3, 2>, 3> edges{{{p0, p1}, {p1, p2}, {p2, p0}}};
    return std::ranges::any_of(edges, [&](const std::array<anim::Vec3, 2>& edge) {
        return segmentDistanceSquared(shape.a, shape.b, edge[0], edge[1]) < r2;
    });
}

void StrikeShapes::onEvent(const anim::ClipEvent& event) {
    switch (event.type) {
    case kEventStrikeOn:
        set(static_cast<int>(event.word), true);
        break;
    case kEventStrikeOff:
        set(static_cast<int>(event.word), false);
        break;
    case kEventStrikeAllOn:
        m_capsule = true;
        for (const StrikeShapeDef& def : kDefs) {
            set(def.bone, true);
        }
        break;
    case kEventStrikeAllOff:
        clear();
        break;
    default:
        break;
    }
    if (m_bits == 0) {
        m_capsule = false;
        m_struckObjects.clear();
        m_struckHumans.clear();
    }
}

void StrikeShapes::clear() {
    m_bits = 0;
    m_capsule = false;
    m_struckObjects.clear();
    m_struckHumans.clear();
}

bool StrikeShapes::on(int bone) const {
    return bone >= kFirstBit && bone <= kLastBit && (m_bits & (1U << static_cast<unsigned>(bone - kFirstBit))) != 0;
}

std::vector<StrikeShapeDef> StrikeShapes::active() const {
    std::vector<StrikeShapeDef> shapes;
    for (const StrikeShapeDef& def : kDefs) {
        if (on(def.bone)) {
            shapes.push_back(def);
        }
    }
    return shapes;
}

bool StrikeShapes::struck(double object) const {
    return std::ranges::find(m_struckObjects, object) != m_struckObjects.end();
}

bool StrikeShapes::struckHuman(const void* human) const {
    return std::ranges::find(m_struckHumans, human) != m_struckHumans.end();
}

void StrikeShapes::set(int bone, bool on) {
    if (bone < kFirstBit || bone > kLastBit) {
        return;
    }
    const std::uint32_t bit = 1U << static_cast<unsigned>(bone - kFirstBit);
    m_bits = on ? (m_bits | bit) : (m_bits & ~bit);
}

} // namespace coney::human
