// SPDX-License-Identifier: GPL-3.0-or-later
#include "raycast/collision_mesh.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

#include "core/assert.h"
#include "fileio/reader.h"

namespace coney::raycast {

namespace {

// Header offsets (docs/research/collision.md#header).
constexpr std::size_t kMatrixOffset = 0x10;
constexpr std::size_t kClampMinOffset = 0x50;
constexpr std::size_t kClampMaxOffset = 0x60;
constexpr std::size_t kCellsOffset = 0x70;
constexpr std::size_t kListCountOffset = 0x7C;
constexpr std::size_t kTriangleCountOffset = 0x84;
constexpr std::size_t kVertexCountOffset = 0x90;
constexpr std::size_t kLowestZOffset = 0x98;

constexpr std::size_t kTriangleBytes = 10;
constexpr std::size_t kVertexBytes = 16;

// The intersection tests' determinant threshold and the sphere push's distance epsilon.
constexpr float kDeterminantEpsilon = 1e-6F;
constexpr float kPushEpsilon = 1e-5F;

// The flag bits a query's mask never excludes on: enabled and testable-while-disabled.
constexpr std::uint32_t kMaskIgnored = 0x801;

// Little-endian readers over buffers whose size the caller has checked.
std::uint16_t loadU16(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[at]) |
                                      (std::to_integer<std::uint16_t>(bytes[at + 1]) << 8U));
}
float loadF32(std::span<const std::byte> bytes, std::size_t at) {
    return std::bit_cast<float>(io::loadU32Le(bytes.subspan(at, 4)));
}

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Component `axis` (0 x, 1 y, 2 z) of `v`.
float component(Vec3 v, std::size_t axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

// Möller-Trumbore: the distance along `direction` from `origin` to the triangle (a, b, c), or -1 for a miss. A
// one-sided test rejects a determinant below the threshold (the back face); a two-sided one rejects a small one of
// either sign.
// @orig 0x00337920 RayTriangle_OneSided (unknown)
// @orig 0x00337a60 RayTriangle_TwoSided (unknown)
float rayTriangle(Vec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c, bool twoSided) {
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 p = cross(direction, e2);
    const float det = dot(e1, p);
    if (twoSided ? std::fabs(det) < kDeterminantEpsilon : det < kDeterminantEpsilon) {
        return -1.0F;
    }
    const float inverse = 1.0F / det;
    const Vec3 s = origin - a;
    const float u = dot(s, p) * inverse;
    if (u < 0.0F || u > 1.0F) {
        return -1.0F;
    }
    const Vec3 q = cross(s, e1);
    const float v = dot(direction, q) * inverse;
    if (v < 0.0F || u + v > 1.0F) {
        return -1.0F;
    }
    return dot(e2, q) * inverse;
}

// Whether `material` is in the exclusion list.
// @orig 0x00350538 CollisionTri_PassesMaterialFilter (CollisionMesh.cpp)
bool excluded(std::uint8_t material, std::span<const std::uint8_t> excludeMaterials) {
    return std::ranges::find(excludeMaterials, material) != excludeMaterials.end();
}

} // namespace

std::expected<std::unique_ptr<CollisionMesh>, Error>
CollisionMesh::build(std::span<const std::byte> header, std::span<const std::byte> lists,
                     std::span<const std::byte> grid, std::span<const std::byte> triangles,
                     std::span<const std::byte> vertices, std::span<const std::byte> checked) {
    if (header.size() < kCollisionHeaderBytes) {
        return fail(ErrorCode::Truncated,
                    std::format("collision header of {} bytes, expected {}", header.size(), kCollisionHeaderBytes));
    }
    std::unique_ptr<CollisionMesh> mesh(new CollisionMesh());

    // The header: matrix, clamp box, cells per axis and the counts.
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            mesh->m_matrix.at(row).at(column) = loadF32(header, kMatrixOffset + (row * 4 + column) * 4);
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        mesh->m_clampMin.at(axis) = loadF32(header, kClampMinOffset + axis * 4);
        mesh->m_clampMax.at(axis) = loadF32(header, kClampMaxOffset + axis * 4);
    }
    mesh->m_nx = loadU16(header, kCellsOffset);
    mesh->m_ny = loadU16(header, kCellsOffset + 2);
    mesh->m_nz = loadU16(header, kCellsOffset + 4);
    const std::uint32_t listCount = io::loadU32Le(header.subspan(kListCountOffset, 4));
    const std::uint32_t triangleCount = loadU16(header, kTriangleCountOffset);
    const std::uint32_t vertexCount = io::loadU32Le(header.subspan(kVertexCountOffset, 4));
    mesh->m_lowestZ = loadF32(header, kLowestZOffset);
    const std::uint64_t cellCount = std::uint64_t{mesh->m_nx} * mesh->m_ny * mesh->m_nz;
    if (cellCount == 0) {
        return fail(ErrorCode::Invalid, "the collision grid has no cells");
    }
    // The clamp box must keep every point inside the grid once truncated.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float limit = static_cast<float>(mesh->cells().at(axis));
        if (!(mesh->m_clampMin.at(axis) >= 0.0F) || !(mesh->m_clampMax.at(axis) < limit) ||
            mesh->m_clampMin.at(axis) > mesh->m_clampMax.at(axis)) {
            return fail(ErrorCode::Invalid, std::format("the collision grid's clamp box on axis {} is outside its {} "
                                                        "cells",
                                                        axis, mesh->cells().at(axis)));
        }
    }

    // Each chunk must hold what the header counts.
    const auto need = [](std::span<const std::byte> chunk, std::uint64_t bytes,
                         std::string_view what) -> std::expected<void, Error> {
        if (chunk.size() < bytes) {
            return fail(ErrorCode::Truncated,
                        std::format("collision {} of {} bytes; the header needs {}", what, chunk.size(), bytes));
        }
        return {};
    };
    const std::uint64_t checkedBytes = (((std::uint64_t{triangleCount} + 7) >> 3U) + 15) & 0x7FF0U;
    for (const auto& result : {need(grid, cellCount * 4, "grid"), need(lists, std::uint64_t{listCount} * 2, "lists"),
                               need(triangles, std::uint64_t{triangleCount} * kTriangleBytes, "triangles"),
                               need(vertices, std::uint64_t{vertexCount} * kVertexBytes, "vertices"),
                               need(checked, checkedBytes, "checked set")}) {
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    // Vertices (x, y, z, then w = 1, ignored) and triangles, every index in range.
    mesh->m_vertices.resize(vertexCount);
    for (std::uint32_t i = 0; i < vertexCount; ++i) {
        const std::size_t at = std::size_t{i} * kVertexBytes;
        mesh->m_vertices[i] = Vec3{loadF32(vertices, at), loadF32(vertices, at + 4), loadF32(vertices, at + 8)};
    }
    mesh->m_triangles.resize(triangleCount);
    for (std::uint32_t i = 0; i < triangleCount; ++i) {
        const std::size_t at = std::size_t{i} * kTriangleBytes;
        CollisionTriangle& triangle = mesh->m_triangles[i];
        for (std::size_t k = 0; k < 3; ++k) {
            triangle.vertices.at(k) = loadU16(triangles, at + k * 2);
            if (triangle.vertices.at(k) >= vertexCount) {
                return fail(ErrorCode::Invalid, std::format("collision triangle {} names vertex {} of {}", i,
                                                            triangle.vertices.at(k), vertexCount));
            }
        }
        // Enabled at load, whatever the file says (the original's handler sets bit 0 on every triangle).
        triangle.flags = static_cast<std::uint16_t>(loadU16(triangles, at + 6) | kTriangleEnabled);
        triangle.material = std::to_integer<std::uint8_t>(triangles[at + 8]);
        triangle.area = std::to_integer<std::uint8_t>(triangles[at + 9]);
    }

    // The index lists and the grid: every non-zero offset names a count and that many triangle indices in range.
    mesh->m_lists.resize(listCount);
    for (std::uint32_t i = 0; i < listCount; ++i) {
        mesh->m_lists[i] = loadU16(lists, std::size_t{i} * 2);
    }
    mesh->m_grid.resize(static_cast<std::size_t>(cellCount));
    for (std::size_t cell = 0; cell < mesh->m_grid.size(); ++cell) {
        const std::uint32_t offset = io::loadU32Le(grid.subspan(cell * 4, 4));
        mesh->m_grid[cell] = offset;
        if (offset == 0) {
            continue;
        }
        if (offset >= listCount || mesh->m_lists[offset] > listCount - offset - 1) {
            return fail(ErrorCode::Invalid,
                        std::format("collision cell {} points at a list past the {} index values", cell, listCount));
        }
        for (std::uint32_t k = 0; k < mesh->m_lists[offset]; ++k) {
            if (mesh->m_lists[offset + 1 + k] >= triangleCount) {
                return fail(ErrorCode::Invalid, std::format("collision cell {} lists triangle {} of {}", cell,
                                                            mesh->m_lists[offset + 1 + k], triangleCount));
            }
        }
    }
    mesh->m_checked.assign(static_cast<std::size_t>(checkedBytes), 0);
    return mesh;
}

Vec3 CollisionMesh::toGrid(Vec3 point) const {
    const auto row = [this](std::size_t r) { return Vec3{m_matrix.at(r)[0], m_matrix.at(r)[1], m_matrix.at(r)[2]}; };
    return row(0) * point.x + row(1) * point.y + row(2) * point.z + row(3);
}

std::array<std::uint32_t, 3> CollisionMesh::cellOf(Vec3 point) const {
    const Vec3 g = toGrid(point);
    std::array<std::uint32_t, 3> cell{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        cell.at(axis) =
            static_cast<std::uint32_t>(std::clamp(component(g, axis), m_clampMin.at(axis), m_clampMax.at(axis)));
    }
    return cell;
}

std::array<std::array<std::uint32_t, 2>, 3> CollisionMesh::cellBox(Vec3 a, Vec3 b) const {
    std::array<std::array<std::uint32_t, 2>, 3> box{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float lo = std::min(component(a, axis), component(b, axis));
        const float hi = std::max(component(a, axis), component(b, axis));
        box.at(axis)[0] = static_cast<std::uint32_t>(std::clamp(lo, m_clampMin.at(axis), m_clampMax.at(axis)));
        box.at(axis)[1] = static_cast<std::uint32_t>(std::clamp(hi, m_clampMin.at(axis), m_clampMax.at(axis)));
    }
    return box;
}

std::span<const std::uint16_t> CollisionMesh::cellTriangles(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
    CONEY_ASSERT(x < m_nx && y < m_ny && z < m_nz);
    const std::uint32_t offset = m_grid[(std::size_t{z} * m_ny + y) * m_nx + x];
    if (offset == 0) {
        return {};
    }
    return std::span<const std::uint16_t>(m_lists).subspan(offset + 1, m_lists[offset]);
}

Vec3 CollisionMesh::faceNormal(std::uint32_t index) const {
    const CollisionTriangle& triangle = m_triangles.at(index);
    const Vec3 a = m_vertices[triangle.vertices[0]];
    const Vec3 n = cross(m_vertices[triangle.vertices[1]] - a, m_vertices[triangle.vertices[2]] - a);
    const float length = std::sqrt(dot(n, n));
    return length > 0.0F ? n * (1.0F / length) : Vec3{};
}

void CollisionMesh::clearChecked() const { std::ranges::fill(m_checked, std::uint8_t{0}); }

bool CollisionMesh::checkOnce(std::uint32_t index) const {
    const auto bit = static_cast<std::uint8_t>(1U << (index & 7U));
    std::uint8_t& byte = m_checked[index >> 3U];
    if ((byte & bit) != 0) {
        return false;
    }
    byte = static_cast<std::uint8_t>(byte | bit);
    return true;
}

void CollisionMesh::rayTestCell(std::uint32_t x, std::uint32_t y, std::uint32_t z, const Ray& ray,
                                std::span<const std::uint8_t> excludeMaterials, std::uint32_t mask,
                                std::optional<RayHit>& best) const {
    for (const std::uint16_t index : cellTriangles(x, y, z)) {
        // Once per cast, however many cells list it.
        if (!checkOnce(index)) {
            continue;
        }
        const CollisionTriangle& triangle = m_triangles[index];
        if ((triangle.flags & 0xFFFU & mask & ~kMaskIgnored) != 0) {
            continue;
        }
        const bool enabled =
            (triangle.flags & kTriangleEnabled) != 0 || (mask & kTriangleEnabled) != 0 ||
            ((mask & kTriangleTestableDisabled) != 0 && (triangle.flags & kTriangleTestableDisabled) != 0);
        if (!enabled || excluded(triangle.material, excludeMaterials)) {
            continue;
        }
        const bool twoSided = (triangle.flags & kTriangleTwoSided) != 0;
        const float t = rayTriangle(ray.origin, ray.direction, m_vertices[triangle.vertices[0]],
                                    m_vertices[triangle.vertices[1]], m_vertices[triangle.vertices[2]], twoSided);
        if (t < 0.0F || t > ray.length || (best && !(t < best->t))) {
            continue;
        }
        Vec3 normal = faceNormal(index);
        if (twoSided && dot(normal, ray.direction) > 0.0F) {
            normal = normal * -1.0F;
        }
        best = RayHit{.normal = normal,
                      .t = t,
                      .material = triangle.material,
                      .flags = static_cast<std::uint16_t>(triangle.flags & 0xFFFU),
                      .area = triangle.area,
                      .triangle = index};
    }
}

std::optional<RayHit> CollisionMesh::rayCast(const Ray& ray, std::span<const std::uint8_t> excludeMaterials,
                                             std::uint32_t mask) const {
    // 1. Both ends in grid space and the cell box they span.
    const Vec3 start = toGrid(ray.origin);
    const Vec3 end = toGrid(ray.origin + ray.direction * ray.length);
    const auto box = cellBox(start, end);
    const std::uint32_t xmin = box[0][0];
    const std::uint32_t xmax = box[0][1];
    const std::uint32_t ymin = box[1][0];
    const std::uint32_t ymax = box[1][1];
    const std::uint32_t zmin = box[2][0];
    const std::uint32_t zmax = box[2][1];
    // 2. A fresh checked set.
    clearChecked();
    std::optional<RayHit> best;
    // Tests the cells (x, y0..y1, every z of the box).
    const auto testColumn = [&](std::uint32_t x, std::uint32_t y0, std::uint32_t y1) {
        for (std::uint32_t y = y0; y <= y1; ++y) {
            for (std::uint32_t z = zmin; z <= zmax; ++z) {
                rayTestCell(x, y, z, ray, excludeMaterials, mask, best);
            }
        }
    };
    if (xmin == xmax || ymin == ymax) {
        // 3. One row or column: every cell of the box.
        for (std::uint32_t x = xmin; x <= xmax; ++x) {
            testColumn(x, ymin, ymax);
        }
        return best;
    }
    // 4. Walk the x columns along the ray's line in grid space (from the unclamped ends), its y at each column's
    // edges ix and ix + 1, truncated and kept within the box. The edges sit on integers while cells are centred on
    // them: the original's arithmetic, kept so the same cells are tested.
    const float slope = (end.y - start.y) / (end.x - start.x);
    const float intercept = start.y - slope * start.x;
    for (std::uint32_t ix = xmin; ix <= xmax; ++ix) {
        const auto yAt = [&](float x) { return static_cast<std::uint32_t>(std::max(slope * x + intercept, 0.0F)); };
        std::uint32_t y0 = yAt(static_cast<float>(ix));
        std::uint32_t y1 = yAt(static_cast<float>(ix) + 1.0F);
        if (y0 > y1) {
            std::swap(y0, y1);
        }
        testColumn(ix, std::clamp(y0, ymin, ymax), std::clamp(y1, ymin, ymax));
    }
    return best;
}

SpherePushResult CollisionMesh::spherePush(float radius, Vec3 centre,
                                           std::span<const std::uint8_t> excludeMaterials) const {
    SpherePushResult result{.touched = false, .centre = centre, .firstNormal = {}};
    // A wall leans at most 15 degrees from vertical.
    const float wallLimit = std::cos(15.0F * std::numbers::pi_v<float> / 180.0F);
    const Vec3 reach{radius, radius, radius};
    const auto box = cellBox(toGrid(centre - reach), toGrid(centre + reach));
    clearChecked();
    Vec3 push;
    std::uint32_t count = 0;
    for (std::uint32_t z = box[2][0]; z <= box[2][1]; ++z) {
        for (std::uint32_t y = box[1][0]; y <= box[1][1]; ++y) {
            for (std::uint32_t x = box[0][0]; x <= box[0][1]; ++x) {
                // @orig 0x00351468 CollisionMesh_SphereTestCell (CollisionMesh.cpp)
                for (const std::uint16_t index : cellTriangles(x, y, z)) {
                    if (!checkOnce(index)) {
                        continue;
                    }
                    const CollisionTriangle& triangle = m_triangles[index];
                    if ((triangle.flags & kTriangleEnabled) == 0 || excluded(triangle.material, excludeMaterials)) {
                        continue;
                    }
                    const Vec3 face = faceNormal(index);
                    if (std::fabs(face.z) > wallLimit || face == Vec3{}) {
                        continue; // floors and ceilings never push
                    }
                    const Vec3 a = m_vertices[triangle.vertices[0]];
                    const Vec3 b = m_vertices[triangle.vertices[1]];
                    const Vec3 c = m_vertices[triangle.vertices[2]];
                    Vec3 n = face;
                    float dist = dot(n, centre) - dot(n, a);
                    if (dist < -kPushEpsilon) {
                        if ((triangle.flags & kTriangleTwoSided) == 0) {
                            continue; // behind a one-sided wall
                        }
                        n = n * -1.0F;
                        dist = -dist;
                    }
                    if (!(radius - dist > kPushEpsilon)) {
                        continue;
                    }
                    // The centre's projection must lie inside all three edges: against each edge's outward
                    // direction (edge × face normal; the face's own winding, whichever side the sphere is on).
                    const auto inside = [&](Vec3 from, Vec3 to) {
                        return dot(cross(to - from, face), centre - from) <= 0.0F;
                    };
                    if (!inside(a, b) || !inside(b, c) || !inside(c, a)) {
                        continue;
                    }
                    push = push + n * (radius - dist);
                    ++count;
                    if (result.firstNormal == Vec3{}) {
                        result.firstNormal = n;
                    }
                }
            }
        }
    }
    if (count > 0) {
        result.touched = true;
        result.centre = centre + push * (1.0F / static_cast<float>(count)); // the average push, not the sum
    }
    return result;
}

void CollisionMesh::setEnabledInBox(Vec3 min, Vec3 max, bool on) {
    const auto box = cellBox(toGrid(min), toGrid(max));
    const auto within = [&](Vec3 p) {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    };
    for (std::uint32_t z = box[2][0]; z <= box[2][1]; ++z) {
        for (std::uint32_t y = box[1][0]; y <= box[1][1]; ++y) {
            for (std::uint32_t x = box[0][0]; x <= box[0][1]; ++x) {
                // @orig 0x00350aa0 CollisionMesh_SetEnabledInBoxCell (CollisionMesh.cpp)
                for (const std::uint16_t index : cellTriangles(x, y, z)) {
                    CollisionTriangle& triangle = m_triangles[index];
                    if (!within(m_vertices[triangle.vertices[0]]) || !within(m_vertices[triangle.vertices[1]]) ||
                        !within(m_vertices[triangle.vertices[2]])) {
                        continue;
                    }
                    triangle.flags = on ? static_cast<std::uint16_t>(triangle.flags | kTriangleEnabled)
                                        : static_cast<std::uint16_t>(triangle.flags & ~kTriangleEnabled);
                }
            }
        }
    }
}

std::expected<void, Error> onCollisionMeshLoaded(chunk::ChunkStacks& stacks, std::uint32_t /*type*/) {
    // The header was loaded last; the other five were written before it, so they come off in reverse file order.
    std::array<chunk::ChunkData, 6> chunks;
    constexpr std::array<std::uint32_t, 6> kOrder{kCollisionMeshChunk, kCollisionStrings,      kCollisionGrid,
                                                  kCollisionTriangles, kCollisionVertexBuffer, kCollisionChecked};
    for (std::size_t i = 0; i < kOrder.size(); ++i) {
        auto chunk = stacks.popChunk(kOrder.at(i));
        if (!chunk) {
            return std::unexpected(std::move(chunk.error()));
        }
        chunks.at(i) = std::move(*chunk);
    }
    auto mesh = CollisionMesh::build(chunks[0].bytes, chunks[1].bytes, chunks[2].bytes, chunks[3].bytes,
                                     chunks[4].bytes, chunks[5].bytes);
    if (!mesh) {
        return std::unexpected(std::move(mesh.error()));
    }
    stacks.pushObject(std::move(*mesh));
    return {};
}

bool dropToGround(const CollisionMesh& mesh, float length, Vec3& position) {
    const auto hit = mesh.rayCast(Ray{.origin = position, .direction = kDown, .length = length}, {}, 0);
    if (!hit) {
        return false;
    }
    position.z -= hit->t - 0.1F; // 0.1 above the ground
    return true;
}

bool dropToMarkedGround(const CollisionMesh& mesh, float length, Vec3& position) {
    Vec3 origin = position;
    float left = length;
    for (;;) {
        const auto hit = mesh.rayCast(Ray{.origin = origin, .direction = kDown, .length = left}, {}, 0);
        if (!hit) {
            return false;
        }
        if (hit->area != 0) {
            position = Vec3{origin.x, origin.y, origin.z - hit->t + 0.25F}; // 0.25 above the marked ground
            return true;
        }
        // An unmarked triangle: go on from just below it.
        origin.z -= hit->t + 0.01F;
        left -= hit->t + 0.01F;
        if (!(left > 0.0F)) {
            return false;
        }
    }
}

std::optional<Vec3> marchRay(const CollisionMesh& mesh, float step, float max, Vec3 origin, Vec3 direction) {
    if (!(step > 0.0F)) {
        return std::nullopt;
    }
    float covered = 0.0F;
    Vec3 from = origin;
    while (covered < max) {
        const float length = std::min(step, max - covered);
        if (const auto hit = mesh.rayCast(Ray{.origin = from, .direction = direction, .length = length}, {}, 0)) {
            return from + direction * hit->t;
        }
        from = from + direction * length;
        covered += length;
    }
    return std::nullopt;
}

} // namespace coney::raycast
