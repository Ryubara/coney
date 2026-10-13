// SPDX-License-Identifier: GPL-3.0-or-later
#include "raycast/collision_builder.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace coney::raycast {

namespace {

// The most cells per axis the builder makes: well inside the header's u16 counts, and plenty for any sandbox.
constexpr float kMaxCellsPerAxis = 4096.0F;

// Little-endian appenders for the chunk bytes.
void putU8(std::vector<std::byte>& out, std::uint8_t value) { out.push_back(static_cast<std::byte>(value)); }
void putU16(std::vector<std::byte>& out, std::uint16_t value) {
    putU8(out, static_cast<std::uint8_t>(value & 0xFFU));
    putU8(out, static_cast<std::uint8_t>(value >> 8U));
}
void putU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        putU8(out, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}
void putF32(std::vector<std::byte>& out, float value) { putU32(out, std::bit_cast<std::uint32_t>(value)); }

// One horizontal axis of the grid: how many cells and how a coordinate maps onto them (g = x · scale + offset).
struct GridAxis {
    std::uint16_t cells = 1;
    float scale = 0.0F;
    float offset = 0.0F;

    // The cell a coordinate falls in: the format's point-to-cell rule, clamped to [0.5, n - 0.5] and truncated.
    [[nodiscard]] std::uint32_t cellOf(float coordinate) const {
        const float g = std::clamp(coordinate * scale + offset, 0.5F, static_cast<float>(cells) - 0.5F);
        return static_cast<std::uint32_t>(g);
    }
};

// The axis over [low, high] with cells about `cellSize` wide: the box maps onto 0 to n - 1, as on the disc.
GridAxis makeAxis(float low, float high, float cellSize) {
    const float extent = high - low;
    GridAxis axis;
    if (!(extent > 0.0F)) {
        return axis; // flat along this axis: one cell, everything maps to it
    }
    const float wanted = std::min(std::ceil(extent / cellSize) + 1.0F, kMaxCellsPerAxis);
    axis.cells = static_cast<std::uint16_t>(std::max(wanted, 2.0F));
    axis.scale = static_cast<float>(axis.cells - 1) / extent;
    axis.offset = -low * axis.scale;
    return axis;
}

} // namespace

std::expected<CollisionChunks, Error> encodeCollisionMesh(std::span<const BuildTriangle> triangles, float cellSize) {
    if (triangles.empty() || triangles.size() > kMaxCollisionTriangles) {
        return fail(ErrorCode::InvalidArgument, std::format("a collision mesh needs 1 to {} triangles, got {}",
                                                            kMaxCollisionTriangles, triangles.size()));
    }
    if (!(cellSize > 0.0F) || !std::isfinite(cellSize)) {
        return fail(ErrorCode::InvalidArgument, "a collision grid's cells must be wider than 0");
    }

    // 1. The vertices, identical corners merged, and the bounding box.
    std::map<std::tuple<float, float, float>, std::uint16_t> indexOf;
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint16_t, 3>> corners(triangles.size());
    Vec3 low{triangles[0].corners[0]};
    Vec3 high{low};
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 v = triangles[t].corners.at(k);
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                return fail(ErrorCode::InvalidArgument, std::format("collision triangle {} has a corner that is not "
                                                                    "a number",
                                                                    t));
            }
            const auto [found, added] = indexOf.try_emplace({v.x, v.y, v.z}, static_cast<std::uint16_t>(0));
            if (added) {
                if (vertices.size() == kMaxCollisionTriangles) {
                    return fail(ErrorCode::InvalidArgument,
                                std::format("a collision mesh holds at most {} vertices", kMaxCollisionTriangles));
                }
                found->second = static_cast<std::uint16_t>(vertices.size());
                vertices.push_back(v);
            }
            corners[t].at(k) = found->second;
            low = Vec3{std::min(low.x, v.x), std::min(low.y, v.y), std::min(low.z, v.z)};
            high = Vec3{std::max(high.x, v.x), std::max(high.y, v.y), std::max(high.z, v.z)};
        }
    }

    // 2. The grid's axes, and each cell's triangles: the cells of the triangle's box, one wider on every side.
    const GridAxis ax = makeAxis(low.x, high.x, cellSize);
    const GridAxis ay = makeAxis(low.y, high.y, cellSize);
    std::vector<std::vector<std::uint16_t>> cellTriangles(std::size_t{ax.cells} * ay.cells);
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        const auto& c = triangles[t].corners;
        const auto [minX, maxX] = std::minmax({c[0].x, c[1].x, c[2].x});
        const auto [minY, maxY] = std::minmax({c[0].y, c[1].y, c[2].y});
        const std::uint32_t x0 = ax.cellOf(minX) > 0 ? ax.cellOf(minX) - 1 : 0;
        const std::uint32_t x1 = std::min<std::uint32_t>(ax.cellOf(maxX) + 1, ax.cells - 1U);
        const std::uint32_t y0 = ay.cellOf(minY) > 0 ? ay.cellOf(minY) - 1 : 0;
        const std::uint32_t y1 = std::min<std::uint32_t>(ay.cellOf(maxY) + 1, ay.cells - 1U);
        for (std::uint32_t y = y0; y <= y1; ++y) {
            for (std::uint32_t x = x0; x <= x1; ++x) {
                cellTriangles[std::size_t{y} * ax.cells + x].push_back(static_cast<std::uint16_t>(t));
            }
        }
    }

    // 3. The lists (a leading 0, so offset 0 reads as an empty cell) and the grid of offsets into them.
    CollisionChunks out;
    std::vector<std::uint16_t> values{0};
    for (const std::vector<std::uint16_t>& list : cellTriangles) {
        if (list.empty()) {
            putU32(out.grid, 0);
            continue;
        }
        putU32(out.grid, static_cast<std::uint32_t>(values.size()));
        values.push_back(static_cast<std::uint16_t>(list.size()));
        values.insert(values.end(), list.begin(), list.end());
    }
    for (const std::uint16_t value : values) {
        putU16(out.lists, value);
    }

    // 4. The header: 16 bytes where the vtable goes, the matrix (rows r0-r3), the clamp box, the counts.
    out.header.resize(16);
    for (const std::array<float, 4>& row :
         {std::array<float, 4>{ax.scale, 0.0F, 0.0F, 0.0F}, std::array<float, 4>{0.0F, ay.scale, 0.0F, 0.0F},
          std::array<float, 4>{0.0F, 0.0F, 0.0F, 0.0F}, std::array<float, 4>{ax.offset, ay.offset, 0.0F, 1.0F}}) {
        for (const float value : row) {
            putF32(out.header, value);
        }
    }
    for (const float value : {0.5F, 0.5F, 0.5F, 1.0F}) {
        putF32(out.header, value);
    }
    for (const float value : {static_cast<float>(ax.cells) - 0.5F, static_cast<float>(ay.cells) - 0.5F, 0.5F, 1.0F}) {
        putF32(out.header, value);
    }
    putU16(out.header, ax.cells);
    putU16(out.header, ay.cells);
    putU16(out.header, 1);
    putU16(out.header, 0);
    putU32(out.header, 0); // +0x78 grid pointer
    putU32(out.header, static_cast<std::uint32_t>(values.size()));
    putU32(out.header, 0); // +0x80 list pointer
    putU16(out.header, static_cast<std::uint16_t>(triangles.size()));
    putU16(out.header, 0);
    putU32(out.header, 0); // +0x88 triangle pointer
    putU32(out.header, 0); // +0x8c vertex pointer
    putU32(out.header, static_cast<std::uint32_t>(vertices.size()));
    putU32(out.header, 0); // +0x94 checked pointer
    putF32(out.header, low.z);
    putU32(out.header, 0);

    // 5. Triangles, vertices (w = 1) and the zeroed checked set, sized as the original sizes it.
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        for (const std::uint16_t index : corners[t]) {
            putU16(out.triangles, index);
        }
        putU16(out.triangles, triangles[t].flags);
        putU8(out.triangles, triangles[t].material);
        putU8(out.triangles, triangles[t].area);
    }
    for (const Vec3& v : vertices) {
        putF32(out.vertices, v.x);
        putF32(out.vertices, v.y);
        putF32(out.vertices, v.z);
        putF32(out.vertices, 1.0F);
    }
    out.checked.resize((((triangles.size() + 7) >> 3U) + 15) & 0x7FF0U);
    return out;
}

std::expected<std::unique_ptr<CollisionMesh>, Error> buildCollisionMesh(std::span<const BuildTriangle> triangles,
                                                                        float cellSize) {
    auto chunks = encodeCollisionMesh(triangles, cellSize);
    if (!chunks) {
        return std::unexpected(std::move(chunks.error()));
    }
    return CollisionMesh::build(chunks->header, chunks->lists, chunks->grid, chunks->triangles, chunks->vertices,
                                chunks->checked);
}

std::expected<void, Error> CollisionMesh::addExtraTriangles(std::span<const BuildTriangle> triangles) {
    if (triangles.empty()) {
        return {};
    }
    // 1. Everything is checked before anything changes: the counts, the corners (identical ones merged, as the
    // encoder merges them) and each cell's list length.
    if (m_triangles.size() + triangles.size() > kMaxCollisionTriangles) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("the collision mesh has {} triangles; {} more would pass the {} it can hold",
                                m_triangles.size(), triangles.size(), kMaxCollisionTriangles));
    }
    std::map<std::tuple<float, float, float>, std::uint16_t> indexOf;
    std::vector<Vec3> added;
    std::vector<std::array<std::uint16_t, 3>> corners(triangles.size());
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 v = triangles[t].corners.at(k);
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                return fail(ErrorCode::InvalidArgument,
                            std::format("extra collision triangle {} has a corner that is not a number", t));
            }
            const auto [found, isNew] = indexOf.try_emplace({v.x, v.y, v.z}, static_cast<std::uint16_t>(0));
            if (isNew) {
                if (m_vertices.size() + added.size() >= kMaxCollisionTriangles) {
                    return fail(ErrorCode::InvalidArgument,
                                std::format("a collision mesh holds at most {} vertices", kMaxCollisionTriangles));
                }
                found->second = static_cast<std::uint16_t>(m_vertices.size() + added.size());
                added.push_back(v);
            }
            corners[t].at(k) = found->second;
        }
    }
    // Each triangle's cells: the grid-space box of its corners (the matrix is affine, so that box holds the whole
    // triangle), clamped as the queries clamp, one cell wider on every side.
    const std::array<std::uint32_t, 3> limits = cells();
    std::map<std::size_t, std::vector<std::uint16_t>> entries; // cell -> its new triangles, in order
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        Vec3 lo = toGrid(triangles[t].corners[0]);
        Vec3 hi = lo;
        for (std::size_t k = 1; k < 3; ++k) {
            const Vec3 g = toGrid(triangles[t].corners.at(k));
            lo = Vec3{std::min(lo.x, g.x), std::min(lo.y, g.y), std::min(lo.z, g.z)};
            hi = Vec3{std::max(hi.x, g.x), std::max(hi.y, g.y), std::max(hi.z, g.z)};
        }
        std::array<std::array<std::uint32_t, 2>, 3> box = cellBox(lo, hi);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            box.at(axis)[0] = box.at(axis)[0] > 0 ? box.at(axis)[0] - 1 : 0;
            box.at(axis)[1] = std::min(box.at(axis)[1] + 1, limits.at(axis) - 1);
        }
        const auto index = static_cast<std::uint16_t>(m_triangles.size() + t);
        for (std::uint32_t z = box[2][0]; z <= box[2][1]; ++z) {
            for (std::uint32_t y = box[1][0]; y <= box[1][1]; ++y) {
                for (std::uint32_t x = box[0][0]; x <= box[0][1]; ++x) {
                    entries[(std::size_t{z} * m_ny + y) * m_nx + x].push_back(index);
                }
            }
        }
    }
    for (const auto& [cell, list] : entries) {
        const std::uint32_t offset = m_grid[cell];
        const std::size_t own = offset == 0 ? 0 : m_lists[offset];
        if (own + list.size() > 0xFFFFU) {
            return fail(
                ErrorCode::InvalidArgument,
                std::format("collision cell {} would list {} triangles; at most 65535 fit", cell, own + list.size()));
        }
    }

    // 2. The mesh as built is kept once, for clearExtraTriangles().
    if (!m_built) {
        m_built = Built{.triangles = m_triangles.size(),
                        .vertices = m_vertices.size(),
                        .lists = m_lists.size(),
                        .checked = m_checked.size(),
                        .grid = m_grid};
    }

    // 3. The vertices and triangles go after the mesh's own, enabled as a loaded triangle is.
    m_vertices.insert(m_vertices.end(), added.begin(), added.end());
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        m_triangles.push_back(
            CollisionTriangle{.vertices = corners[t],
                              .flags = static_cast<std::uint16_t>(triangles[t].flags | kTriangleEnabled),
                              .material = triangles[t].material,
                              .area = triangles[t].area});
    }
    // 4. Each cell that gains triangles gets a new list at the end of the lists: its own entries, then the new ones.
    // Its old list stays where it was, since other cells may share it. Offset 0 reads as an empty cell, so a mesh
    // without lists gets a leading 0 first.
    if (m_lists.empty()) {
        m_lists.push_back(0);
    }
    for (const auto& [cell, list] : entries) {
        const std::uint32_t offset = m_grid[cell];
        std::vector<std::uint16_t> own;
        if (offset != 0) {
            const auto first = m_lists.begin() + offset + 1;
            own.assign(first, first + m_lists[offset]);
        }
        m_grid[cell] = static_cast<std::uint32_t>(m_lists.size());
        m_lists.push_back(static_cast<std::uint16_t>(own.size() + list.size()));
        m_lists.insert(m_lists.end(), own.begin(), own.end());
        m_lists.insert(m_lists.end(), list.begin(), list.end());
    }
    // 5. The checked set covers the new triangles: the size the format's rule gives for the new count.
    const std::size_t checked = (((m_triangles.size() + 7) >> 3U) + 15) & ~std::size_t{15};
    if (checked > m_checked.size()) {
        m_checked.resize(checked, 0);
    }
    return {};
}

void CollisionMesh::clearExtraTriangles() {
    if (!m_built) {
        return;
    }
    m_triangles.resize(m_built->triangles);
    m_vertices.resize(m_built->vertices);
    m_lists.resize(m_built->lists);
    m_checked.resize(m_built->checked);
    m_grid = std::move(m_built->grid);
    m_built.reset();
}

std::size_t CollisionMesh::extraTriangleCount() const { return m_built ? m_triangles.size() - m_built->triangles : 0; }

} // namespace coney::raycast
