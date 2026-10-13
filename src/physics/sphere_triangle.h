// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "animation/anim_math.h"

// A moving sphere against one triangle of the level, and the working triangle every mesh test fills: the kernels the
// physics world's mesh sweeps call for each triangle they keep. Each function follows the original's reading exactly,
// quirks included (the capsule's root order that reports some corner contacts late or not at all), so a sphere meets
// the level where and when the original's does.
// Research: docs/research/physics-kernels.md#unit-1

namespace coney::physics {

/// A sphere: its centre and radius (the original's `{centre, radius at +0x10}` record).
struct Sphere {
    anim::Vec3 centre;
    float radius = 0.0F;
};

/// The working triangle a mesh test fills for the triangle under test
/// (docs/research/physics-kernels.md#u1-working-triangle). Edge `k` runs from corner `k` to corner `k + 1` (mod 3).
struct WorkingTriangle {
    std::array<anim::Vec3, 3> corners{}; ///< `+0x00`, `+0x10`, `+0x20`: v0, v1, v2.
    std::array<anim::Vec3, 3> edges{};   ///< `+0x30`-`+0x50`: e0 = v1 − v0, e1 = v2 − v1, e2 = v0 − v2.
    std::array<float, 3> lengths{};      ///< `+0x60`-`+0x68`: the edge lengths, written only by normaliseEdges().
    anim::Vec3 normal;                   ///< `+0x70`: the unit normal (zero for a degenerate triangle).
    float plane = 0.0F;                  ///< `+0x80`: the plane constant `d = v0 · n`, set by the caller.
};

/// Writes the three edges from the corners; with `normalise`, then makes them unit (normaliseEdges()). Both mesh sweeps
/// that call the sphere kernel pass false, so the kernel sees the real edges.
/// @orig 0x00343a90 CollTri_ComputeEdges (unknown)
void computeEdges(WorkingTriangle& triangle, bool normalise);

/// Divides each edge by its length and keeps the lengths (the original's name, "ComputeEdgeNormals", is wrong). A
/// zero-length edge stays zero, as `0 × (1/0)` does on the PS2.
/// @orig 0x00343b20 CollTri_ComputeEdgeNormals (unknown)
void normaliseEdges(WorkingTriangle& triangle);

/// Writes the unit normal `unit((v1 − v0) × (v2 − v0))`; zero for a degenerate triangle, as the PS2's `rsqrt(0)` gives
/// (an IEEE division would give NaN).
/// @orig 0x00343c30 CollTri_ComputeNormal (unknown)
void computeNormal(WorkingTriangle& triangle);

/// Turns the triangle round: swaps corners 1 and 2 and negates the normal. The edges and the plane constant are not
/// recomputed (the callers flip before computing them).
/// @orig 0x00343c90 CollTri_Flip (unknown)
void flip(WorkingTriangle& triangle);

/// What Sweep_SphereTriangle hands back.
struct TriangleSweep {
    bool hit = false;    ///< A fraction was recorded (it may lie outside 0 to 1, which the callers drop).
    float fraction = 0;  ///< The fraction of the whole move at which the sphere first touches, when `hit`.
    bool masked = false; ///< The inside mask was written (not on the early "no hit" returns of the plane step).
    unsigned mask = 0;   ///< Bit k set when the test point lies inside or on edge k; 7 is the face.
};

/// A sphere starting at `sphere` and moving by `move` (the whole displacement) against `triangle`, whose edges, normal
/// and plane constant are filled. The test point is the centre when the sphere already reaches the plane, else where it
/// first touches the plane (no hit when it moves away from it or reaches it past the move); the point inside all edges
/// is a face hit, outside one edge sweeps that edge as a capsule, outside two sweeps one edge of their corner. The edge
/// and corner steps get the original sphere and the whole move, so their fraction is of the whole move and is not
/// range-checked. A degenerate triangle (zero normal) is never hit (unreachable in the original, whose callers skip
/// it).
/// @orig 0x0034ee60 Sweep_SphereTriangle (unknown)
/// @orig 0x0034ee40 Sweep_SphereTriangleThunk (unknown)
[[nodiscard]] TriangleSweep sweepSphereTriangle(const Sphere& sphere, const WorkingTriangle& triangle, anim::Vec3 move);

/// Up to two points a segment or capsule step writes: the count and the points.
struct EdgePoints {
    int count = 0;
    std::array<anim::Vec3, 2> points{};
};

/// Whether the segment `a + u·e` (u from 0 to 1) meets the closed ball at `centre` of `radius`, with the points the
/// original writes: both crossings inside the segment, else the one inside it, and the far crossing **even past the
/// segment's end** when the segment starts inside the ball.
/// @orig 0x0034e120 Segment_SphereIntersect (unknown)
[[nodiscard]] bool segmentSphereIntersect(anim::Vec3 a, anim::Vec3 e, anim::Vec3 centre, float radius, EdgePoints& out);

/// A sphere at `centre` of `radius` moving by `move` against the capsule of that radius round the segment `a` to
/// `a + e`: the roots as fractions of `move`, in the original's order (the side wall's, then the sphere at `a`, then
/// the sphere at `a + e`, each kept only within its part of the axis, stopping at two), which is not the order of time.
/// A move parallel to the axis (or none) meets only the end spheres. Returns the number of roots written to `roots`.
/// The original's name ("Ray_InfiniteCylinder") is wrong: it is a capsule.
/// @orig 0x0034d938 Ray_InfiniteCylinder (unknown)
[[nodiscard]] int sweepSphereCapsule(float radius, anim::Vec3 centre, anim::Vec3 move, anim::Vec3 a, anim::Vec3 e,
                                     std::array<float, 2>& roots);

/// The capsule step of an edge: sweepSphereCapsule()'s roots, with the centre at each root from 0 to 1 (inclusive)
/// written to `out` in list order. `roots` is left as the capsule wrote it, not compacted or sorted. True when a root
/// lies in 0 to 1.
/// @orig 0x0034dff8 SphereSweep_EdgeCylinder (unknown)
[[nodiscard]] bool sphereSweepEdgeCylinder(float radius, anim::Vec3 centre, anim::Vec3 move, anim::Vec3 a, anim::Vec3 e,
                                           EdgePoints& out, std::array<float, 2>& roots);

/// The sphere against the edge `a` to `a + e`: fraction 0 when the segment already meets the ball, else the capsule's
/// **first listed** root (not the smallest, possibly outside 0 to 1) when any of its roots lies in 0 to 1, with the
/// contact point clamped onto the segment. Returns whether `fraction` was written.
/// @orig 0x0034e3b8 SphereSweep_Edge (unknown)
[[nodiscard]] bool sphereSweepEdge(anim::Vec3 a, anim::Vec3 e, const Sphere& sphere, anim::Vec3 move, float& fraction,
                                   EdgePoints& out);

/// The sphere past corner `k` of `triangle`: no contact when the corner is already inside the ball (`fraction` is then
/// set to 0 all the same) or the move does not go toward it; otherwise one edge of the corner, chosen by where the
/// centre's path crosses edge k's side plane (`sideNormal` = `e_k × n`, `edge` = e_k), swept by sphereSweepEdge().
/// @orig 0x0034e5d0 SphereSweep_VertexRegion (unknown)
[[nodiscard]] bool sphereSweepVertexRegion(int k, const WorkingTriangle& triangle, anim::Vec3 sideNormal,
                                           anim::Vec3 edge, const Sphere& sphere, anim::Vec3 move, float& fraction,
                                           EdgePoints& out);

/// The closest point of the triangle `corners` to `point`, as `v0 + s·(v1 − v0) + t·(v2 − v0)`: returns the squared
/// distance and writes `s` and `t` when given (Eberly's region method, followed exactly).
/// @orig 0x0034e840 PointTriangle_DistanceSq (unknown)
[[nodiscard]] float pointTriangleDistanceSq(anim::Vec3 point, const std::array<anim::Vec3, 3>& corners,
                                            float* s = nullptr, float* t = nullptr);

/// Whether `sphere` overlaps the triangle (closest point strictly inside the radius), with the depth `r − distance`;
/// `depth` is left alone when it does not.
/// @orig 0x0034edd0 SphereTriangle_Penetration (unknown)
[[nodiscard]] bool sphereTrianglePenetration(const Sphere& sphere, const std::array<anim::Vec3, 3>& corners,
                                             float& depth);

} // namespace coney::physics
