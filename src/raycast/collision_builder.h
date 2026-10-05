// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

#include "core/error.h"
#include "raycast/collision_mesh.h"

// Coney's own writer of the collision mesh format: turns a list of triangles into the six chunks a level file holds
// (docs/research/collision.md#chunks) and builds a CollisionMesh from them, so geometry Coney makes itself (the
// sandbox, docs/guides/sandbox.md) is queried by exactly the code that queries a level's. The original game only reads
// the format; it has no counterpart of this file.

namespace coney::raycast {

/// The material id the builder gives a triangle unless told otherwise: `MATERIAL_CONCRETE`
/// (docs/research/collision.md#materials).
inline constexpr std::uint8_t kMaterialConcrete = 5;

/// The most triangles, and the most distinct vertices, a mesh can have: the header's triangle count and the
/// triangles' vertex indices are `u16` (docs/research/collision.md#header).
inline constexpr std::size_t kMaxCollisionTriangles = 65535;

/// One triangle to build a collision mesh from, in the game's axes (z up). The face it counts as the front is the one
/// `(b - a) × (c - a)` points to, as in the format.
struct BuildTriangle {
    std::array<Vec3, 3> corners{}; ///< a, b, c.
    std::uint16_t flags = 0;       ///< Flag bits as the file stores them; the enabled bit is set at load anyway.
    std::uint8_t material = kMaterialConcrete; ///< A `MATERIAL_*` id.
    std::uint8_t area = 1;                     ///< The area byte; not 0, so Collision_DropToMarkedGround stops on it.
};

/// The six chunks of a collision mesh, as a level file holds them (docs/research/collision.md#chunks).
struct CollisionChunks {
    std::vector<std::byte> header;    ///< `0x03`: the 160-byte header.
    std::vector<std::byte> lists;     ///< `0x06`: the cells' triangle index lists.
    std::vector<std::byte> grid;      ///< `0x05`: one list offset per cell.
    std::vector<std::byte> triangles; ///< `0x04`: 10-byte triangles.
    std::vector<std::byte> vertices;  ///< `0x07`: `float[4]` vertices.
    std::vector<std::byte> checked;   ///< `0x52`: the query scratch bit set, zeroed.
};

/// Encodes `triangles` as a collision mesh's chunks. Identical corners share one vertex. The grid is one layer of cells
/// about `cellSize` wide in x and y over the triangles' bounding box, its matrix mapping that box onto cells 0 to n - 1
/// as the disc's meshes do, with a `z` scale of 0 (as 45 of the 64 levels have). Each triangle is listed in the cells
/// its bounding box touches and one more on every side, so the ray cast's half-cell column walk never misses it.
/// Fails with ErrorCode::InvalidArgument for no triangles, more than kMaxCollisionTriangles triangles or distinct
/// vertices, a corner that is not finite, or a cell size that is not positive.
[[nodiscard]] std::expected<CollisionChunks, Error> encodeCollisionMesh(std::span<const BuildTriangle> triangles,
                                                                        float cellSize);

/// encodeCollisionMesh(), then CollisionMesh::build() over the chunks: a mesh ready for queries. Fails as either does.
[[nodiscard]] std::expected<std::unique_ptr<CollisionMesh>, Error>
buildCollisionMesh(std::span<const BuildTriangle> triangles, float cellSize);

} // namespace coney::raycast
