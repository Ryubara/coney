// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/overlaps.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <variant>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/segment_kernels.h"
#include "physics/shapes.h"

namespace coney::physics {

namespace {

using anim::Vec3;

// The original's "coincident axes" threshold for the capsule-capsule normal (0x358637bd).
constexpr float kCoincident = 1e-6F;

// Squares a float.
float sq(float v) { return v * v; }

// A unit vector as a contact normal with w 1.
Vec4 normalW1(Vec3 v) { return Vec4{v.x, v.y, v.z, 1.0F}; }

// The negation of `v`.
Vec3 negate(Vec3 v) { return Vec3{-v.x, -v.y, -v.z}; }

// The component `k` of `v`.
float component(Vec3 v, std::size_t k) { return k == 0 ? v.x : (k == 1 ? v.y : v.z); }

// The vertical segment of a capsule, base to top, for the tests that round its ends.
Vec3 capsuleAxis(const Capsule& capsule) { return Vec3{0.0F, 0.0F, capsule.height}; }

// Whether the closest points of two segments (`d1` from `s1`, `d2` from `s2`) are within `reach`.
bool segmentsWithin(Vec3 s1, Vec3 d1, Vec3 s2, Vec3 d2, float reach) {
    const Vec3 w = anim::subtract(s1, s2);
    const SegmentParams p = segmentClosestPoints(d1, d2, w);
    const Vec3 between = anim::subtract(anim::add(w, anim::scale(d1, p.s)), anim::scale(d2, p.t));
    return anim::dot(between, between) <= sq(reach);
}

} // namespace

bool overlapSphereSphere(Contact& /*contact*/, const Sphere& own, const Sphere& other) {
    const Vec3 d = anim::subtract(own.centre, other.centre);
    return anim::dot(d, d) <= sq(own.radius + other.radius);
}

bool overlapBoxBox(Contact& /*contact*/, const Box& own, const Box& other) {
    const std::array<float, 3>& a = own.half;
    const std::array<float, 3>& b = other.half;
    // The original's matrix: R[k][m] = the sum over the four rows n (three axes, then the centre) of A_n[k]·B_n[m],
    // where a correct test takes A_k · B_m. Kept: no caller pairs two boxes, so it changes nothing in play
    // (docs/research/physics.md#u4-physicsoverlap-boxbox-00345dd8).
    std::array<std::array<float, 3>, 3> r{};
    std::array<std::array<float, 3>, 3> absR{};
    for (std::size_t k = 0; k < 3; ++k) {
        for (std::size_t m = 0; m < 3; ++m) {
            float sum = 0.0F;
            for (std::size_t n = 0; n < 3; ++n) {
                sum += component(own.axes.at(n), k) * component(other.axes.at(n), m);
            }
            sum += component(own.centre, k) * component(other.centre, m);
            r.at(k).at(m) = sum;
            absR.at(k).at(m) = std::fabs(sum);
        }
    }
    const Vec3 between = anim::subtract(other.centre, own.centre);
    std::array<float, 3> t{};
    for (std::size_t k = 0; k < 3; ++k) {
        t.at(k) = anim::dot(own.axes.at(k), between);
    }
    // Own's three axes; separated only on a strict excess, so touching boxes overlap.
    for (std::size_t k = 0; k < 3; ++k) {
        const float reach = a.at(k) + b[0] * absR.at(k)[0] + b[1] * absR.at(k)[1] + b[2] * absR.at(k)[2];
        if (std::fabs(t.at(k)) > reach) {
            return false;
        }
    }
    // Other's three axes.
    for (std::size_t m = 0; m < 3; ++m) {
        const float along = t[0] * r[0].at(m) + t[1] * r[1].at(m) + t[2] * r[2].at(m);
        const float reach = a[0] * absR[0].at(m) + a[1] * absR[1].at(m) + a[2] * absR[2].at(m) + b.at(m);
        if (std::fabs(along) > reach) {
            return false;
        }
    }
    // The nine cross products A_i × B_j.
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t i1 = (i + 1) % 3;
        const std::size_t i2 = (i + 2) % 3;
        for (std::size_t j = 0; j < 3; ++j) {
            const std::size_t j1 = (j + 1) % 3;
            const std::size_t j2 = (j + 2) % 3;
            const float along = t.at(i2) * r.at(i1).at(j) - t.at(i1) * r.at(i2).at(j);
            const float reach = a.at(i1) * absR.at(i2).at(j) + a.at(i2) * absR.at(i1).at(j) +
                                b.at(j1) * absR.at(i).at(j2) + b.at(j2) * absR.at(i).at(j1);
            if (std::fabs(along) > reach) {
                return false;
            }
        }
    }
    return true;
}

bool overlapCapsuleCapsule(Contact& contact, const Capsule& own, const Capsule& other) {
    // The height ranges, touching included.
    if (own.base.z > other.base.z + other.height || other.base.z > own.base.z + own.height) {
        return false;
    }
    // Side by side, strictly.
    const Vec3 d{other.base.x - own.base.x, other.base.y - own.base.y, 0.0F};
    const float distanceSq = anim::dot(d, d);
    const float reach = own.radius + other.radius;
    if (distanceSq >= sq(reach)) {
        return false;
    }
    Vec3 toOther{0.0F, 1.0F, 0.0F};
    float distance = 0.0F;
    if (distanceSq >= kCoincident) {
        distance = std::sqrt(distanceSq);
        toOther = anim::scale(d, 1.0F / distance);
    }
    contact.halves[0].normal = normalW1(toOther);
    contact.halves[1].normal = normalW1(negate(toOther));
    contact.depth = reach - distance;
    return true;
}

bool overlapSegmentSegment(Contact& /*contact*/, const Segment& own, const Segment& other) {
    return segmentsWithin(own.start, anim::subtract(own.end, own.start), other.start,
                          anim::subtract(other.end, other.start), own.radius + other.radius);
}

bool overlapBoxSphere(Contact& /*contact*/, const Box& own, const Sphere& other) {
    const Vec3 offset = anim::subtract(other.centre, own.centre);
    float distanceSq = 0.0F;
    for (std::size_t k = 0; k < 3; ++k) {
        const float x = anim::dot(own.axes.at(k), offset);
        const float h = own.half.at(k);
        if (x < -h) {
            distanceSq += sq(x + h);
        } else if (x > h) {
            distanceSq += sq(x - h);
        }
    }
    return distanceSq <= sq(other.radius);
}

bool overlapSphereBox(Contact& contact, const Sphere& own, const Box& other) {
    return overlapBoxSphere(contact, other, own);
}

bool overlapCapsuleBox(Contact& contact, const Capsule& own, const Box& other) {
    // Step 1: the box's most vertical axis (the first wins a tie); the other two span the plane of the test.
    std::size_t up = 0;
    for (std::size_t k = 1; k < 3; ++k) {
        if (std::fabs(other.axes.at(k).z) > std::fabs(other.axes.at(up).z)) {
            up = k;
        }
    }
    const std::size_t i = (up + 1) % 3;
    const std::size_t j = (i + 1) % 3;
    const float hx = other.half.at(i);
    const float hy = other.half.at(j);
    const Vec3& ai = other.axes.at(i);
    const Vec3& aj = other.axes.at(j);
    // Step 2: the capsule's base in that plane, heights dropped.
    const Vec3 d{own.base.x - other.centre.x, own.base.y - other.centre.y, 0.0F};
    const float x = anim::dot(d, ai);
    const float y = anim::dot(d, aj);
    const float r = own.radius;
    Vec4& normal = contact.halves[1].normal;
    float depth = 0.0F;
    // A corner's normal: the in-plane offset put on axes 0 and 1 whatever the plane is, normalised; w not written.
    const auto cornerNormal = [&](float ox, float oy) {
        const Vec3 n = anim::normalise(anim::add(anim::scale(other.axes[0], ox), anim::scale(other.axes[1], oy)));
        normal.x = n.x;
        normal.y = n.y;
        normal.z = n.z;
    };
    // Step 3: the nine regions, the boundaries in the middle row and column.
    const bool left = x < -hx;
    const bool right = x > hx;
    const bool below = y < -hy;
    const bool above = y > hy;
    if (left && !below && !above) {
        if (!capsuleBoxSideInterval(x, r, hx, depth)) {
            return false;
        }
        normal = normalW1(negate(ai));
    } else if (right && !below && !above) {
        if (!capsuleBoxSideInterval(-x, r, hx, depth)) {
            return false;
        }
        normal = normalW1(ai);
    } else if (!left && !right && below) {
        if (!capsuleBoxSideInterval(y, r, hy, depth)) {
            return false;
        }
        normal = normalW1(negate(aj));
    } else if (!left && !right && above) {
        if (!capsuleBoxSideInterval(-y, r, hy, depth)) {
            return false;
        }
        normal = normalW1(aj);
    } else if (left && above) {
        if (!capsuleBoxCornerCircle(x, -y, r, hx, hy, depth)) {
            return false;
        }
        cornerNormal(x + hx, y - hy);
    } else if (right && below) {
        if (!capsuleBoxCornerCircle(-x, y, r, hx, hy, depth)) {
            return false;
        }
        cornerNormal(x - hx, y + hy);
    } else if (right && above) {
        if (!capsuleBoxCornerCircle(-x, -y, r, hx, hy, depth)) {
            return false;
        }
        cornerNormal(x - hx, y - hy);
    } else if (left && below) {
        if (!capsuleBoxCornerCircle(x, y, r, hx, hy, depth)) {
            return false;
        }
        // The original builds this corner's normal from the opposite corner (+hx, +hy): skewed, still outward.
        cornerNormal(x - hx, y - hy);
    } else if (x < y) {
        // Over the box: the face by the signed coordinates, not the nearest one.
        normal = normalW1(y < 0.0F ? negate(aj) : aj);
        depth = hy - std::fabs(y) + r;
    } else {
        normal = normalW1(x < 0.0F ? negate(ai) : ai);
        depth = hx - std::fabs(x) + r;
    }
    contact.depth = depth;
    return true;
}

bool overlapBoxCapsule(Contact& contact, const Box& own, const Capsule& other) {
    if (!overlapCapsuleBox(contact, other, own)) {
        return false;
    }
    std::swap(contact.halves[0], contact.halves[1]);
    // Coney's choice: the capsule-side normal, where the original leaves whatever the record held (nothing reads it).
    const Vec4& boxNormal = contact.halves[0].normal;
    contact.halves[1].normal = Vec4{-boxNormal.x, -boxNormal.y, -boxNormal.z, boxNormal.w};
    return true;
}

bool overlapSegmentSphere(Contact& /*contact*/, const Segment& own, const Sphere& other) {
    const Vec3 closest = segmentClosestPoint(own.start, anim::subtract(own.end, own.start), other.centre);
    const Vec3 d = anim::subtract(closest, other.centre);
    return anim::dot(d, d) <= sq(own.radius + other.radius);
}

bool overlapSphereSegment(Contact& contact, const Sphere& own, const Segment& other) {
    return overlapSegmentSphere(contact, other, own);
}

bool overlapCapsuleSphere(Contact& /*contact*/, const Capsule& own, const Sphere& other) {
    const Vec3 closest = segmentClosestPoint(own.base, capsuleAxis(own), other.centre);
    const Vec3 d = anim::subtract(closest, other.centre);
    return anim::dot(d, d) <= sq(own.radius + other.radius);
}

bool overlapSphereCapsule(Contact& contact, const Sphere& own, const Capsule& other) {
    return overlapCapsuleSphere(contact, other, own);
}

bool overlapSegmentCapsule(Contact& /*contact*/, const Segment& own, const Capsule& other) {
    return segmentsWithin(own.start, anim::subtract(own.end, own.start), other.base, capsuleAxis(other),
                          own.radius + other.radius);
}

bool overlapCapsuleSegment(Contact& contact, const Capsule& own, const Segment& other) {
    return overlapSegmentCapsule(contact, other, own);
}

namespace {

// The overlap table's entries (docs/research/physics.md#dispatch), one overload per filled pair.
bool tableEntry(Contact& c, const Box& a, const Box& b) { return overlapBoxBox(c, a, b); }
bool tableEntry(Contact& c, const Box& a, const Sphere& b) { return overlapBoxSphere(c, a, b); }
bool tableEntry(Contact& c, const Box& a, const Capsule& b) { return overlapBoxCapsule(c, a, b); }
bool tableEntry(Contact& c, const Sphere& a, const Box& b) { return overlapSphereBox(c, a, b); }
bool tableEntry(Contact& c, const Sphere& a, const Sphere& b) { return overlapSphereSphere(c, a, b); }
bool tableEntry(Contact& c, const Sphere& a, const Capsule& b) { return overlapSphereCapsule(c, a, b); }
bool tableEntry(Contact& c, const Sphere& a, const Segment& b) { return overlapSphereSegment(c, a, b); }
bool tableEntry(Contact& c, const Capsule& a, const Box& b) { return overlapCapsuleBox(c, a, b); }
bool tableEntry(Contact& c, const Capsule& a, const Sphere& b) { return overlapCapsuleSphere(c, a, b); }
bool tableEntry(Contact& c, const Capsule& a, const Capsule& b) { return overlapCapsuleCapsule(c, a, b); }
bool tableEntry(Contact& c, const Capsule& a, const Segment& b) { return overlapCapsuleSegment(c, a, b); }
bool tableEntry(Contact& c, const Segment& a, const Sphere& b) { return overlapSegmentSphere(c, a, b); }
bool tableEntry(Contact& c, const Segment& a, const Capsule& b) { return overlapSegmentCapsule(c, a, b); }
bool tableEntry(Contact& c, const Segment& a, const Segment& b) { return overlapSegmentSegment(c, a, b); }

// The two empty entries, a segment and a box either way round: never an overlap.
bool tableEntry(Contact& /*c*/, const Segment& /*a*/, const Box& /*b*/) { return false; }
bool tableEntry(Contact& /*c*/, const Box& /*a*/, const Segment& /*b*/) { return false; }

} // namespace

bool overlapShapes(Contact& contact, const Shape& own, const Shape& other) {
    return std::visit([&contact](const auto& a, const auto& b) { return tableEntry(contact, a, b); }, own, other);
}

} // namespace coney::physics
