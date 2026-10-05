// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sandbox/sandbox_layout.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::sandbox::buildSandboxMesh;
using coney::sandbox::kNoTessellation;
using coney::sandbox::SandboxLayout;
using coney::sandbox::SandboxMesh;

namespace {

// The layout of `text`, which must parse.
SandboxLayout layoutOf(std::string_view text) {
    auto layout = coney::sandbox::parseSandboxLayout(text);
    REQUIRE(layout.has_value());
    return *layout;
}

// The geometric normal of triangle `t`.
Vec3 faceNormal(const SandboxMesh& mesh, std::size_t t) {
    const auto& v = mesh.triangles[t].vertices;
    const Vec3 a = mesh.vertices[v[0]].position;
    return coney::anim::normalise(coney::anim::cross(coney::anim::subtract(mesh.vertices[v[1]].position, a),
                                                     coney::anim::subtract(mesh.vertices[v[2]].position, a)));
}

// Checks what every mesh must satisfy: unit normals, every face and vertex normal pointing away from `inside` (a point
// inside the convex primitive), and the face's winding agreeing with its vertices' normals.
void checkOutward(const SandboxMesh& mesh, Vec3 inside) {
    for (const auto& v : mesh.vertices) {
        CHECK(coney::anim::length(v.normal) == Approx(1.0F).margin(1e-4));
        CHECK(coney::anim::dot(v.normal, coney::anim::subtract(v.position, inside)) > -1e-4F);
    }
    for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
        const Vec3 n = faceNormal(mesh, t);
        const auto& v = mesh.triangles[t].vertices;
        const Vec3 centroid = coney::anim::scale(
            coney::anim::add(coney::anim::add(mesh.vertices[v[0]].position, mesh.vertices[v[1]].position),
                             mesh.vertices[v[2]].position),
            1.0F / 3.0F);
        CHECK(coney::anim::dot(n, coney::anim::subtract(centroid, inside)) > 0.0F);
        CHECK(coney::anim::dot(n, mesh.vertices[v[0]].normal) > 0.5F);
    }
}

// The mesh's bounds.
std::pair<Vec3, Vec3> boundsOf(const SandboxMesh& mesh) {
    Vec3 lo{1e9F, 1e9F, 1e9F};
    Vec3 hi{-1e9F, -1e9F, -1e9F};
    for (const auto& v : mesh.vertices) {
        lo = Vec3{std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
        hi = Vec3{std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
    }
    return {lo, hi};
}

} // namespace

TEST_CASE("a raised box has six faces, a grounded one has no bottom", "[sandbox][mesh]") {
    const SandboxMesh raised = buildSandboxMesh(layoutOf("box at=0,0,1 size=2,4,3\n"), kNoTessellation);
    CHECK(raised.triangles.size() == 12);
    CHECK(raised.vertices.size() == 24);
    checkOutward(raised, Vec3{0.0F, 0.0F, 2.5F});
    const auto [lo, hi] = boundsOf(raised);
    CHECK(lo.x == -1.0F);
    CHECK(hi.y == 2.0F);
    CHECK(lo.z == 1.0F);
    CHECK(hi.z == 4.0F);

    const SandboxMesh grounded = buildSandboxMesh(layoutOf("box at=0,0,0 size=2,4,3\n"), kNoTessellation);
    CHECK(grounded.triangles.size() == 10);
}

TEST_CASE("tessellation splits faces into pieces no longer than the edge", "[sandbox][mesh]") {
    // A 4 × 2 × 1 box at 1 m: top 4×2, sides 4×1 and 2×1 pieces, no bottom.
    const SandboxMesh mesh = buildSandboxMesh(layoutOf("box at=0,0,0 size=4,2,1\n"), 1.0F);
    CHECK(mesh.triangles.size() == std::size_t{2} * (8 + 4 + 4 + 2 + 2));
    CHECK(mesh.vertices.size() == 15 + 10 + 10 + 6 + 6);
    checkOutward(mesh, Vec3{0.0F, 0.0F, 0.5F});
}

TEST_CASE("texture coordinates keep one tile per metre on every face, turned or not", "[sandbox][mesh]") {
    for (const std::string_view text :
         {"box at=0,0,0 size=3,5,2\n", "box at=1,2,0 size=3,5,2 yaw=30\n", "box at=0,0,0 size=3,5,2 uv=2\n"}) {
        const SandboxLayout layout = layoutOf(text);
        const SandboxMesh mesh = buildSandboxMesh(layout, kNoTessellation);
        const float scale = layout.primitives[0].uvScale;
        // Along every triangle edge the texture moves as far as the edge is long.
        for (const auto& t : mesh.triangles) {
            for (std::size_t k = 0; k < 3; ++k) {
                const auto& a = mesh.vertices[t.vertices.at(k)];
                const auto& b = mesh.vertices[t.vertices.at((k + 1) % 3)];
                const float uvDistance = std::hypot(b.u - a.u, b.v - a.v);
                CHECK(uvDistance == Approx(coney::anim::distance(a.position, b.position) * scale).margin(1e-4));
            }
        }
    }
    // On a wall, v is the height above the ground.
    const SandboxMesh wall = buildSandboxMesh(layoutOf("box at=0,0,0 size=1,1,2\n"), kNoTessellation);
    for (const auto& v : wall.vertices) {
        if (std::fabs(v.normal.z) < 0.5F) {
            CHECK(v.v == Approx(v.position.z));
        }
    }
}

TEST_CASE("a ramp's slope faces up the angle it was given", "[sandbox][mesh]") {
    const SandboxMesh mesh = buildSandboxMesh(layoutOf("ramp at=0,0,0 width=2 height=1 angle=30\n"), kNoTessellation);
    // Slope (2), high end (2), two sides (1 each); the bottom is on the ground.
    CHECK(mesh.triangles.size() == 6);
    const Vec3 slope = faceNormal(mesh, 0);
    CHECK(slope.z == Approx(std::cos(0.5235988F)));
    CHECK(slope.y == Approx(-std::sin(0.5235988F))); // rising along +y, so facing back down -y
    checkOutward(mesh, Vec3{0.0F, 0.5F, 0.2F});
    const auto [lo, hi] = boundsOf(mesh);
    CHECK(hi.y - lo.y == Approx(1.0F / std::tan(0.5235988F)));
}

TEST_CASE("stairs have a tread at each step's height and no hidden faces", "[sandbox][mesh]") {
    const SandboxMesh mesh =
        buildSandboxMesh(layoutOf("stairs at=0,0,0 width=2 steps=3 rise=0.2 run=0.5\n"), kNoTessellation);
    // Per step: tread, riser and two side columns; then the back. No bottom on the ground.
    CHECK(mesh.triangles.size() == std::size_t{2} * (3 * 4 + 1));
    std::vector<float> treads;
    for (std::size_t t = 0; t < mesh.triangles.size(); t += 2) {
        if (faceNormal(mesh, t).z > 0.99F) {
            treads.push_back(mesh.vertices[mesh.triangles[t].vertices[0]].position.z);
        }
    }
    REQUIRE(treads.size() == 3);
    CHECK(treads[0] == Approx(0.2F));
    CHECK(treads[2] == Approx(0.6F));
}

TEST_CASE("round shapes have outward unit normals and close round their axis", "[sandbox][mesh]") {
    const SandboxMesh cylinder =
        buildSandboxMesh(layoutOf("cylinder at=0,0,1 radius=1 height=2 segments=8\n"), kNoTessellation);
    // Bottom and top discs (8 triangles each) and the side (8 quads).
    CHECK(cylinder.triangles.size() == 8 + 16 + 8);
    checkOutward(cylinder, Vec3{0.0F, 0.0F, 2.0F});
    const SandboxMesh sphere = buildSandboxMesh(layoutOf("sphere at=0,0,0 radius=1 segments=8\n"), kNoTessellation);
    checkOutward(sphere, Vec3{0.0F, 0.0F, 1.0F});
    const auto [lo, hi] = boundsOf(sphere);
    CHECK(lo.z == Approx(0.0F).margin(1e-6));
    CHECK(hi.z == Approx(2.0F));
    const SandboxMesh capsule =
        buildSandboxMesh(layoutOf("capsule at=0,0,0 radius=0.4 height=1.8 segments=12\n"), kNoTessellation);
    checkOutward(capsule, Vec3{0.0F, 0.0F, 0.9F});
    CHECK(boundsOf(capsule).second.z == Approx(1.8F));
}

TEST_CASE("collision triangles come from solid primitives, untessellated, with their tags", "[sandbox][mesh]") {
    const SandboxLayout layout = layoutOf("tessellate edge=0.25\n"
                                          "box at=0,0,0 size=4,4,1 material=35 flags=0x20 area=3\n"
                                          "box at=5,0,0 size=1,1,1 solid=no\n");
    const auto triangles = coney::sandbox::sandboxCollisionTriangles(layout);
    REQUIRE(triangles.size() == 10); // the first box only, two triangles a face, no bottom
    CHECK(triangles[0].material == 35);
    CHECK(triangles[0].flags == 0x20);
    CHECK(triangles[0].area == 3);
}

TEST_CASE("a primitive's yaw turns it counter-clockwise seen from above", "[sandbox][mesh]") {
    const Vec3 turned = coney::sandbox::turnByYaw(Vec3{1.0F, 0.0F, 2.0F}, 90.0F);
    CHECK(turned.x == Approx(0.0F).margin(1e-6));
    CHECK(turned.y == Approx(1.0F));
    CHECK(turned.z == 2.0F);
}
