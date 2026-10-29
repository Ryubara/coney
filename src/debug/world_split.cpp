// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/world_split.h"

// Stubs: the tests of debug/world_split.h come first (docs/guides/stamps.md#fails-first); each returns a default.

namespace coney::debug {

bool GameBox::contains(Vec3 /*point*/, float /*margin*/) const { return false; }

MeshPieces connectedPieces(std::span<const Vec3> /*positions*/, std::span<const MeshTriangle> /*triangles*/,
                           float /*weld*/) {
    return {};
}

std::vector<std::uint8_t> trianglesInBox(std::span<const Vec3> /*positions*/,
                                         std::span<const MeshTriangle> /*triangles*/, const GameBox& /*box*/,
                                         CutRule /*rule*/, float /*weld*/) {
    return {};
}

std::vector<raycast::BuildTriangle> upFacingCollision(std::span<const Vec3> /*positions*/,
                                                      std::span<const MeshTriangle> /*triangles*/,
                                                      std::span<const std::uint8_t> /*take*/, float /*minUpZ*/,
                                                      std::uint8_t /*material*/) {
    return {};
}

std::vector<raycast::BuildTriangle> collisionInBox(const raycast::CollisionMesh& /*mesh*/, const GameBox& /*box*/,
                                                   Vec3 /*offset*/) {
    return {};
}

std::vector<SpreadPlacement> spreadLayout(std::span<const GameBox> /*boxes*/, std::uint32_t /*columns*/, float /*gap*/,
                                          float /*scale*/, Vec3 /*origin*/, std::optional<float> /*fitCell*/) {
    return {};
}

Vec3 offsetAt(std::span<const OffsetKey> /*keys*/, std::uint64_t /*step*/) { return {}; }

float pointTriangleDistance(Vec3 /*p*/, Vec3 /*a*/, Vec3 /*b*/, Vec3 /*c*/) { return 0.0F; }

TriangleGrid::TriangleGrid(std::span<const Vec3> positions, std::span<const MeshTriangle> triangles, float cell)
    : m_positions(positions), m_triangles(triangles), m_cell(cell) {}

std::optional<float> TriangleGrid::nearest(Vec3 /*p*/, float /*reach*/) const { return std::nullopt; }

std::uint64_t TriangleGrid::keyOf(std::int64_t /*x*/, std::int64_t /*y*/, std::int64_t /*z*/) const { return 0; }

std::int64_t TriangleGrid::cellOf(float /*v*/) const { return 0; }

std::expected<WorldEdits, Error> parseWorldEdits(std::string_view /*text*/) { return WorldEdits{}; }

} // namespace coney::debug
