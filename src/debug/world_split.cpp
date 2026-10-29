// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the debug tools' world edits; the original has none (see world_split.h).
#include "debug/world_split.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <numeric>
#include <tuple>
#include <utility>

namespace coney::debug {

namespace {

// A cell of the weld grid, by its three integer coordinates.
using WeldCell = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

// A point's cell on a grid of `size` (a weld distance): points closer than it fall in the same or a neighbouring cell.
WeldCell weldCellOf(Vec3 p, float size) {
    return {static_cast<std::int64_t>(std::floor(p.x / size)), static_cast<std::int64_t>(std::floor(p.y / size)),
            static_cast<std::int64_t>(std::floor(p.z / size))};
}

// The root of `i` in a union-find forest, halving the path on the way.
std::uint32_t findRoot(std::vector<std::uint32_t>& parent, std::uint32_t i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

// Joins the sets of `a` and `b`.
void join(std::vector<std::uint32_t>& parent, std::uint32_t a, std::uint32_t b) {
    a = findRoot(parent, a);
    b = findRoot(parent, b);
    if (a != b) {
        parent[std::max(a, b)] = std::min(a, b);
    }
}

// Vertex `v` of a mesh's list, or the origin for an index past its end (damaged input never reads out of bounds).
Vec3 vertexAt(std::span<const Vec3> positions, std::uint32_t v) { return v < positions.size() ? positions[v] : Vec3{}; }

// The centroid of triangle `t`.
Vec3 centroidOf(std::span<const Vec3> positions, const MeshTriangle& t) {
    const Vec3 a = vertexAt(positions, t[0]);
    const Vec3 b = vertexAt(positions, t[1]);
    const Vec3 c = vertexAt(positions, t[2]);
    return Vec3{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F, (a.z + b.z + c.z) / 3.0F};
}

// a - b.
Vec3 minus(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }

// a + b.
Vec3 plus(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }

// The cross product a × b.
Vec3 cross(Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

} // namespace

bool GameBox::contains(Vec3 point, float margin) const {
    return point.x >= min.x - margin && point.x <= max.x + margin && point.y >= min.y - margin &&
           point.y <= max.y + margin && point.z >= min.z - margin && point.z <= max.z + margin;
}

MeshPieces connectedPieces(std::span<const Vec3> positions, std::span<const MeshTriangle> triangles, float weld) {
    // One set per vertex; triangles join their corners, and welded copies join through the grid of `weld` cells.
    std::vector<std::uint32_t> parent(positions.size());
    std::iota(parent.begin(), parent.end(), 0U);
    if (weld > 0.0F) {
        std::map<WeldCell, std::vector<std::uint32_t>> cells;
        for (std::uint32_t v = 0; v < positions.size(); ++v) {
            cells[weldCellOf(positions[v], weld)].push_back(v);
        }
        // A copy within `weld` lies in the same cell or one of its 26 neighbours.
        for (std::uint32_t v = 0; v < positions.size(); ++v) {
            const auto [cx, cy, cz] = weldCellOf(positions[v], weld);
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                for (std::int64_t dy = -1; dy <= 1; ++dy) {
                    for (std::int64_t dz = -1; dz <= 1; ++dz) {
                        const auto it = cells.find(WeldCell{cx + dx, cy + dy, cz + dz});
                        if (it == cells.end()) {
                            continue;
                        }
                        for (const std::uint32_t w : it->second) {
                            const Vec3 d = minus(positions[v], positions[w]);
                            if (std::abs(d.x) <= weld && std::abs(d.y) <= weld && std::abs(d.z) <= weld) {
                                join(parent, v, w);
                            }
                        }
                    }
                }
            }
        }
    }
    for (const MeshTriangle& t : triangles) {
        if (t[0] < parent.size() && t[1] < parent.size() && t[2] < parent.size()) {
            join(parent, t[0], t[1]);
            join(parent, t[0], t[2]);
        }
    }
    // Number the pieces by their first triangle.
    MeshPieces pieces;
    pieces.pieceOf.reserve(triangles.size());
    std::map<std::uint32_t, std::uint32_t> numberOf;
    for (const MeshTriangle& t : triangles) {
        const std::uint32_t root = t[0] < parent.size() ? findRoot(parent, t[0]) : 0;
        const auto [it, added] = numberOf.try_emplace(root, pieces.count);
        if (added) {
            ++pieces.count;
        }
        pieces.pieceOf.push_back(it->second);
    }
    return pieces;
}

std::vector<std::uint8_t> trianglesInBox(std::span<const Vec3> positions, std::span<const MeshTriangle> triangles,
                                         const GameBox& box, CutRule rule, float weld) {
    std::vector<std::uint8_t> take(triangles.size(), 0);
    const auto cornersIn = [&](const MeshTriangle& t) {
        return box.contains(vertexAt(positions, t[0]), weld) && box.contains(vertexAt(positions, t[1]), weld) &&
               box.contains(vertexAt(positions, t[2]), weld);
    };
    switch (rule) {
    case CutRule::Centroid:
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            take[i] = box.contains(centroidOf(positions, triangles[i]), weld) ? 1 : 0;
        }
        break;
    case CutRule::AllVertices:
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            take[i] = cornersIn(triangles[i]) ? 1 : 0;
        }
        break;
    case CutRule::Pieces: {
        // A piece is taken when every one of its triangles lies in the box.
        const MeshPieces pieces = connectedPieces(positions, triangles, weld);
        std::vector<std::uint8_t> inside(pieces.count, 1);
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            if (!cornersIn(triangles[i])) {
                inside[pieces.pieceOf[i]] = 0;
            }
        }
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            take[i] = inside[pieces.pieceOf[i]];
        }
        break;
    }
    }
    return take;
}

std::vector<raycast::BuildTriangle> upFacingCollision(std::span<const Vec3> positions,
                                                      std::span<const MeshTriangle> triangles,
                                                      std::span<const std::uint8_t> take, float minUpZ,
                                                      std::uint8_t material) {
    std::vector<raycast::BuildTriangle> out;
    for (std::size_t i = 0; i < triangles.size() && i < take.size(); ++i) {
        if (take[i] == 0) {
            continue;
        }
        const Vec3 a = vertexAt(positions, triangles[i][0]);
        Vec3 b = vertexAt(positions, triangles[i][1]);
        Vec3 c = vertexAt(positions, triangles[i][2]);
        const Vec3 n = cross(minus(b, a), minus(c, a));
        const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (length <= 1e-6F) {
            continue; // degenerate
        }
        // Facing up or down by `minUpZ` either way: a visible face's winding says nothing of which side is solid, so
        // a roof seen from above is kept whichever way it was wound, and wound to face up.
        if (std::abs(n.z / length) < minUpZ) {
            continue;
        }
        if (n.z < 0.0F) {
            std::swap(b, c);
        }
        raycast::BuildTriangle t;
        t.corners = {a, b, c};
        t.material = material;
        out.push_back(t);
    }
    return out;
}

std::vector<raycast::BuildTriangle> collisionInBox(const raycast::CollisionMesh& mesh, const GameBox& box,
                                                   Vec3 offset) {
    std::vector<raycast::BuildTriangle> out;
    const std::span<const raycast::Vec3> vertices = mesh.vertices();
    for (const raycast::CollisionTriangle& t : mesh.triangles()) {
        if ((t.flags & raycast::kTriangleEnabled) == 0) {
            continue;
        }
        raycast::BuildTriangle moved;
        bool inside = true;
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3 v = t.vertices.at(k) < vertices.size() ? vertices[t.vertices.at(k)] : Vec3{};
            inside = inside && box.contains(v);
            moved.corners.at(k) = plus(v, offset);
        }
        if (!inside) {
            continue;
        }
        moved.flags = t.flags;
        moved.material = t.material;
        moved.area = t.area;
        out.push_back(moved);
    }
    return out;
}

std::vector<SpreadPlacement> spreadLayout(std::span<const GameBox> boxes, std::uint32_t columns, float gap, float scale,
                                          Vec3 origin, std::optional<float> fitCell) {
    // Every cell as large as the largest footprint, scaled, plus the gap; or the fitted cell plus the gap.
    const auto footprintOf = [](const GameBox& box) { return std::max(box.max.x - box.min.x, box.max.y - box.min.y); };
    float footprint = 0.0F;
    for (const GameBox& box : boxes) {
        footprint = std::max(footprint, footprintOf(box));
    }
    const float cell = (fitCell ? *fitCell : footprint * scale) + gap;
    const std::uint32_t across = std::max(columns, 1U);
    std::vector<SpreadPlacement> placed;
    placed.reserve(boxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const GameBox& box = boxes[i];
        const Vec3 centre{(box.min.x + box.max.x) * 0.5F, (box.min.y + box.max.y) * 0.5F,
                          (box.min.z + box.max.z) * 0.5F};
        const float own = fitCell ? (footprintOf(box) > 0.0F ? *fitCell / footprintOf(box) : 1.0F) : scale;
        // The cell's column and row are whole numbers on purpose: the grid fills a row, then the next.
        const std::size_t row = i / across;
        const Vec3 target{origin.x + static_cast<float>(i % across) * cell, origin.y - static_cast<float>(row) * cell,
                          origin.z + (box.max.z - box.min.z) * 0.5F * own};
        // The centre stays where it is under the scaling; then it moves to the cell's centre, half the scaled height
        // above the floor.
        placed.push_back(SpreadPlacement{centre, own, minus(target, centre)});
    }
    return placed;
}

Vec3 offsetAt(std::span<const OffsetKey> keys, std::uint64_t step) {
    if (keys.empty() || step < keys.front().step) {
        return Vec3{};
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (step < keys[i].step) {
            const OffsetKey& a = keys[i - 1];
            const OffsetKey& b = keys[i];
            const float t = static_cast<float>(step - a.step) / static_cast<float>(b.step - a.step);
            return Vec3{a.offset.x + (b.offset.x - a.offset.x) * t, a.offset.y + (b.offset.y - a.offset.y) * t,
                        a.offset.z + (b.offset.z - a.offset.z) * t};
        }
    }
    return keys.back().offset;
}

float pointTriangleDistance(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    // The closest point by the triangle's Voronoi regions (corners, edges, then the face), from Ericson's Real-Time
    // Collision Detection, 5.1.5.
    const auto dot = [](Vec3 u, Vec3 v) { return u.x * v.x + u.y * v.y + u.z * v.z; };
    const auto scaled = [](Vec3 v, float k) { return Vec3{v.x * k, v.y * k, v.z * k}; };
    const auto length = [&dot](Vec3 v) { return std::sqrt(dot(v, v)); };
    const Vec3 ab = minus(b, a);
    const Vec3 ac = minus(c, a);
    const Vec3 ap = minus(p, a);
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return length(ap);
    }
    const Vec3 bp = minus(p, b);
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return length(bp);
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return length(minus(p, plus(a, scaled(ab, d1 / (d1 - d3)))));
    }
    const Vec3 cp = minus(p, c);
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return length(cp);
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return length(minus(p, plus(a, scaled(ac, d2 / (d2 - d6)))));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        return length(minus(p, plus(b, scaled(minus(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))))));
    }
    const float sum = va + vb + vc;
    if (std::abs(sum) < 1e-12F) {
        return length(ap); // degenerate: a line or a point
    }
    const float v = vb / sum;
    const float w = vc / sum;
    return length(minus(p, plus(a, plus(scaled(ab, v), scaled(ac, w)))));
}

TriangleGrid::TriangleGrid(std::span<const Vec3> positions, std::span<const MeshTriangle> triangles, float cell)
    : m_positions(positions), m_triangles(triangles), m_cell(cell > 0.0F ? cell : 1.0F) {
    for (std::uint32_t i = 0; i < triangles.size(); ++i) {
        const Vec3 a = vertexAt(positions, triangles[i][0]);
        const Vec3 b = vertexAt(positions, triangles[i][1]);
        const Vec3 c = vertexAt(positions, triangles[i][2]);
        const std::int64_t x0 = cellOf(std::min({a.x, b.x, c.x}));
        const std::int64_t x1 = cellOf(std::max({a.x, b.x, c.x}));
        const std::int64_t y0 = cellOf(std::min({a.y, b.y, c.y}));
        const std::int64_t y1 = cellOf(std::max({a.y, b.y, c.y}));
        const std::int64_t z0 = cellOf(std::min({a.z, b.z, c.z}));
        const std::int64_t z1 = cellOf(std::max({a.z, b.z, c.z}));
        // A triangle far larger than a cube is listed in every cube of its bounds: few are, in a level's meshes.
        for (std::int64_t x = x0; x <= x1; ++x) {
            for (std::int64_t y = y0; y <= y1; ++y) {
                for (std::int64_t z = z0; z <= z1; ++z) {
                    m_entries.emplace_back(keyOf(x, y, z), i);
                }
            }
        }
    }
    std::sort(m_entries.begin(), m_entries.end());
}

std::int64_t TriangleGrid::cellOf(float v) const { return static_cast<std::int64_t>(std::floor(v / m_cell)); }

std::uint64_t TriangleGrid::keyOf(std::int64_t x, std::int64_t y, std::int64_t z) const {
    // 21 bits a coordinate, offset to stay positive: cubes of a metre reach a million metres either way.
    constexpr std::int64_t kOffset = 1 << 20;
    constexpr std::uint64_t kMask = (1U << 21) - 1;
    return (static_cast<std::uint64_t>(x + kOffset) & kMask) << 42U |
           (static_cast<std::uint64_t>(y + kOffset) & kMask) << 21U | (static_cast<std::uint64_t>(z + kOffset) & kMask);
}

std::optional<float> TriangleGrid::nearest(Vec3 p, float reach) const {
    std::optional<float> best;
    const std::int64_t cx = cellOf(p.x);
    const std::int64_t cy = cellOf(p.y);
    const std::int64_t cz = cellOf(p.z);
    // The reach is at most a cube, so the point's cube and its 26 neighbours hold every triangle within it.
    for (std::int64_t dx = -1; dx <= 1; ++dx) {
        for (std::int64_t dy = -1; dy <= 1; ++dy) {
            for (std::int64_t dz = -1; dz <= 1; ++dz) {
                const std::uint64_t key = keyOf(cx + dx, cy + dy, cz + dz);
                auto it = std::lower_bound(m_entries.begin(), m_entries.end(),
                                           std::pair<std::uint64_t, std::uint32_t>{key, 0});
                for (; it != m_entries.end() && it->first == key; ++it) {
                    const MeshTriangle& t = m_triangles[it->second];
                    const float d = pointTriangleDistance(p, vertexAt(m_positions, t[0]), vertexAt(m_positions, t[1]),
                                                          vertexAt(m_positions, t[2]));
                    if (d <= reach && (!best || d < *best)) {
                        best = d;
                    }
                }
            }
        }
    }
    return best;
}

} // namespace coney::debug
