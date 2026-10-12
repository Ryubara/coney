// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/sphere_box_sweep.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/shapes.h"

namespace coney::physics {

namespace {

using anim::Vec3;

// What a step answers: a hit, no hit, or "already touching" (which the kernel treats as no hit).
enum class Step : std::uint8_t { Miss, Hit, Touching };

// Three floats in box-axis order (x, y, z of the box frame).
using Local = std::array<float, 3>;

// A step's view of the box: which box axis each of its three axes is, so one step serves every region.
using Order = std::array<int, 3>;

// The folded problem every step works on: half extents, start centre and move along the box's axes, each axis turned
// so the start lies on its positive side; then the step's answer.
struct Folded {
    Local half{};
    Local start{};
    Local move{};
    float radius = 0.0F;
    float fraction = 0.0F;
    Local point{};
};

// The original's region normals in the box frame (docs/research/physics.md#the-normal-tables). Six of them are not
// the true outward normal of the region whose index picks them (the edges along y, corners 1, 3, 5 and 7): kept as
// the original has them, since no strike reads them.
constexpr float kA = 0.70710677F;
constexpr float kB = 0.57735026F;
constexpr std::array<Vec3, 6> kFaceNormals{{{0, 1, 0}, {1, 0, 0}, {0, -1, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<Vec3, 12> kEdgeNormals{{{-kA, kA, 0},
                                             {kA, kA, 0},
                                             {kA, -kA, 0},
                                             {-kA, -kA, 0},
                                             {kA, 0, kA},
                                             {kA, 0, -kA},
                                             {-kA, 0, -kA},
                                             {-kA, 0, kA},
                                             {0, kA, kA},
                                             {0, kA, -kA},
                                             {0, -kA, -kA},
                                             {0, -kA, kA}}};
constexpr std::array<Vec3, 8> kCornerNormals{{{-kB, kB, kB},
                                              {-kB, kB, -kB},
                                              {kB, kB, -kB},
                                              {kB, kB, kB},
                                              {-kB, -kB, kB},
                                              {-kB, -kB, -kB},
                                              {kB, -kB, -kB},
                                              {kB, -kB, kB}}};

// Squares a float.
float sq(float v) { return v * v; }

// The 3D cross product's squared length |q × m|², for the "line misses the sphere" tests.
float crossLengthSq(Vec3 q, Vec3 m) {
    const Vec3 c = anim::cross(q, m);
    return anim::dot(c, c);
}

// The unit horizontal vector along `v` (z dropped), w 1; (0, 0, 0, 1) when `v` has no horizontal part, where the
// original normalises a zero vector into a meaningless value
// (docs/research/physics.md#u4b-sweep-sphereorientedbox-0034bbe8).
Vec4 horizontalUnit(Vec3 v) {
    const float lengthSq = sq(v.x) + sq(v.y);
    if (lengthSq <= 0.0F) {
        return Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    }
    const float inverse = 1.0F / std::sqrt(lengthSq);
    return Vec4{v.x * inverse, v.y * inverse, 0.0F, 1.0F};
}

Step faceStep(Folded& f, const Order& o, bool startCheck);

// One edge, its line along axis `q` at the far corners `h1` (axis `a1`) and `h2` (axis `a2`): the cylinder round the
// line, or the sphere at the end the move heads for when the centre passes that end still farther than r from the
// line. The caller has seen the path come within r of the line.
// @orig 0x0034b3b8 SphereBox_SweepEdge (unknown)
Step edgeStep(Folded& f, int a1, int q, int a2) {
    const float r = f.radius;
    const float h1 = f.half[a1];
    const float h2 = f.half[a2];
    const float b = f.half[q];
    const float e1 = f.start[a1] - h1;
    const float e2 = f.start[a2] - h2;
    const float m1 = f.move[a1];
    const float mq = f.move[q];
    const float m2 = f.move[a2];
    // Step 1: the end the move heads for, and the offsets from the line (times |mq|) where the centre crosses it.
    const float sq1 = mq >= 0.0F ? 1.0F : -1.0F;
    const float fq = mq >= 0.0F ? f.start[q] - b : f.start[q] + b;
    const float j1 = sq1 * (e1 * mq - fq * m1);
    const float j2 = sq1 * (e2 * mq - fq * m2);
    // Step 2: the end's corner sphere.
    if (j1 >= 0.0F && j2 >= 0.0F && sq(r) * sq(mq) < sq(j1) + sq(j2)) {
        const Vec3 qv{e1, fq, e2};
        const Vec3 m{m1, mq, m2};
        if (crossLengthSq(qv, m) > sq(r) * anim::dot(m, m)) {
            return Step::Miss;
        }
        f.fraction = raySphereRoot(qv, m, sq(r));
        f.point[a1] = h1;
        f.point[q] = sq1 * b;
        f.point[a2] = h2;
        return Step::Hit;
    }
    // Step 3: the cylinder; the point along the line is not clamped to the edge's length.
    f.fraction = rayCircleRoot(e1, e2, m1, m2, sq(m1) + sq(m2), sq(r));
    f.point[a1] = h1;
    f.point[q] = f.start[q] + f.fraction * mq;
    f.point[a2] = h2;
    return Step::Hit;
}

// The edge region: the start centre outside the slabs of axes o[0] (P) and o[2] (R), inside the slab of o[1] (Q, the
// axis along the edge). Picks the edge's cylinder or one of its two faces by where the path crosses the faces' planes.
// @orig 0x0034b600 SphereBox_SweepEdges (unknown)
Step edgeRegionStep(Folded& f, const Order& o, bool startCheck) {
    const int p = o[0];
    const int q = o[1];
    const int s = o[2];
    const float r = f.radius;
    const float ep = f.start[p] - f.half[p];
    const float es = f.start[s] - f.half[s];
    const float mp = f.move[p];
    const float ms = f.move[s];
    // Step 1: already within r of the edge line.
    if (startCheck && sq(ep) + sq(es) - sq(r) <= 0.0F) {
        f.fraction = 0.0F;
        return Step::Touching;
    }
    // Step 2: not approaching the line in the P-R plane.
    if (mp * ep + ms * es >= 0.0F) {
        return Step::Miss;
    }
    // Step 3: k / |mp| is the height over the R face where the centre crosses p = a; −k / |ms| how far out along P it
    // is where it crosses s = c.
    const float k = ms * ep - mp * es;
    if (k >= 0.0F) {
        // Step 4.
        if (mp >= 0.0F) {
            return Step::Miss;
        }
        if (k <= r * std::fabs(mp)) {
            return edgeStep(f, s, q, p);
        }
        return faceStep(f, Order{p, q, s}, false);
    }
    // Step 5.
    if (ms >= 0.0F) {
        return Step::Miss;
    }
    if (k >= r * ms) {
        return edgeStep(f, p, q, s);
    }
    return faceStep(f, Order{s, q, p}, false);
}

// The face region of axis o[2] (w): the start centre over the face (from the edge step, possibly past it), moving
// towards its plane. The face itself, one of its two edges as an infinite line, or the corner between them.
// @orig 0x0034b040 SphereBox_SweepCorner (unknown)
Step faceStep(Folded& f, const Order& o, bool startCheck) {
    const int iu = o[0];
    const int iv = o[1];
    const int iw = o[2];
    const float r = f.radius;
    const float a = f.half[iu];
    const float b = f.half[iv];
    const float c = f.half[iw];
    const float u = f.start[iu];
    const float v = f.start[iv];
    const float w = f.start[iw];
    const float mu = f.move[iu];
    const float mv = f.move[iv];
    const float mw = f.move[iw];
    // Step 1: already touching the face.
    if (startCheck && w <= c + r) {
        f.fraction = 0.0F;
        return Step::Touching;
    }
    // Step 2: not moving towards the face's plane.
    if (mw >= 0.0F) {
        return Step::Miss;
    }
    // Step 3: for each in-plane axis, the edge the move heads for, and k (≤ r·|m| exactly when the centre is still
    // over the face as the sphere reaches its plane).
    const float g = w - c;
    const float su = mu >= 0.0F ? 1.0F : -1.0F;
    const float ku = mu >= 0.0F ? mu * g - mw * (u - a) : mw * (u + a) - mu * g;
    const float sv = mv >= 0.0F ? 1.0F : -1.0F;
    const float kv = mv >= 0.0F ? mv * g - mw * (v - b) : mw * (v + b) - mv * g;
    const bool overU = ku <= r * std::fabs(mu);
    const bool overV = kv <= r * std::fabs(mv);
    // Step 4: the face.
    if (overU && overV) {
        f.fraction = (r - g) / mw;
        f.point[iu] = u + f.fraction * mu;
        f.point[iv] = v + f.fraction * mv;
        f.point[iw] = c;
        return Step::Hit;
    }
    // Step 5: the u edge, unless the path passes it farther than r.
    if (!overU && overV) {
        if (sq(ku) > sq(r) * (sq(mu) + sq(mw))) {
            return Step::Miss;
        }
        f.fraction = rayCircleRoot(u - su * a, g, mu, mw, sq(mu) + sq(mw), sq(r));
        f.point[iu] = su * a;
        f.point[iv] = v + f.fraction * mv;
        f.point[iw] = c;
        return Step::Hit;
    }
    // Step 6: the v edge.
    if (overU && !overV) {
        if (sq(kv) > sq(r) * (sq(mv) + sq(mw))) {
            return Step::Miss;
        }
        f.fraction = rayCircleRoot(v - sv * b, g, mv, mw, sq(mv) + sq(mw), sq(r));
        f.point[iu] = u + f.fraction * mu;
        f.point[iv] = sv * b;
        f.point[iw] = c;
        return Step::Hit;
    }
    // Step 7: the corner sphere; a path past both edges that misses it misses, without trying the edge cylinders.
    if (sq(ku) > sq(r) * (sq(mu) + sq(mw)) || sq(kv) > sq(r) * (sq(mv) + sq(mw))) {
        return Step::Miss;
    }
    const Vec3 q{u - su * a, v - sv * b, g};
    const Vec3 m{mu, mv, mw};
    if (crossLengthSq(q, m) > sq(r) * anim::dot(m, m)) {
        return Step::Miss;
    }
    f.fraction = raySphereRoot(q, m, sq(r));
    f.point[iu] = su * a;
    f.point[iv] = sv * b;
    f.point[iw] = c;
    return Step::Hit;
}

// The corner region: the start centre outside all three slabs. The corner's sphere when the path crosses one of the
// three faces' planes within r of the corner (the point is then the sphere's centre at contact, not the corner), else
// one of the three edges through it.
// @orig 0x0034b7b0 SphereBox_SweepFaceEdges (unknown)
Step cornerStep(Folded& f) {
    const float r = f.radius;
    const Vec3 e{f.start[0] - f.half[0], f.start[1] - f.half[1], f.start[2] - f.half[2]};
    const Vec3 m{f.move[0], f.move[1], f.move[2]};
    // Step 1: already within r of the corner.
    if (anim::dot(e, e) - sq(r) <= 0.0F) {
        f.fraction = 0.0F;
        return Step::Touching;
    }
    // Step 2: not approaching it.
    if (anim::dot(m, e) >= 0.0F) {
        return Step::Miss;
    }
    // Step 3: the offsets from the corner where the centre crosses each axis's plane, times that axis's move.
    const float g2 = m.x * e.z - m.z * e.x;
    const float g3 = m.y * e.z - m.z * e.y;
    const float g4 = m.y * e.x - m.x * e.y;
    // Step 4: the corner's sphere.
    const bool crossesX = g2 < 0.0F && g4 >= 0.0F;
    const bool crossesY = g4 < 0.0F && g3 < 0.0F;
    if ((crossesX && sq(g2) + sq(g4) <= sq(r) * sq(m.x)) || (crossesY && sq(g3) + sq(g4) <= sq(r) * sq(m.y)) ||
        (g2 >= 0.0F && g3 >= 0.0F && sq(g2) + sq(g3) <= sq(r) * sq(m.z))) {
        f.fraction = raySphereRoot(e, m, sq(r));
        for (int i = 0; i < 3; ++i) {
            f.point[i] = f.start[i] + f.fraction * f.move[i];
        }
        return Step::Hit;
    }
    // Step 5: one of the three edges through the corner.
    if (crossesX) {
        return edgeRegionStep(f, Order{1, 0, 2}, false);
    }
    if (crossesY) {
        return edgeRegionStep(f, Order{0, 1, 2}, false);
    }
    return edgeRegionStep(f, Order{0, 2, 1}, false);
}

// The feature index of a region from the start's signs (docs/research/physics.md#u4b-sweep-sphereorientedbox-0034bbe8).
int faceIndex(int axis, const Local& sign) {
    if (axis == 0) {
        return sign[0] > 0.0F ? 1 : 3;
    }
    if (axis == 1) {
        return sign[1] > 0.0F ? 0 : 2;
    }
    return sign[2] > 0.0F ? 4 : 5;
}

// The index of the edge along `axis`.
int edgeIndex(int axis, const Local& sign) {
    const bool s0 = sign[0] > 0.0F;
    const bool s1 = sign[1] > 0.0F;
    const bool s2 = sign[2] > 0.0F;
    if (axis == 2) {
        return s0 ? (s1 ? 1 : 2) : (s1 ? 0 : 3);
    }
    if (axis == 1) {
        return s0 ? (s2 ? 7 : 6) : (s2 ? 5 : 4);
    }
    return s1 ? (s2 ? 8 : 9) : (s2 ? 11 : 10);
}

// The index of the corner.
int cornerIndex(const Local& sign) {
    const bool s0 = sign[0] > 0.0F;
    const bool s1 = sign[1] > 0.0F;
    const bool s2 = sign[2] > 0.0F;
    if (s1) {
        return s2 ? (s0 ? 1 : 0) : (s0 ? 2 : 3);
    }
    return s2 ? (s0 ? 5 : 4) : (s0 ? 6 : 7);
}

// A box-frame normal from the original's tables turned into the world (w 0); any other type the constant (0, 1, 0, 1).
Vec4 featureNormal(const Box& box, int type, int index) {
    Vec3 n{};
    float w = 0.0F;
    if (type == kFeatureFace) {
        n = kFaceNormals.at(static_cast<std::size_t>(index));
    } else if (type == kFeatureEdge) {
        n = kEdgeNormals.at(static_cast<std::size_t>(index));
    } else if (type == kFeatureCorner) {
        n = kCornerNormals.at(static_cast<std::size_t>(index));
    } else {
        n = {0.0F, 1.0F, 0.0F};
        w = 1.0F;
    }
    const Vec3 world = anim::add(anim::add(anim::scale(box.axes[0], n.x), anim::scale(box.axes[1], n.y)),
                                 anim::scale(box.axes[2], n.z));
    return Vec4{world.x, world.y, world.z, w};
}

} // namespace

float raySphereRoot(Vec3 q, Vec3 m, float rr) {
    // C / (D − B) rather than the original's (C·s) / (1 − B·s) with s = 1 / D: the same root, and exact at D = 0
    // where the original divides by zero (docs/research/physics.md#u4b-ray-sphereroot-0034af50-and-ray-circlero).
    const float c = anim::dot(q, q) - rr;
    const float b = anim::dot(q, m);
    const float a = anim::dot(m, m);
    const float d = std::sqrt(std::fabs(b * b - a * c));
    return c / (d - b);
}

float rayCircleRoot(float qx, float qy, float mx, float my, float a, float rr) {
    const float c = qx * qx + qy * qy - rr;
    const float b = mx * qx + my * qy;
    const float d = std::sqrt(std::fabs(b * b - a * c));
    return c / (d - b);
}

std::optional<SphereBoxHit> sweepSphereOrientedBox(float radius, float maxFraction, const Box& box, Vec3 boxMove,
                                                   Vec3 centre, Vec3 sphereMove) {
    // Step 1: the start and the relative move along the box's axes, each axis folded so the start is on its positive
    // side; the region is which slabs the start centre lies in (the radius plays no part).
    const Vec3 offset = anim::subtract(centre, box.centre);
    const Vec3 relative = anim::subtract(sphereMove, boxMove);
    Folded f;
    f.radius = radius;
    Local sign{};
    std::array<bool, 3> within{};
    for (int i = 0; i < 3; ++i) {
        f.half[i] = box.half[i];
        f.start[i] = anim::dot(offset, box.axes[i]);
        f.move[i] = anim::dot(relative, box.axes[i]);
        sign[i] = 1.0F;
        if (f.start[i] < 0.0F) {
            sign[i] = -1.0F;
            f.start[i] = -f.start[i];
            f.move[i] = -f.move[i];
        }
        within[i] = f.start[i] <= f.half[i];
    }
    const int outside = (within[0] ? 0 : 1) + (within[1] ? 0 : 1) + (within[2] ? 0 : 1);

    // Step 2: a centre inside (or on) the box hits at once. The local point's third value is the world z, a quirk of
    // the original kept as it is.
    if (outside == 0) {
        SphereBoxHit hit;
        hit.featureType = kFeatureInside;
        hit.point = centre;
        for (int i = 0; i < 2; ++i) {
            const float along = anim::dot(centre, box.axes[i]) - anim::dot(box.centre, box.axes[i]);
            (i == 0 ? hit.localPoint.x : hit.localPoint.y) = std::clamp(along, -box.half[i], box.half[i]);
        }
        hit.localPoint.z = centre.z;
        hit.featureNormal = horizontalUnit(offset);
        const Vec4& n = hit.featureNormal;
        hit.directionToBox = Vec4{-n.x, -n.y, -n.z, 1.0F};
        return hit;
    }

    // Step 3: the region's step, with its feature named first.
    SphereBoxHit hit;
    Step step = Step::Miss;
    if (outside == 1) {
        const int axis = !within[2] ? 2 : (!within[1] ? 1 : 0);
        hit.featureType = kFeatureFace;
        hit.featureIndex = faceIndex(axis, sign);
        const Order order = axis == 2 ? Order{0, 1, 2} : (axis == 1 ? Order{0, 2, 1} : Order{1, 2, 0});
        step = faceStep(f, order, true);
    } else if (outside == 2) {
        const int along = within[0] ? 0 : (within[1] ? 1 : 2);
        hit.featureType = kFeatureEdge;
        hit.featureIndex = edgeIndex(along, sign);
        const Order order = along == 0 ? Order{1, 0, 2} : (along == 1 ? Order{0, 1, 2} : Order{0, 2, 1});
        step = edgeRegionStep(f, order, true);
    } else {
        hit.featureType = kFeatureCorner;
        hit.featureIndex = cornerIndex(sign);
        step = cornerStep(f);
    }

    // Step 4: only a hit within the move counts (a fraction equal to the limit hits).
    if (step != Step::Hit || f.fraction > maxFraction) {
        return std::nullopt;
    }

    // Step 5: the normal, the direction to the box, and the point unfolded into the box frame and the world.
    hit.fraction = f.fraction;
    hit.featureNormal = featureNormal(box, hit.featureType, hit.featureIndex);
    hit.directionToBox = horizontalUnit(anim::subtract(box.centre, centre));
    hit.localPoint = Vec3{sign[0] * f.point[0], sign[1] * f.point[1], sign[2] * f.point[2]};
    hit.point = anim::add(box.centre, anim::add(anim::add(anim::scale(box.axes[0], hit.localPoint.x),
                                                          anim::scale(box.axes[1], hit.localPoint.y)),
                                                anim::scale(box.axes[2], hit.localPoint.z)));
    return hit;
}

bool sweepSphereBox(Contact& contact, const Sphere& sphere, const Box& box, Vec3 move) {
    const auto hit = sweepSphereOrientedBox(sphere.radius, 1.0F, box, Vec3{}, sphere.centre, move);
    if (!hit) {
        return false;
    }
    ContactHalf& own = contact.halves[0];
    ContactHalf& other = contact.halves[1];
    contact.fraction = hit->fraction;
    other.featureIndex = hit->featureIndex;
    contact.point = Vec4{hit->point.x, hit->point.y, hit->point.z, 1.0F};
    // The local point's w is not written.
    other.localPoint.x = hit->localPoint.x;
    other.localPoint.y = hit->localPoint.y;
    other.localPoint.z = hit->localPoint.z;
    other.normal = hit->featureNormal;
    own.normal = hit->directionToBox;
    other.featureType = hit->featureType;
    contact.flags = 0;
    return true;
}

bool sweepBoxSphere(Contact& contact, const Box& box, const Sphere& sphere, Vec3 move) {
    const auto hit = sweepSphereOrientedBox(sphere.radius, 1.0F, box, move, sphere.centre, Vec3{});
    if (!hit) {
        return false;
    }
    ContactHalf& own = contact.halves[0];
    ContactHalf& other = contact.halves[1];
    contact.fraction = hit->fraction;
    own.featureIndex = hit->featureIndex;
    own.featureType = hit->featureType;
    contact.point = Vec4{hit->point.x, hit->point.y, hit->point.z, 1.0F};
    own.localPoint.x = hit->localPoint.x;
    own.localPoint.y = hit->localPoint.y;
    own.localPoint.z = hit->localPoint.z;
    own.normal = hit->featureNormal;
    other.normal = hit->directionToBox;
    contact.flags = 0;
    return true;
}

} // namespace coney::physics
