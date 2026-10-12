// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/sphere_triangle.h"

#include <array>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The numbers are the page's worked cases (docs/research/physics-kernels.md#u1-tests), from a double-precision model of
// the spec; 1e-5 is ample at these magnitudes.

using Catch::Approx;
using coney::anim::Vec3;
using coney::physics::EdgePoints;
using coney::physics::Sphere;
using coney::physics::WorkingTriangle;

namespace {

constexpr double kTolerance = 1e-5;

// The page's triangle T: v0 = 0, v1 = (4, 0, 0), v2 = (0, 4, 0), so n = (0, 0, 1) and d = 0, filled as the mesh
// sweeps fill it (edges not normalised).
WorkingTriangle triangleT() {
    WorkingTriangle t;
    t.corners = {Vec3{0, 0, 0}, Vec3{4, 0, 0}, Vec3{0, 4, 0}};
    coney::physics::computeEdges(t, false);
    coney::physics::computeNormal(t);
    t.plane = coney::anim::dot(t.corners[0], t.normal);
    return t;
}

// Checks two vectors match to the tolerance.
void checkVec(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == Approx(expected.x).margin(kTolerance));
    CHECK(actual.y == Approx(expected.y).margin(kTolerance));
    CHECK(actual.z == Approx(expected.z).margin(kTolerance));
}

} // namespace

TEST_CASE("the working triangle's edges, normalised edges, normal and flip", "[physics][sphere_triangle]") {
    WorkingTriangle t = triangleT();
    checkVec(t.edges[0], Vec3{4, 0, 0});
    checkVec(t.edges[1], Vec3{-4, 4, 0});
    checkVec(t.edges[2], Vec3{0, -4, 0});
    checkVec(t.normal, Vec3{0, 0, 1});

    WorkingTriangle unit = t;
    coney::physics::computeEdges(unit, true);
    checkVec(unit.edges[1], Vec3{-0.707107F, 0.707107F, 0});
    CHECK(unit.lengths[0] == Approx(4.0F));
    CHECK(unit.lengths[1] == Approx(5.656854F));
    CHECK(unit.lengths[2] == Approx(4.0F));

    // A flip swaps corners 1 and 2 and turns the normal; the edges stay until recomputed.
    WorkingTriangle flipped = t;
    coney::physics::flip(flipped);
    checkVec(flipped.corners[1], Vec3{0, 4, 0});
    checkVec(flipped.corners[2], Vec3{4, 0, 0});
    checkVec(flipped.normal, Vec3{0, 0, -1});
    checkVec(flipped.edges[0], Vec3{4, 0, 0});

    // A degenerate triangle's normal is zero, not NaN.
    WorkingTriangle line;
    line.corners = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}};
    coney::physics::computeNormal(line);
    checkVec(line.normal, Vec3{0, 0, 0});
}

TEST_CASE("a sphere swept against a triangle: the page's fourteen cases", "[physics][sphere_triangle]") {
    const WorkingTriangle t = triangleT();
    struct Case {
        Vec3 centre;
        Vec3 move;
        bool hit;
        float fraction;
        bool masked;
        unsigned mask;
    };
    const std::array<Case, 14> cases{{
        {Vec3{1, 1, 2}, Vec3{0, 0, -3}, true, 0.5F, true, 7},                           // 1 the face
        {Vec3{1, 1, 0.3F}, Vec3{5, 0, 0}, true, 0.0F, true, 7},                         // 2 already reaching
        {Vec3{2, -0.3F, 2}, Vec3{0, 0, -3}, true, 8.0F / 15.0F, true, 6},               // 3 edge 0
        {Vec3{2, -0.3F, 0.3F}, Vec3{0, 0, -1}, true, 0.0F, true, 6},                    // 4 the segment meets the ball
        {Vec3{1, 1, 2}, Vec3{0, 0, 1}, false, 0.0F, false, 0},                          // 5 moving away
        {Vec3{1, 1, 2}, Vec3{0, 0, -1}, false, 0.0F, false, 0},                         // 6 t = 1.5
        {Vec3{1, 1, -2}, Vec3{0, 0, 3}, true, 0.5F, true, 7},                           // 7 from behind
        {Vec3{-1, -1, 1}, Vec3{2, 1, -1.5F}, true, 7.0F / 13.0F, true, 2},              // 8 corner v0, edge 0
        {Vec3{-0.2F, -0.2F, 0.1F}, Vec3{1, 1, 0}, false, 0.0F, true, 2},                // 9 corner inside the ball
        {Vec3{-1, -1, 0.2F}, Vec3{-1, -1, 0}, false, 0.0F, true, 2},                    // 10 away from the corner
        {Vec3{-0.4F, -0.6F, 0.6F}, Vec3{-0.2F, 1.8F, -1.4F}, true, 0.476954F, true, 2}, // 11 late
        {Vec3{-0.9F, -0.9F, 0.6F}, Vec3{0.6F, 1.2F, -0.6F}, true, 1.783594F, true, 2},  // 12 missed (above 1)
        {Vec3{6, -0.3F, 0.2F}, Vec3{-3, 0, 0}, true, 2.115470F, true, 4},               // 13 parallel
        {Vec3{-2, -0.3F, 0.2F}, Vec3{3, 0, 0}, true, 0.551197F, true, 2},               // 14 the cap at v0
    }};
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const Case& c = cases.at(i);
        INFO("case " << (i + 1));
        const auto result = coney::physics::sweepSphereTriangle(Sphere{c.centre, 0.5F}, t, c.move);
        CHECK(result.hit == c.hit);
        if (c.hit) {
            CHECK(result.fraction == Approx(c.fraction).margin(kTolerance));
        }
        CHECK(result.masked == c.masked);
        if (c.masked) {
            CHECK(result.mask == c.mask);
        }
    }
}

TEST_CASE("a degenerate triangle is never hit by the kernel alone", "[physics][sphere_triangle]") {
    WorkingTriangle line;
    line.corners = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}};
    coney::physics::computeEdges(line, false);
    coney::physics::computeNormal(line);
    CHECK_FALSE(coney::physics::sweepSphereTriangle(Sphere{Vec3{1, 0, 0}, 0.5F}, line, Vec3{0, 0, -1}).hit);
}

TEST_CASE("the corner step picks one edge and skips a corner inside the ball", "[physics][sphere_triangle]") {
    const WorkingTriangle t = triangleT();
    const Vec3 m0 = coney::anim::cross(t.edges[0], t.normal);
    EdgePoints points;
    // Case 9: the corner is inside the ball: no contact, but the fraction is written as 0.
    float fraction = 9.0F;
    CHECK_FALSE(coney::physics::sphereSweepVertexRegion(0, t, m0, t.edges[0], Sphere{Vec3{-0.2F, -0.2F, 0.1F}, 0.5F},
                                                        Vec3{1, 1, 0}, fraction, points));
    CHECK(fraction == 0.0F);
    // Case 8: edge 0 is chosen and met at 7/13.
    CHECK(coney::physics::sphereSweepVertexRegion(0, t, m0, t.edges[0], Sphere{Vec3{-1, -1, 1}, 0.5F},
                                                  Vec3{2, 1, -1.5F}, fraction, points));
    CHECK(fraction == Approx(7.0F / 13.0F).margin(kTolerance));
}

TEST_CASE("the capsule lists the wall's roots before the caps'", "[physics][sphere_triangle]") {
    std::array<float, 2> roots{};
    CHECK(coney::physics::sweepSphereCapsule(0.5F, Vec3{2, -0.3F, 2}, Vec3{0, 0, -3}, Vec3{0, 0, 0}, Vec3{4, 0, 0},
                                             roots) == 2);
    CHECK(roots[0] == Approx(0.533333F).margin(kTolerance));
    CHECK(roots[1] == Approx(0.8F).margin(kTolerance));
    // The wall's exit first, then the A cap's entry.
    const Vec3 c{-0.9F, -0.2F, 0.45F};
    const Vec3 m{1.6F, 0, -0.5F};
    CHECK(coney::physics::sweepSphereCapsule(0.5F, c, m, Vec3{0, 0, 0}, Vec3{4, 0, 0}, roots) == 2);
    CHECK(roots[0] == Approx(1.816515F).margin(kTolerance));
    CHECK(roots[1] == Approx(0.336596F).margin(kTolerance));
    // The edge cylinder keeps the one root in 0..1 but leaves the list as it is.
    EdgePoints points;
    CHECK(coney::physics::sphereSweepEdgeCylinder(0.5F, c, m, Vec3{0, 0, 0}, Vec3{4, 0, 0}, points, roots));
    CHECK(points.count == 1);
    checkVec(points.points[0], coney::anim::add(c, coney::anim::scale(m, 0.336596F)));
    CHECK(roots[0] == Approx(1.816515F).margin(kTolerance));
    // So the edge step reports the exit.
    float fraction = 0.0F;
    CHECK(coney::physics::sphereSweepEdge(Vec3{0, 0, 0}, Vec3{4, 0, 0}, Sphere{c, 0.5F}, m, fraction, points));
    CHECK(fraction == Approx(1.816515F).margin(kTolerance));
    // A parallel move toward A lists [exit, entry]; a zero move nothing.
    CHECK(coney::physics::sweepSphereCapsule(0.5F, Vec3{6, -0.3F, 0.2F}, Vec3{-3, 0, 0}, Vec3{0, 0, 0}, Vec3{4, 0, 0},
                                             roots) == 2);
    CHECK(roots[0] == Approx(2.115470F).margin(kTolerance));
    CHECK(roots[1] == Approx(0.551197F).margin(kTolerance));
    CHECK(coney::physics::sweepSphereCapsule(0.5F, Vec3{2, -0.3F, 2}, Vec3{0, 0, 0}, Vec3{0, 0, 0}, Vec3{4, 0, 0},
                                             roots) == 0);
}

TEST_CASE("a segment against a ball: two, one or the point past the end", "[physics][sphere_triangle]") {
    const Vec3 a{0, 0, 0};
    const Vec3 e{4, 0, 0};
    EdgePoints out;
    REQUIRE(coney::physics::segmentSphereIntersect(a, e, Vec3{2, -0.3F, 0.3F}, 0.5F, out));
    REQUIRE(out.count == 2);
    checkVec(out.points[0], Vec3{1.735425F, 0, 0});
    checkVec(out.points[1], Vec3{2.264575F, 0, 0});
    REQUIRE(coney::physics::segmentSphereIntersect(a, e, Vec3{4.3F, -0.3F, 0}, 0.5F, out));
    REQUIRE(out.count == 1);
    checkVec(out.points[0], Vec3{3.9F, 0, 0});
    REQUIRE(coney::physics::segmentSphereIntersect(a, e, Vec3{2, 0, 0}, 5.0F, out));
    REQUIRE(out.count == 1);
    checkVec(out.points[0], Vec3{7, 0, 0});
    REQUIRE(coney::physics::segmentSphereIntersect(a, e, Vec3{2, -0.5F, 0}, 0.5F, out));
    REQUIRE(out.count == 1);
    checkVec(out.points[0], Vec3{2, 0, 0});
    CHECK_FALSE(coney::physics::segmentSphereIntersect(a, e, Vec3{2, -1, 0}, 0.5F, out));
    CHECK(out.count == 0);
}

TEST_CASE("the point-triangle distance covers every region", "[physics][sphere_triangle]") {
    const std::array<Vec3, 3> t{Vec3{0, 0, 0}, Vec3{4, 0, 0}, Vec3{0, 4, 0}};
    struct Case {
        Vec3 point;
        float q;
        float s;
        float t;
    };
    const std::array<Case, 7> cases{{
        {Vec3{1, 1, 2}, 4, 0.25F, 0.25F},
        {Vec3{-1, -1, 0}, 2, 0, 0},
        {Vec3{5, -1, 1}, 3, 1, 0},
        {Vec3{3, 3, 0}, 2, 0.5F, 0.5F},
        {Vec3{0.5F, 0.5F, -0.5F}, 0.25F, 0.125F, 0.125F},
        {Vec3{2, -2, 0}, 4, 0.5F, 0},
        {Vec3{-1, 2, 0}, 1, 0, 0.5F},
    }};
    for (const Case& c : cases) {
        INFO("point " << c.point.x << ", " << c.point.y << ", " << c.point.z);
        float s = -1.0F;
        float tt = -1.0F;
        CHECK(coney::physics::pointTriangleDistanceSq(c.point, t, &s, &tt) == Approx(c.q).margin(kTolerance));
        CHECK(s == Approx(c.s).margin(kTolerance));
        CHECK(tt == Approx(c.t).margin(kTolerance));
    }
}

TEST_CASE("a sphere's penetration of a triangle is strict at the radius", "[physics][sphere_triangle]") {
    const std::array<Vec3, 3> t{Vec3{0, 0, 0}, Vec3{4, 0, 0}, Vec3{0, 4, 0}};
    float depth = -1.0F;
    REQUIRE(coney::physics::sphereTrianglePenetration(Sphere{Vec3{1, 1, 0.3F}, 0.5F}, t, depth));
    CHECK(depth == Approx(0.2F).margin(kTolerance));
    depth = -1.0F;
    CHECK_FALSE(coney::physics::sphereTrianglePenetration(Sphere{Vec3{1, 1, 0.5F}, 0.5F}, t, depth));
    CHECK(depth == -1.0F);
    REQUIRE(coney::physics::sphereTrianglePenetration(Sphere{Vec3{5, -1, 1}, 2.0F}, t, depth));
    CHECK(depth == Approx(0.267949F).margin(kTolerance));
}
