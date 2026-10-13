// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/sphere_triangle.h"

#include <cmath>
#include <cstddef>
#include <utility>

namespace coney::physics {

namespace {

using anim::Vec3;

// The arithmetic the kernels need, with the dot product summed as the original's VU0 sums it ((x + y) + z).
Vec3 operator+(Vec3 a, Vec3 b) { return anim::add(a, b); }
Vec3 operator-(Vec3 a, Vec3 b) { return anim::subtract(a, b); }
Vec3 operator*(Vec3 a, float s) { return anim::scale(a, s); }
float dot3(Vec3 a, Vec3 b) { return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z); }

// A move must be shorter than this for the capsule to treat it as no move (`0x358637bd`).
constexpr float kTinyMove = 1e-6F;
// An axis-relative direction with |z| at least this is parallel to the capsule's axis (`0x3f7fffef`).
constexpr float kParallel = 0.99999899F;

// The inside-mask bits of the three edges.
constexpr unsigned kInsideAll = 7;

// The two unit vectors across the axis `w`, as Vec_MakePerpendicularBasis (`0x00349180`) builds them. Only the
// squared distance across the axis is used, so any orthonormal pair gives the same roots up to rounding.
void perpendicularBasis(Vec3 w, Vec3& u, Vec3& v) {
    if (std::fabs(w.y) <= std::fabs(w.x)) {
        u = Vec3{-w.z, 0.0F, w.x} * (1.0F / std::sqrt((w.x * w.x) + (w.z * w.z)));
    } else {
        u = Vec3{0.0F, w.z, -w.y} * (1.0F / std::sqrt((w.y * w.y) + (w.z * w.z)));
    }
    v = anim::cross(w, u);
}

// Appends `root` to the capsule's list; true once the list holds two (the capsule then returns at once).
bool keepRoot(std::array<float, 2>& roots, int& count, float root) {
    roots.at(static_cast<std::size_t>(count)) = root;
    ++count;
    return count == 2;
}

// One end sphere of the capsule: its roots along the unit direction from `b` (the start's offset from the sphere's
// centre dotted with the direction) and `c` (that offset squared less r²), each kept when `keep` accepts its axial
// position, as fractions of the move (× `k`). Returns true once two roots are kept.
template <typename Keep>
bool capRoots(float b, float c, float pz, float dz, float k, Keep keep, std::array<float, 2>& roots, int& count) {
    const float discriminant = (b * b) - c;
    if (discriminant > 0.0F) {
        const float q = std::sqrt(discriminant);
        for (const float tau : {-b - q, -b + q}) {
            if (keep(pz + (tau * dz)) && keepRoot(roots, count, tau * k)) {
                return true;
            }
        }
    } else if (discriminant == 0.0F) {
        const float tau = -b;
        if (keep(pz + (tau * dz)) && keepRoot(roots, count, tau * k)) {
            return true;
        }
    }
    return false;
}

// The Eberly edge rule along `s` with `t` = 0 (or the same along `t` with `s` = 0): the closest parameter on the edge
// from the corner, given that edge's `d` (or `e`) and `a` (or `c`), and the squared distance with `f`.
float edgeRule(float d, float a, float f, float& param) {
    if (d >= 0.0F) {
        param = 0.0F;
        return f;
    }
    if (-d >= a) {
        param = 1.0F;
        return a + (2.0F * d) + f;
    }
    param = -d / a;
    return (d * param) + f;
}

} // namespace

void computeEdges(WorkingTriangle& triangle, bool normalise) {
    for (std::size_t k = 0; k < 3; ++k) {
        triangle.edges.at(k) = triangle.corners.at((k + 1) % 3) - triangle.corners.at(k);
    }
    if (normalise) {
        normaliseEdges(triangle);
    }
}

void normaliseEdges(WorkingTriangle& triangle) {
    for (std::size_t k = 0; k < 3; ++k) {
        Vec3& edge = triangle.edges.at(k);
        const float length = std::sqrt(dot3(edge, edge));
        triangle.lengths.at(k) = length;
        // A zero edge stays zero, as 0 × (1/0) = 0 on the PS2.
        if (length > 0.0F) {
            edge = edge * (1.0F / length);
        }
    }
}

void computeNormal(WorkingTriangle& triangle) {
    const Vec3 c = anim::cross(triangle.corners[1] - triangle.corners[0], triangle.corners[2] - triangle.corners[0]);
    const float lengthSq = dot3(c, c);
    // The PS2's rsqrt(0) gives the largest float and 0 × that is 0; an IEEE division would give NaN.
    triangle.normal = lengthSq > 0.0F ? c * (1.0F / std::sqrt(lengthSq)) : Vec3{};
}

void flip(WorkingTriangle& triangle) {
    std::swap(triangle.corners[1], triangle.corners[2]);
    triangle.normal = triangle.normal * -1.0F;
}

TriangleSweep sweepSphereTriangle(const Sphere& sphere, const WorkingTriangle& triangle, Vec3 move) {
    TriangleSweep result;
    const Vec3 n = triangle.normal;
    const Vec3 c = sphere.centre;
    const float r = sphere.radius;
    const float d = triangle.plane;
    // 1. The start's distance from the plane. A degenerate triangle (n = 0) would hit any sphere at 0 in the
    // original; its callers never pass one, and here it is refused outright.
    const float nn = dot3(n, n);
    if (nn == 0.0F) {
        return result;
    }
    const float nc = dot3(n, c);
    const float dist = nc - d;
    // 2-3. The test point: the centre when the sphere already reaches the plane, else where it first touches it.
    Vec3 p = c;
    float base = 0.0F;
    const bool reaching = (dist * dist) <= ((nn * r) * r);
    if (!reaching) {
        const float nm = dot3(n, move);
        Vec3 leading;
        if (d < nc) {
            if (!(nm < 0.0F)) {
                return result;
            }
            leading = c + (n * -r);
        } else {
            if (!(nm > 0.0F)) {
                return result;
            }
            leading = c + (n * r);
        }
        const float t = (d - dot3(n, leading)) / nm;
        if (t > 1.0F) {
            return result;
        }
        base = t;
        p = leading + (move * t);
    }
    // 4. The inside mask: bit k when the point is inside or on edge k (its outward side normal m_k = e_k × n).
    std::array<Vec3, 3> sides{};
    unsigned mask = 0;
    for (std::size_t k = 0; k < 3; ++k) {
        sides.at(k) = anim::cross(triangle.edges.at(k), n);
        if (dot3(sides.at(k), p) <= dot3(sides.at(k), triangle.corners.at(k))) {
            mask |= 1U << k;
        }
    }
    result.masked = true;
    result.mask = mask;
    // 5. The dispatch: the face, one edge, or a corner's region; the steps get the original sphere and whole move.
    EdgePoints points;
    float fraction = 0.0F;
    bool hit = false;
    switch (mask) {
    case kInsideAll:
        fraction = reaching ? 0.0F : base;
        hit = true;
        break;
    case 6:
        hit = sphereSweepEdge(triangle.corners[0], triangle.edges[0], sphere, move, fraction, points);
        break;
    case 5:
        hit = sphereSweepEdge(triangle.corners[1], triangle.edges[1], sphere, move, fraction, points);
        break;
    case 3:
        hit = sphereSweepEdge(triangle.corners[2], triangle.edges[2], sphere, move, fraction, points);
        break;
    case 2:
        hit = sphereSweepVertexRegion(0, triangle, sides[0], triangle.edges[0], sphere, move, fraction, points);
        break;
    case 4:
        hit = sphereSweepVertexRegion(1, triangle, sides[1], triangle.edges[1], sphere, move, fraction, points);
        break;
    case 1:
        hit = sphereSweepVertexRegion(2, triangle, sides[2], triangle.edges[2], sphere, move, fraction, points);
        break;
    default: // 0: outside every edge, only a degenerate triangle
        break;
    }
    if (hit) {
        result.hit = true;
        result.fraction = fraction;
    }
    return result;
}

bool segmentSphereIntersect(Vec3 a, Vec3 e, Vec3 centre, float radius, EdgePoints& out) {
    const Vec3 d = a - centre;
    const float c = dot3(d, d) - (radius * radius);
    const float b = dot3(d, e);
    const float aa = dot3(e, e);
    const float discriminant = (b * b) - (aa * c);
    out.count = 0;
    if (discriminant < 0.0F) {
        return false;
    }
    if (discriminant == 0.0F) {
        // A zero-length edge comes here with 0/0, which is a huge negative on the PS2: no contact.
        if (aa == 0.0F) {
            return false;
        }
        const float u = -b / aa;
        if (u < 0.0F || u > 1.0F) {
            return false;
        }
        out.count = 1;
        out.points[0] = a + (e * u);
        return true;
    }
    const float s = std::sqrt(discriminant);
    const float inverse = 1.0F / aa;
    const float u1 = (-b - s) * inverse;
    const float u2 = (-b + s) * inverse;
    if (u1 > 1.0F || u2 < 0.0F) {
        return false;
    }
    if (u1 >= 0.0F) {
        out.points[0] = a + (e * u1);
        out.count = 1;
        if (u2 <= 1.0F) {
            out.points[1] = a + (e * u2);
            out.count = 2;
        }
        return true;
    }
    // The segment starts inside the ball: the far crossing, even when it lies past the segment's end.
    out.count = 1;
    out.points[0] = a + (e * u2);
    return true;
}

int sweepSphereCapsule(float radius, Vec3 centre, Vec3 move, Vec3 a, Vec3 e, std::array<float, 2>& roots) {
    int count = 0;
    const float rr = radius * radius;
    const float length = std::sqrt(dot3(e, e));
    const Vec3 delta = centre - a;
    if (length == 0.0F) {
        // A zero-length edge behaves as the sphere at `a` on the PS2 (1/0 makes the axis zero; inferred). The mesh
        // skips degenerate triangles before this.
        const float m = std::sqrt(dot3(move, move));
        if (m < kTinyMove) {
            return 0;
        }
        const float k = 1.0F / m;
        const Vec3 dir = move * k;
        capRoots(dot3(delta, dir), dot3(delta, delta) - rr, 0.0F, 0.0F, k, [](float) { return true; }, roots, count);
        return count;
    }
    // The capsule's frame: w along the axis, u and v across it.
    const Vec3 w = e * (1.0F / length);
    Vec3 u;
    Vec3 v;
    perpendicularBasis(w, u, v);
    const Vec3 p{dot3(u, delta), dot3(v, delta), dot3(w, delta)};
    const Vec3 local{dot3(u, move), dot3(v, move), dot3(w, move)};
    const float m = std::sqrt(dot3(local, local));
    const float k = 1.0F / m;
    // A move along the axis (or none) meets only the end spheres, entry and exit along the axis.
    const bool tiny = m < kTinyMove;
    const Vec3 dir = tiny ? Vec3{} : local * k;
    if (tiny || std::fabs(dir.z) >= kParallel) {
        const float rho = (rr - (p.x * p.x)) - (p.y * p.y);
        const float me = dot3(move, e);
        if (rho < 0.0F || me == 0.0F) {
            return 0;
        }
        const float s = std::sqrt(rho);
        if (me < 0.0F) {
            roots[0] = (p.z + s) * k;
            roots[1] = -((length - p.z) + s) * k;
        } else {
            roots[0] = -(p.z + s) * k;
            roots[1] = ((length - p.z) + s) * k;
        }
        return 2;
    }
    // 1. The side wall, kept within the axis's length.
    const float a0 = ((p.x * p.x) + (p.y * p.y)) - rr;
    const float a1 = (p.x * dir.x) + (p.y * dir.y);
    const float a2 = (dir.x * dir.x) + (dir.y * dir.y);
    const float wall = (a1 * a1) - (a2 * a0);
    if (wall < 0.0F) {
        return 0; // a line that misses the infinite cylinder misses the capsule too
    }
    const auto onWall = [&](float tau) {
        const float axial = p.z + (tau * dir.z);
        return axial >= 0.0F && axial <= length;
    };
    if (wall > 0.0F) {
        const float q = std::sqrt(wall);
        const float inverse = 1.0F / a2;
        for (const float tau : {(-a1 - q) * inverse, (-a1 + q) * inverse}) {
            if (onWall(tau) && keepRoot(roots, count, tau * k)) {
                return count;
            }
        }
    } else {
        const float tau = -a1 / a2;
        if (onWall(tau)) {
            keepRoot(roots, count, tau * k);
            return count;
        }
    }
    // 2. The sphere at a, kept before the axis's start.
    const float b0 = a0 + (p.z * p.z);
    const float b1 = a1 + (p.z * dir.z);
    if (capRoots(b1, b0, p.z, dir.z, k, [](float axial) { return axial <= 0.0F; }, roots, count)) {
        return count;
    }
    // 3. The sphere at a + e, kept past the axis's end.
    const float c1 = b1 - (dir.z * length);
    const float c0 = b0 + (length * (length - (2.0F * p.z)));
    capRoots(c1, c0, p.z, dir.z, k, [length](float axial) { return axial >= length; }, roots, count);
    return count;
}

bool sphereSweepEdgeCylinder(float radius, Vec3 centre, Vec3 move, Vec3 a, Vec3 e, EdgePoints& out,
                             std::array<float, 2>& roots) {
    const int found = sweepSphereCapsule(radius, centre, move, a, e, roots);
    out.count = 0;
    for (int i = 0; i < found; ++i) {
        const float root = roots.at(static_cast<std::size_t>(i));
        if (root >= 0.0F && root <= 1.0F) {
            out.points.at(static_cast<std::size_t>(out.count)) = centre + (move * root);
            ++out.count;
        }
    }
    return out.count > 0;
}

bool sphereSweepEdge(Vec3 a, Vec3 e, const Sphere& sphere, Vec3 move, float& fraction, EdgePoints& out) {
    // 1. The segment already meets the ball: a contact at 0.
    if (segmentSphereIntersect(a, e, sphere.centre, sphere.radius, out)) {
        fraction = 0.0F;
        return true;
    }
    // 2. The capsule step; the first listed root is reported, whatever its value.
    std::array<float, 2> roots{};
    if (!sphereSweepEdgeCylinder(sphere.radius, sphere.centre, move, a, e, out, roots)) {
        return false;
    }
    fraction = roots[0];
    // 3. The contact point: the centre at that fraction, clamped onto the segment by projection.
    const Vec3 at = sphere.centre + (move * roots[0]);
    const float ea = dot3(e, a);
    const float eb = ea + dot3(e, e);
    const float ep = dot3(e, at);
    Vec3 point;
    if (ep < ea) {
        point = a;
    } else if (eb < ep) {
        point = a + e;
    } else {
        point = a + (e * ((ep - ea) / (eb - ea)));
    }
    out.points[0] = point;
    out.count = 1;
    return true;
}

bool sphereSweepVertexRegion(int k, const WorkingTriangle& triangle, Vec3 sideNormal, Vec3 edge, const Sphere& sphere,
                             Vec3 move, float& fraction, EdgePoints& out) {
    const auto corner = [&triangle](int i) { return triangle.corners.at(static_cast<std::size_t>((i + 3) % 3)); };
    const Vec3 vertex = corner(k);
    const Vec3 c = sphere.centre;
    const Vec3 toCorner = vertex - c;
    // 1-2. A corner already inside the ball, or a move that does not go toward it: no contact.
    if (dot3(toCorner, toCorner) < sphere.radius * sphere.radius) {
        fraction = 0.0F;
        return false;
    }
    if (dot3(move, toCorner) <= 0.0F) {
        return false;
    }
    // 3. Which edge: where the centre's path crosses edge k's side plane, along edge k past the corner or not.
    const float numerator = dot3(sideNormal, vertex) - dot3(sideNormal, c);
    const float denominator = dot3(sideNormal, move);
    bool edgeK = false;
    if (denominator != 0.0F) {
        const Vec3 crossing = c + (move * (numerator / denominator));
        edgeK = dot3(edge, vertex) <= dot3(edge, crossing);
    } else {
        // The move is parallel to the side plane: the PS2's ±FLT_MAX crossing decides by signs (inferred, the page's
        // faithful IEEE rule).
        const float along = dot3(edge, move);
        if (along == 0.0F) {
            edgeK = dot3(edge, vertex) <= dot3(edge, c);
        } else if (numerator != 0.0F) {
            edgeK = (numerator > 0.0F) == (along > 0.0F);
        }
    }
    const Vec3 a = edgeK ? vertex : corner(k - 1);
    const Vec3 e = edgeK ? corner(k + 1) - vertex : vertex - a;
    // 4. Only that one edge; the corner is its end cap.
    return sphereSweepEdge(a, e, sphere, move, fraction, out);
}

float pointTriangleDistanceSq(Vec3 point, const std::array<Vec3, 3>& corners, float* sOut, float* tOut) {
    const Vec3 e0 = corners[1] - corners[0];
    const Vec3 e1 = corners[2] - corners[0];
    const Vec3 diff = corners[0] - point;
    const float a = dot3(e0, e0);
    const float b = dot3(e0, e1);
    const float c = dot3(e1, e1);
    const float d = dot3(e0, diff);
    const float e = dot3(e1, diff);
    const float f = dot3(diff, diff);
    const float det = std::fabs((a * c) - (b * b));
    float s = (b * e) - (c * d);
    float t = (b * d) - (a * e);
    float q = 0.0F;
    // The squared distance at (s, t) inside the triangle, in the original's order of sums.
    const auto inside = [&](float ss, float tt) {
        return (ss * (((a * ss) + (b * tt)) + (2.0F * d))) + (tt * (((b * ss) + (c * tt)) + (2.0F * e))) + f;
    };
    // Along the far edge from (s, t) = (1, 0) toward (0, 1), clamped: `num` against `den` = (a − 2b) + c.
    const auto farEdge = [&](float num, bool fromS) {
        const float den = (a - (2.0F * b)) + c;
        if (num >= den) {
            s = fromS ? 1.0F : 0.0F;
            t = fromS ? 0.0F : 1.0F;
            return fromS ? (a + (2.0F * d) + f) : (c + (2.0F * e) + f);
        }
        const float share = num / den;
        s = fromS ? share : 1.0F - share;
        t = fromS ? 1.0F - share : share;
        return inside(s, t);
    };
    if (s + t <= det) {
        if (s < 0.0F) {
            if (t < 0.0F) { // region 4
                if (d < 0.0F) {
                    t = 0.0F;
                    q = edgeRule(d, a, f, s);
                } else {
                    s = 0.0F;
                    q = edgeRule(e, c, f, t);
                }
            } else { // region 3
                s = 0.0F;
                q = edgeRule(e, c, f, t);
            }
        } else if (t < 0.0F) { // region 5
            t = 0.0F;
            q = edgeRule(d, a, f, s);
        } else { // region 0
            const float inverse = 1.0F / det;
            s *= inverse;
            t *= inverse;
            q = inside(s, t);
        }
    } else if (s < 0.0F) { // region 2
        const float t0 = b + d;
        const float t1 = c + e;
        if (t0 < t1) {
            q = farEdge(t1 - t0, true);
        } else {
            s = 0.0F;
            if (t1 <= 0.0F) {
                t = 1.0F;
                q = c + (2.0F * e) + f;
            } else if (e >= 0.0F) {
                t = 0.0F;
                q = f;
            } else {
                t = -e / c;
                q = (e * t) + f;
            }
        }
    } else if (t < 0.0F) { // region 6
        const float t0 = b + e;
        const float t1 = a + d;
        if (t0 < t1) {
            q = farEdge(t1 - t0, false);
        } else {
            t = 0.0F;
            if (t1 <= 0.0F) {
                s = 1.0F;
                q = a + (2.0F * d) + f;
            } else if (d >= 0.0F) {
                s = 0.0F;
                q = f;
            } else {
                s = -d / a;
                q = (d * s) + f;
            }
        }
    } else { // region 1
        const float num = ((c + e) - b) - d;
        if (num <= 0.0F) {
            s = 0.0F;
            t = 1.0F;
            q = c + (2.0F * e) + f;
        } else {
            q = farEdge(num, true);
        }
    }
    if (sOut != nullptr) {
        *sOut = s;
    }
    if (tOut != nullptr) {
        *tOut = t;
    }
    return std::fabs(q);
}

bool sphereTrianglePenetration(const Sphere& sphere, const std::array<Vec3, 3>& corners, float& depth) {
    const float q = pointTriangleDistanceSq(sphere.centre, corners);
    if (!(q < sphere.radius * sphere.radius)) {
        return false;
    }
    depth = sphere.radius - std::sqrt(q);
    return true;
}

} // namespace coney::physics
