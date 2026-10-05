// SPDX-License-Identifier: GPL-3.0-or-later
#include "raycast/collision_builder.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "raycast/collision_mesh.h"

using Catch::Approx;
using coney::raycast::BuildTriangle;
using coney::raycast::Ray;
using coney::raycast::Vec3;

namespace {

// The hit of a cast that must hit.
coney::raycast::RayHit mustHit(const std::optional<coney::raycast::RayHit>& hit) {
    REQUIRE(hit.has_value());
    return hit.value_or(coney::raycast::RayHit{});
}

// A square floor at height z over [x0, x1] × [y0, y1]: two triangles facing up.
std::vector<BuildTriangle> floorSquare(float z, float x0, float x1, float y0, float y1) {
    return {BuildTriangle{.corners = {Vec3{x0, y0, z}, Vec3{x1, y0, z}, Vec3{x1, y1, z}}},
            BuildTriangle{.corners = {Vec3{x0, y0, z}, Vec3{x1, y1, z}, Vec3{x0, y1, z}}}};
}

// A wall in the plane x = `x` over y [y0, y1] and z [0, h], facing -x.
std::vector<BuildTriangle> wallFacingMinusX(float x, float y0, float y1, float h) {
    return {BuildTriangle{.corners = {Vec3{x, y0, 0}, Vec3{x, y0, h}, Vec3{x, y1, h}}},
            BuildTriangle{.corners = {Vec3{x, y0, 0}, Vec3{x, y1, h}, Vec3{x, y1, 0}}}};
}

// The nearest hit of `ray` over every triangle, without the grid: what the grid walk must agree with.
std::optional<float> bruteForce(const coney::raycast::CollisionMesh& mesh, const Ray& ray) {
    std::optional<float> best;
    for (std::uint32_t i = 0; i < mesh.triangles().size(); ++i) {
        const auto& v = mesh.triangles()[i].vertices;
        const Vec3 a = mesh.vertices()[v[0]];
        const Vec3 b = mesh.vertices()[v[1]];
        const Vec3 c = mesh.vertices()[v[2]];
        // Möller-Trumbore, one-sided, as the mesh's own test.
        const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 d = ray.direction;
        const Vec3 p{d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x};
        const float det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
        if (det < 1e-6F) {
            continue;
        }
        const Vec3 s{ray.origin.x - a.x, ray.origin.y - a.y, ray.origin.z - a.z};
        const float u = (s.x * p.x + s.y * p.y + s.z * p.z) / det;
        const Vec3 q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
        const float w = (d.x * q.x + d.y * q.y + d.z * q.z) / det;
        const float t = (e2.x * q.x + e2.y * q.y + e2.z * q.z) / det;
        if (u >= 0 && w >= 0 && u + w <= 1 && t >= 0 && t <= ray.length && (!best || t < *best)) {
            best = t;
        }
    }
    return best;
}

} // namespace

TEST_CASE("a built floor merges shared corners and drops a point onto it", "[collision_builder]") {
    auto mesh = coney::raycast::buildCollisionMesh(floorSquare(2.0F, -10, 10, -10, 10), 4.0F);
    REQUIRE(mesh.has_value());
    CHECK((*mesh)->triangles().size() == 2);
    CHECK((*mesh)->vertices().size() == 4); // two corners shared
    CHECK((*mesh)->lowestZ() == 2.0F);
    Vec3 p{3.0F, -4.0F, 10.0F};
    REQUIRE(coney::raycast::dropToGround(**mesh, 20.0F, p));
    CHECK(p.z == Approx(2.1F)); // 0.1 above the ground, as Collision_DropToGround leaves it
}

TEST_CASE("a built mesh keeps each triangle's flags, material and area byte", "[collision_builder]") {
    std::vector<BuildTriangle> tris = floorSquare(0.0F, 0, 4, 0, 4);
    tris[0].flags = 0x0010;
    tris[0].material = 35;
    tris[0].area = 7;
    auto mesh = coney::raycast::buildCollisionMesh(tris, 4.0F);
    REQUIRE(mesh.has_value());
    const auto& t = (*mesh)->triangles()[0];
    CHECK(t.flags == (0x0010 | coney::raycast::kTriangleEnabled));
    CHECK(t.material == 35);
    CHECK(t.area == 7);
    // A ray down onto that triangle reports them.
    const coney::raycast::RayHit hit =
        mustHit((*mesh)->rayCast(Ray{{3.0F, 1.0F, 5.0F}, coney::raycast::kDown, 10.0F}, {}, 0));
    CHECK(hit.material == 35);
    CHECK(hit.area == 7);
}

TEST_CASE("a built wall stops a horizontal ray and pushes a sphere out", "[collision_builder]") {
    std::vector<BuildTriangle> tris = floorSquare(0.0F, -20, 20, -20, 20);
    const std::vector<BuildTriangle> wall = wallFacingMinusX(5.0F, -3, 3, 3);
    tris.insert(tris.end(), wall.begin(), wall.end());
    auto mesh = coney::raycast::buildCollisionMesh(tris, 4.0F);
    REQUIRE(mesh.has_value());
    const coney::raycast::RayHit hit =
        mustHit((*mesh)->rayCast(Ray{{-10.0F, 0.5F, 1.0F}, {1.0F, 0.0F, 0.0F}, 30.0F}, {}, 0));
    CHECK(hit.t == Approx(15.0F));
    CHECK(hit.normal.x == Approx(-1.0F));
    const auto push = (*mesh)->spherePush(0.5F, Vec3{4.8F, 0.0F, 1.0F}, {});
    CHECK(push.touched);
    CHECK(push.centre.x == Approx(4.5F));
}

TEST_CASE("the grid walk of a built mesh finds the same hits as testing every triangle", "[collision_builder]") {
    // A field of small raised squares over 60 m, cast across by long slanted rays in many directions.
    std::vector<BuildTriangle> tris = floorSquare(0.0F, -30, 30, -30, 30);
    for (int i = -5; i <= 5; ++i) {
        for (int j = -5; j <= 5; ++j) {
            const auto x = static_cast<float>(i) * 5.0F;
            const auto y = static_cast<float>(j) * 5.0F;
            const std::vector<BuildTriangle> top =
                floorSquare(static_cast<float>((i + j + 10) % 4) * 0.5F + 0.25F, x, x + 1.0F, y, y + 1.0F);
            tris.insert(tris.end(), top.begin(), top.end());
        }
    }
    auto mesh = coney::raycast::buildCollisionMesh(tris, 4.0F);
    REQUIRE(mesh.has_value());
    int compared = 0;
    for (int k = 0; k < 72; ++k) {
        const float angle = static_cast<float>(k) * 0.0872665F; // 5 degrees apart
        const Vec3 direction{std::cos(angle) * 0.6F, std::sin(angle) * 0.6F, -0.8F};
        const Ray ray{{-std::cos(angle) * 25.0F + 0.3F, -std::sin(angle) * 25.0F + 0.7F, 30.0F}, direction, 60.0F};
        const auto grid = (*mesh)->rayCast(ray, {}, 0);
        const auto brute = bruteForce(**mesh, ray);
        REQUIRE(grid.has_value() == brute.has_value());
        if (grid && brute) {
            CHECK(grid->t == Approx(*brute).margin(1e-4));
            ++compared;
        }
    }
    CHECK(compared == 72);
}

TEST_CASE("the collision builder refuses what the format cannot hold", "[collision_builder]") {
    const std::vector<BuildTriangle> none;
    CHECK(coney::raycast::encodeCollisionMesh(none, 4.0F).error().code == coney::ErrorCode::InvalidArgument);
    CHECK(coney::raycast::encodeCollisionMesh(floorSquare(0, 0, 1, 0, 1), 0.0F).error().code ==
          coney::ErrorCode::InvalidArgument);
    std::vector<BuildTriangle> nan = floorSquare(0, 0, 1, 0, 1);
    nan[1].corners[2].z = std::numeric_limits<float>::quiet_NaN();
    CHECK(coney::raycast::encodeCollisionMesh(nan, 4.0F).error().code == coney::ErrorCode::InvalidArgument);
    const std::vector<BuildTriangle> tooMany(coney::raycast::kMaxCollisionTriangles + 1,
                                             floorSquare(0, 0, 1, 0, 1).front());
    CHECK(coney::raycast::encodeCollisionMesh(tooMany, 4.0F).error().code == coney::ErrorCode::InvalidArgument);
}

TEST_CASE("encoded chunks have the sizes the level file's chunks have", "[collision_builder]") {
    auto chunks = coney::raycast::encodeCollisionMesh(floorSquare(0, 0, 10, 0, 10), 4.0F);
    REQUIRE(chunks.has_value());
    CHECK(chunks->header.size() == coney::raycast::kCollisionHeaderBytes);
    CHECK(chunks->triangles.size() == std::size_t{2} * 10);
    CHECK(chunks->vertices.size() == std::size_t{4} * 16);
    CHECK(chunks->checked.size() == 16); // ((2 + 7) / 8 + 15) & ~15
    CHECK(chunks->lists.size() % 2 == 0);
}
