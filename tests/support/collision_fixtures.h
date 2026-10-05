// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic collision meshes, built byte by byte as a level file's six collision chunks hold them: for the collision
// mesh's own tests and for the characters that stand and walk on one. No game data.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "raycast/collision_mesh.h"
#include "support/fixtures.h"
#include "support/world_fixtures.h"

namespace coney::test {

/// One synthetic triangle: three corners, flags (the enabled bit is set at load anyway), material and area byte.
struct Tri {
    raycast::Vec3 a, b, c;
    std::uint16_t flags = 0;
    std::uint8_t material = 5;
    std::uint8_t area = 1;
};

/// The six chunks of a synthetic collision mesh over the box [0, size]² × [-50, 50] with n × n × 1 cells: the matrix
/// maps the box onto 0..n-1 as the disc's do, and each triangle is listed in the cells of its bounding box, one cell
/// wider on every side (as the disc's lists are, roughly).
struct MeshChunks {
    Bytes header, lists, grid, triangles, vertices, checked;
};

inline MeshChunks meshChunks(const std::vector<Tri>& tris, std::uint16_t n = 8, float size = 80.0F) {
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
        for (const raycast::Vec3& v : {t.a, t.b, t.c}) {
            f32(f32(f32(f32(out.vertices, v.x), v.y), v.z), 1.0F);
        }
        out.triangles.u16(next).u16(static_cast<std::uint16_t>(next + 1)).u16(static_cast<std::uint16_t>(next + 2));
        out.triangles.u16(t.flags).u8(t.material).u8(t.area);
        next = static_cast<std::uint16_t>(next + 3);
    }
    out.checked.fill(((tris.size() + 7) / 8 + 15) & 0x7FF0U, 0);
    return out;
}

/// Builds the mesh of `tris`, requiring success.
inline std::unique_ptr<raycast::CollisionMesh> makeMesh(const std::vector<Tri>& tris, std::uint16_t n = 8) {
    MeshChunks c = meshChunks(tris, n);
    auto mesh = raycast::CollisionMesh::build(c.header.span(), c.lists.span(), c.grid.span(), c.triangles.span(),
                                              c.vertices.span(), c.checked.span());
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

/// A floor square at height z over [x0, x1] × [y0, y1], as two triangles facing up.
inline std::vector<Tri> floorAt(float z, float x0, float x1, float y0, float y1, std::uint8_t area = 1,
                                std::uint16_t flags = 0) {
    return {Tri{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, flags, 5, area},
            Tri{{x0, y0, z}, {x1, y1, z}, {x0, y1, z}, flags, 5, area}};
}

/// A wall in the plane x = `x` over y [y0, y1] and z [z0, z1], facing -x (two triangles).
inline std::vector<Tri> wallFacingMinusX(float x, float y0, float y1, float z0, float z1, std::uint16_t flags = 0) {
    return {Tri{{x, y0, z0}, {x, y0, z1}, {x, y1, z1}, flags, 5, 1},
            Tri{{x, y0, z0}, {x, y1, z1}, {x, y1, z0}, flags, 5, 1}};
}

/// A wall in the plane y = `y` over x [x0, x1] and z [z0, z1], facing -y (two triangles), with `flags` and `material`.
inline std::vector<Tri> wallFacingMinusY(float y, float x0, float x1, float z0, float z1, std::uint16_t flags = 0,
                                         std::uint8_t material = 5) {
    return {Tri{{x0, y, z0}, {x1, y, z0}, {x1, y, z1}, flags, material, 1},
            Tri{{x0, y, z0}, {x1, y, z1}, {x0, y, z1}, flags, material, 1}};
}

/// The same wall facing +y.
inline std::vector<Tri> wallFacingPlusY(float y, float x0, float x1, float z0, float z1, std::uint16_t flags = 0,
                                        std::uint8_t material = 5) {
    return {Tri{{x1, y, z0}, {x0, y, z0}, {x0, y, z1}, flags, material, 1},
            Tri{{x1, y, z0}, {x0, y, z1}, {x1, y, z1}, flags, material, 1}};
}

/// A block across x [x0, x1] from y0 to y1, `height` tall, standing on z = 0: its face toward -y, its top and its back
/// face, each with `flags` and `material`. A thin one is a fence; a deep one a ledge or a wall to climb.
inline std::vector<Tri> blockAlongY(float x0, float x1, float y0, float y1, float height, std::uint16_t flags = 0,
                                    std::uint8_t material = 5) {
    std::vector<Tri> tris = wallFacingMinusY(y0, x0, x1, 0.0F, height, flags, material);
    for (Tri top : floorAt(height, x0, x1, y0, y1, 1, flags)) {
        top.material = material;
        tris.push_back(top);
    }
    const std::vector<Tri> back = wallFacingPlusY(y1, x0, x1, 0.0F, height, flags, material);
    tris.insert(tris.end(), back.begin(), back.end());
    return tris;
}

/// Joins triangle lists.
inline std::vector<Tri> join(std::vector<Tri> a, const std::vector<Tri>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

} // namespace coney::test
