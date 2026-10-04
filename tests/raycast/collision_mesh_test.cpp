// SPDX-License-Identifier: GPL-3.0-or-later
#include "raycast/collision_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_stacks.h"
#include "core/error.h"
#include "support/fixtures.h"
#include "support/world_fixtures.h"

using Catch::Approx;
using coney::raycast::CollisionMesh;
using coney::raycast::Ray;
using coney::raycast::Vec3;
using coney::test::Bytes;
using coney::test::f32;

namespace {

// One synthetic triangle: three corners, flags (the enabled bit is set at load anyway), material and area byte.
struct Tri {
    Vec3 a, b, c;
    std::uint16_t flags = 0;
    std::uint8_t material = 5;
    std::uint8_t area = 1;
};

// The six chunks of a synthetic collision mesh over the box [0, size]² × [-50, 50] with n × n × 1 cells: the matrix
// maps the box onto 0..n-1 as the disc's do, and each triangle is listed in the cells of its bounding box, one cell
// wider on every side (as the disc's lists are, roughly).
struct MeshChunks {
    Bytes header, lists, grid, triangles, vertices, checked;
};

MeshChunks meshChunks(const std::vector<Tri>& tris, std::uint16_t n = 8, float size = 80.0F) {
    MeshChunks out;
    const float scale = static_cast<float>(n - 1) / size;
    // Header: 16 bytes of vtable, the matrix (diagonal scale, z scale 0 for one layer), the clamp box, the counts.
    out.header.fill(16, 0);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const float value = row == column && row < 2 ? scale : (row == 3 && column == 3 ? 1.0F : 0.0F);
            f32(out.header, value);
        }
    }
    f32(f32(f32(f32(out.header, 0.5F), 0.5F), 0.5F), 1.0F);
    f32(f32(f32(f32(out.header, static_cast<float>(n) - 0.5F), static_cast<float>(n) - 0.5F), 0.5F), 1.0F);
    out.header.u16(n).u16(n).u16(1).u16(0);

    // Lists: a leading 0 (offset 0 reads as empty), then one list per non-empty cell.
    std::vector<std::uint16_t> values{0};
    std::vector<std::uint32_t> cells(std::size_t{n} * n, 0);
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            std::vector<std::uint16_t> here;
            for (std::size_t i = 0; i < tris.size(); ++i) {
                const Tri& t = tris[i];
                const float lox = std::min({t.a.x, t.b.x, t.c.x}) * scale - 1.0F;
                const float hix = std::max({t.a.x, t.b.x, t.c.x}) * scale + 1.0F;
                const float loy = std::min({t.a.y, t.b.y, t.c.y}) * scale - 1.0F;
                const float hiy = std::max({t.a.y, t.b.y, t.c.y}) * scale + 1.0F;
                if (static_cast<float>(x) >= lox && static_cast<float>(x) <= hix && static_cast<float>(y) >= loy &&
                    static_cast<float>(y) <= hiy) {
                    here.push_back(static_cast<std::uint16_t>(i));
                }
            }
            if (!here.empty()) {
                cells[std::size_t{y} * n + x] = static_cast<std::uint32_t>(values.size());
                values.push_back(static_cast<std::uint16_t>(here.size()));
                values.insert(values.end(), here.begin(), here.end());
            }
        }
    }
    for (const std::uint16_t v : values) {
        out.lists.u16(v);
    }
    for (const std::uint32_t c : cells) {
        out.grid.u32(c);
    }
    out.header.u32(0).u32(static_cast<std::uint32_t>(values.size())).u32(0); // grid pointer, list count, list pointer
    out.header.u16(static_cast<std::uint16_t>(tris.size())).u16(0).u32(0).u32(0); // triangle count, two pointers
    out.header.u32(static_cast<std::uint32_t>(tris.size() * 3)).u32(0);           // vertex count, checked pointer
    f32(out.header, -50.0F).u32(0);                                               // lowest z, zero

    // Three vertices per triangle.
    std::uint16_t next = 0;
    for (const Tri& t : tris) {
        for (const Vec3& v : {t.a, t.b, t.c}) {
            f32(f32(f32(f32(out.vertices, v.x), v.y), v.z), 1.0F);
        }
        out.triangles.u16(next).u16(static_cast<std::uint16_t>(next + 1)).u16(static_cast<std::uint16_t>(next + 2));
        out.triangles.u16(t.flags).u8(t.material).u8(t.area);
        next = static_cast<std::uint16_t>(next + 3);
    }
    out.checked.fill(((tris.size() + 7) / 8 + 15) & 0x7FF0U, 0);
    return out;
}

// Builds the mesh of `tris`, requiring success.
std::unique_ptr<CollisionMesh> makeMesh(const std::vector<Tri>& tris, std::uint16_t n = 8) {
    MeshChunks c = meshChunks(tris, n);
    auto mesh = CollisionMesh::build(c.header.span(), c.lists.span(), c.grid.span(), c.triangles.span(),
                                     c.vertices.span(), c.checked.span());
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

// A floor square at height z over [x0, x1] × [y0, y1], as two triangles facing up.
std::vector<Tri> floorAt(float z, float x0, float x1, float y0, float y1, std::uint8_t area = 1,
                         std::uint16_t flags = 0) {
    return {Tri{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, flags, 5, area},
            Tri{{x0, y0, z}, {x1, y1, z}, {x0, y1, z}, flags, 5, area}};
}

// A wall in the plane x = `x` over y [y0, y1] and z [z0, z1], facing -x (two triangles).
std::vector<Tri> wallFacingMinusX(float x, float y0, float y1, float z0, float z1, std::uint16_t flags = 0) {
    return {Tri{{x, y0, z0}, {x, y0, z1}, {x, y1, z1}, flags, 5, 1},
            Tri{{x, y0, z0}, {x, y1, z1}, {x, y1, z0}, flags, 5, 1}};
}

// Joins triangle lists.
std::vector<Tri> join(std::vector<Tri> a, const std::vector<Tri>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// The hit of a cast that must hit.
coney::raycast::RayHit mustHit(const std::optional<coney::raycast::RayHit>& hit) {
    REQUIRE(hit.has_value());
    return hit.value_or(coney::raycast::RayHit{});
}

// A downward ray from `from`.
Ray down(Vec3 from, float length = 100.0F) {
    return Ray{.origin = from, .direction = coney::raycast::kDown, .length = length};
}

} // namespace

TEST_CASE("a point drops onto the floor below it, 0.1 above the ground", "[collision]") {
    const auto mesh = makeMesh(floorAt(2.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Vec3 p{30.0F, 40.0F, 20.0F};
    REQUIRE(coney::raycast::dropToGround(*mesh, 50.0F, p));
    CHECK(p.z == Approx(2.1F));
    const coney::raycast::RayHit hit = mustHit(mesh->rayCast(down({30.0F, 40.0F, 20.0F}), {}, 0));
    CHECK(hit.t == Approx(18.0F));
    CHECK(hit.normal.z == Approx(1.0F));
    CHECK(hit.material == 5);
    // Too short a ray misses and leaves the point where it was.
    Vec3 high{30.0F, 40.0F, 20.0F};
    CHECK_FALSE(coney::raycast::dropToGround(*mesh, 10.0F, high));
    CHECK(high.z == 20.0F);
}

TEST_CASE("one-sided triangles are missed from behind, two-sided ones hit with the normal facing the ray",
          "[collision]") {
    const auto oneSided = makeMesh(floorAt(2.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    const Ray up{.origin = {30.0F, 40.0F, -10.0F}, .direction = {0.0F, 0.0F, 1.0F}, .length = 50.0F};
    CHECK_FALSE(oneSided->rayCast(up, {}, 0).has_value());
    const auto twoSided = makeMesh(floorAt(2.0F, 0.0F, 80.0F, 0.0F, 80.0F, 1, coney::raycast::kTriangleTwoSided));
    const coney::raycast::RayHit hit = mustHit(twoSided->rayCast(up, {}, 0));
    CHECK(hit.t == Approx(12.0F));
    CHECK(hit.normal.z == Approx(-1.0F));
    CHECK(hit.flags == (coney::raycast::kTriangleEnabled | coney::raycast::kTriangleTwoSided));
}

TEST_CASE("a cast returns the nearest hit, and a triangle in several cells is tested once", "[collision]") {
    // The upper floor spans most of the grid, so it is listed in many cells.
    const auto mesh =
        makeMesh(join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), floorAt(5.0F, 10.0F, 70.0F, 10.0F, 70.0F)));
    const coney::raycast::RayHit hit = mustHit(mesh->rayCast(down({40.0F, 40.0F, 20.0F}), {}, 0));
    CHECK(hit.t == Approx(15.0F));
    CHECK(hit.triangle >= 2);
}

TEST_CASE("the mask skips type bits and reaches disabled triangles; materials can be excluded", "[collision]") {
    // Flag 0x04 (a type bit) on the upper floor, 0x800 on nothing.
    const auto mesh =
        makeMesh(join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), floorAt(5.0F, 0.0F, 80.0F, 0.0F, 80.0F, 2, 0x0004)));
    const Ray ray = down({40.0F, 40.0F, 20.0F});
    CHECK(mustHit(mesh->rayCast(ray, {}, 0)).t == Approx(15.0F));
    CHECK(mustHit(mesh->rayCast(ray, {}, 0x0004)).t == Approx(20.0F)); // the type bit excludes the upper floor
    CHECK(mustHit(mesh->rayCast(ray, {}, 0x0001)).t == Approx(15.0F)); // bits 0 and 11 never exclude
    const std::array<std::uint8_t, 2> exclude{7, 5};
    CHECK_FALSE(mesh->rayCast(ray, exclude, 0).has_value());
}

TEST_CASE("triangles inside a box switch off and on; mask bit 0 still sees them", "[collision]") {
    auto mesh = makeMesh(join(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), floorAt(5.0F, 30.0F, 50.0F, 30.0F, 50.0F)));
    const Ray ray = down({40.0F, 40.0F, 20.0F});
    // The box holds the small upper floor whole, and only part of the big lower one.
    mesh->setEnabledInBox({25.0F, 25.0F, 4.0F}, {55.0F, 55.0F, 6.0F}, false);
    CHECK(mustHit(mesh->rayCast(ray, {}, 0)).t == Approx(20.0F));
    CHECK(mustHit(mesh->rayCast(ray, {}, coney::raycast::kTriangleEnabled)).t == Approx(15.0F));
    mesh->setEnabledInBox({25.0F, 25.0F, 4.0F}, {55.0F, 55.0F, 6.0F}, true);
    CHECK(mustHit(mesh->rayCast(ray, {}, 0)).t == Approx(15.0F));
}

TEST_CASE("a long diagonal ray walks the grid's columns to a far wall", "[collision]") {
    // A small wall near the far corner; the ray starts near the opposite corner, crossing many cells.
    const auto mesh = makeMesh(wallFacingMinusX(70.0F, 60.0F, 75.0F, -5.0F, 5.0F), 16);
    const float s = 1.0F / std::sqrt(2.0F);
    const Ray ray{.origin = {2.0F, 0.0F, 0.0F}, .direction = {s, s, 0.0F}, .length = 120.0F};
    const coney::raycast::RayHit hit = mustHit(mesh->rayCast(ray, {}, 0));
    CHECK(hit.t == Approx(68.0F * std::sqrt(2.0F)));
    CHECK(hit.normal.x == Approx(-1.0F));
    // Steeper than 45 degrees, and backwards along x: the walk orders each column's ends.
    const auto backHit =
        mesh->rayCast(Ray{.origin = {78.0F, 75.0F, 0.0F}, .direction = {-0.6F, -0.8F, 0.0F}, .length = 100.0F}, {}, 0);
    CHECK_FALSE(backHit.has_value()); // it meets the wall from behind
}

TEST_CASE("a sphere is pushed out of a wall by its overlap, and floors never push", "[collision]") {
    const auto mesh =
        makeMesh(join(wallFacingMinusX(40.0F, 20.0F, 60.0F, -10.0F, 10.0F), floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F)));
    const auto pushed = mesh->spherePush(1.0F, {39.5F, 40.0F, 0.5F}, {});
    CHECK(pushed.touched);
    CHECK(pushed.centre.x == Approx(39.0F));
    CHECK(pushed.centre.z == Approx(0.5F)); // the floor under it does not push
    CHECK(pushed.firstNormal.x == Approx(-1.0F));
    // Clear of the wall: nothing.
    CHECK_FALSE(mesh->spherePush(1.0F, {30.0F, 40.0F, 0.5F}, {}).touched);
    // Behind the one-sided wall: nothing.
    CHECK_FALSE(mesh->spherePush(1.0F, {40.5F, 40.0F, 0.5F}, {}).touched);
    // Beside the wall's end, touching only its edge: nothing.
    CHECK_FALSE(mesh->spherePush(1.0F, {39.5F, 60.5F, 0.5F}, {}).touched);
}

TEST_CASE("two walls push by the average, and a two-sided wall pushes from either side", "[collision]") {
    // Two coincident walls (a wall listed twice) push by the average: once the overlap, not twice.
    const auto doubled = makeMesh(join(wallFacingMinusX(40.0F, 20.0F, 60.0F, -10.0F, 10.0F),
                                       wallFacingMinusX(40.0F, 20.0F, 60.0F, -10.0F, 10.0F)));
    CHECK(doubled->spherePush(1.0F, {39.5F, 40.0F, 0.0F}, {}).centre.x == Approx(39.0F));
    const auto twoSided =
        makeMesh(wallFacingMinusX(40.0F, 20.0F, 60.0F, -10.0F, 10.0F, coney::raycast::kTriangleTwoSided));
    const auto behind = twoSided->spherePush(1.0F, {40.5F, 40.0F, 0.0F}, {});
    CHECK(behind.touched);
    CHECK(behind.centre.x == Approx(41.0F));
    CHECK(behind.firstNormal.x == Approx(1.0F));
}

TEST_CASE("dropping to marked ground passes through triangles whose area byte is 0", "[collision]") {
    const auto mesh =
        makeMesh(join(floorAt(5.0F, 0.0F, 80.0F, 0.0F, 80.0F, 0), floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F, 3)));
    Vec3 p{40.0F, 40.0F, 20.0F};
    REQUIRE(coney::raycast::dropToMarkedGround(*mesh, 50.0F, p));
    CHECK(p.z == Approx(0.25F));
    Vec3 unmarkedOnly{40.0F, 40.0F, 20.0F};
    const auto upper = makeMesh(floorAt(5.0F, 0.0F, 80.0F, 0.0F, 80.0F, 0));
    CHECK_FALSE(coney::raycast::dropToMarkedGround(*upper, 50.0F, unmarkedOnly));
    CHECK(unmarkedOnly.z == 20.0F);
}

TEST_CASE("a marched ray finds the first hit beyond its first steps", "[collision]") {
    const auto mesh = makeMesh(wallFacingMinusX(60.0F, 0.0F, 80.0F, -10.0F, 10.0F));
    const auto hit = coney::raycast::marchRay(*mesh, 7.0F, 100.0F, {5.0F, 40.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
    REQUIRE(hit.has_value());
    CHECK(hit.value_or(Vec3{}).x == Approx(60.0F));
    CHECK_FALSE(coney::raycast::marchRay(*mesh, 7.0F, 40.0F, {5.0F, 40.0F, 0.0F}, {1.0F, 0.0F, 0.0F}).has_value());
}

TEST_CASE("a collision mesh with an index out of range or a short chunk is refused", "[collision]") {
    MeshChunks c = meshChunks(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Bytes badTriangles;
    badTriangles.u16(0).u16(1).u16(99).u16(0).u8(5).u8(1).u16(0).u16(1).u16(2).u16(0).u8(5).u8(1);
    auto refused = CollisionMesh::build(c.header.span(), c.lists.span(), c.grid.span(), badTriangles.span(),
                                        c.vertices.span(), c.checked.span());
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == coney::ErrorCode::Invalid);
    Bytes shortVertices;
    shortVertices.fill(16, 0);
    auto truncated = CollisionMesh::build(c.header.span(), c.lists.span(), c.grid.span(), c.triangles.span(),
                                          shortVertices.span(), c.checked.span());
    REQUIRE_FALSE(truncated.has_value());
    CHECK(truncated.error().code == coney::ErrorCode::Truncated);
    Bytes badGrid = c.grid;
    badGrid.patchU32(0, 0xFFFF);
    CHECK_FALSE(CollisionMesh::build(c.header.span(), c.lists.span(), badGrid.span(), c.triangles.span(),
                                     c.vertices.span(), c.checked.span())
                    .has_value());
}

TEST_CASE("the 0x03 handler pops the five collision chunks and pushes the mesh", "[collision]") {
    MeshChunks c = meshChunks(floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    coney::chunk::ChunkStacks stacks;
    // The file's order: 0x52, 0x07, 0x04, 0x05, 0x06, 0x03.
    const auto push = [&stacks](std::uint32_t type, const Bytes& bytes) {
        const auto span = bytes.span();
        stacks.pushChunk(coney::chunk::ChunkData{
            .type = type, .id = 0, .bytes = std::vector<std::byte>(span.begin(), span.end()), .object = nullptr});
    };
    push(coney::raycast::kCollisionChecked, c.checked);
    push(coney::raycast::kCollisionVertexBuffer, c.vertices);
    push(coney::raycast::kCollisionTriangles, c.triangles);
    push(coney::raycast::kCollisionGrid, c.grid);
    push(coney::raycast::kCollisionStrings, c.lists);
    push(coney::raycast::kCollisionMeshChunk, c.header);
    REQUIRE(coney::raycast::onCollisionMeshLoaded(stacks, coney::raycast::kCollisionMeshChunk).has_value());
    CHECK(stacks.chunks().empty());
    auto mesh = stacks.popObject<CollisionMesh>();
    REQUIRE(mesh.has_value());
    CHECK((*mesh)->triangles().size() == 2);
    CHECK((*mesh)->cells() == std::array<std::uint32_t, 3>{8, 8, 1});

    // Out of order: the grid where the lists should be.
    push(coney::raycast::kCollisionChecked, c.checked);
    push(coney::raycast::kCollisionGrid, c.grid);
    push(coney::raycast::kCollisionMeshChunk, c.header);
    CHECK_FALSE(coney::raycast::onCollisionMeshLoaded(stacks, coney::raycast::kCollisionMeshChunk).has_value());
}
