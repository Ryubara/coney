// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/error.h"

// A level's static collision: the triangles characters, cameras and scripts test against, read from the level file's
// six collision chunks and queried through a uniform grid. Everything is in the game's axes, z up: the mesh is not
// converted to RenderWare's. Format and queries: docs/research/collision.md.

namespace coney::raycast {

/// A point or direction in the game's axes (z up).
struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

/// Chunk types of the collision mesh, in the order the level file holds them (docs/research/collision.md#chunks).
inline constexpr std::uint32_t kCollisionChecked = 0x52;      ///< One bit per triangle, scratch for the queries.
inline constexpr std::uint32_t kCollisionVertexBuffer = 0x07; ///< `float[4]` vertices.
inline constexpr std::uint32_t kCollisionTriangles = 0x04;    ///< 10-byte triangles.
inline constexpr std::uint32_t kCollisionGrid = 0x05;         ///< One `u32` list offset per cell.
inline constexpr std::uint32_t kCollisionStrings = 0x06;      ///< The cells' triangle index lists (`u16`).
inline constexpr std::uint32_t kCollisionMeshChunk = 0x03;    ///< The 160-byte header; its handler builds the mesh.

/// Bytes of the `0x03` header chunk.
inline constexpr std::size_t kCollisionHeaderBytes = 160;

/// Triangle flag bits (docs/research/collision.md#triangles).
inline constexpr std::uint16_t kTriangleEnabled = 0x0001;          ///< Set at load; switched by setEnabledInBox().
inline constexpr std::uint16_t kTriangleTwoSided = 0x0002;         ///< Rays and spheres meet both faces.
inline constexpr std::uint16_t kTriangleTestableDisabled = 0x0800; ///< Testable while disabled, with mask bit 0x800.

/// The material id a ray cast reports before it hits anything: `MATERIAL_NONE`. Also the original's end marker of an
/// exclusion list, which Coney passes as a span instead.
inline constexpr std::uint8_t kMaterialNone = 1;

/// One collision triangle as the level file stores it.
struct CollisionTriangle {
    std::array<std::uint16_t, 3> vertices{}; ///< Indices into the vertex buffer.
    std::uint16_t flags = 0;                 ///< kTriangleEnabled, kTriangleTwoSided, type bits 2-10, ...
    std::uint8_t material = 0;               ///< A `MATERIAL_*` id.
    std::uint8_t area = 0; ///< The area byte (meaning speculative); 0 is skipped by dropToMarkedGround.
};

/// A ray: `origin`, a unit `direction` and the `length` along it that counts.
struct Ray {
    Vec3 origin;
    Vec3 direction;
    float length = 0.0F;
};

/// What a ray cast hit: the nearest accepted triangle.
struct RayHit {
    Vec3 normal;                ///< Unit face normal; for a two-sided triangle, facing the ray.
    float t = 0.0F;             ///< Distance along the ray.
    std::uint8_t material = 0;  ///< The triangle's material id.
    std::uint16_t flags = 0;    ///< The triangle's flags & 0xfff.
    std::uint8_t area = 0;      ///< The triangle's area byte.
    std::uint32_t triangle = 0; ///< Its index.
};

/// What a sphere push did.
struct SpherePushResult {
    bool touched = false; ///< Some wall overlapped the sphere; the centre was moved.
    Vec3 centre;          ///< The centre after the push (unchanged when nothing touched).
    Vec3 firstNormal;     ///< The normal of the first wall that pushed; zero when none.
};

/// A level's collision mesh: header, grid, index lists, triangles and vertices, and the per-query scratch bit set.
///
/// Queries are logically const but use the mesh's checked bit set as scratch (as the original does), so one mesh
/// must not be queried from two threads at once.
///
/// Research: docs/research/collision.md
class CollisionMesh final : public chunk::LoadedObject {
  public:
    /// Builds a mesh from the data of its six chunks, checking every size, offset and index the queries follow:
    /// the header's counts against the chunk sizes, each grid offset and list against the index lists, each list
    /// entry against the triangle count and each triangle's vertices against the vertex count. Fails with
    /// ErrorCode::Invalid (or ErrorCode::Truncated for a chunk shorter than its count says), naming what is wrong.
    /// Sets every triangle's enabled bit and clears the checked set, as the original's handler does.
    [[nodiscard]] static std::expected<std::unique_ptr<CollisionMesh>, Error>
    build(std::span<const std::byte> header, std::span<const std::byte> lists, std::span<const std::byte> grid,
          std::span<const std::byte> triangles, std::span<const std::byte> vertices,
          std::span<const std::byte> checked);

    [[nodiscard]] std::string_view describe() const override { return "collision mesh"; }

    /// The nearest triangle `ray` hits within its length, skipping triangles whose material is in
    /// `excludeMaterials`, whose type bits meet `mask` (`flags & 0xfff & mask & ~0x801`), or that are disabled
    /// (unless `mask` has bit 0, or bit 11 and the triangle flag 0x800). Walks the grid's columns as the original
    /// does, half a cell off its cell centres, so it finds the same hits.
    /// @orig 0x00350cd8 CollisionMesh_RayCast (CollisionMesh.cpp)
    [[nodiscard]] std::optional<RayHit> rayCast(const Ray& ray, std::span<const std::uint8_t> excludeMaterials,
                                                std::uint32_t mask) const;

    /// Moves a sphere of `radius` at `centre` out of the walls it overlaps: each enabled wall (|n.z| <= cos 15°) whose
    /// face the sphere reaches pushes it out along its normal, and the centre moves by the average push. Floors,
    /// ceilings, edges and corners never push.
    /// @orig 0x003519f8 CollisionMesh_SpherePush (CollisionMesh.cpp)
    [[nodiscard]] SpherePushResult spherePush(float radius, Vec3 centre,
                                              std::span<const std::uint8_t> excludeMaterials) const;

    /// Switches every triangle whose three vertices lie inside the box [`min`, `max`] on or off (its enabled bit).
    /// @orig 0x00351160 CollisionMesh_SetEnabledInBox (CollisionMesh.cpp)
    void setEnabledInBox(Vec3 min, Vec3 max, bool on);

    /// Cells per axis.
    [[nodiscard]] std::array<std::uint32_t, 3> cells() const { return {m_nx, m_ny, m_nz}; }
    /// The triangles, in file order.
    [[nodiscard]] std::span<const CollisionTriangle> triangles() const { return m_triangles; }
    /// The vertices, in file order.
    [[nodiscard]] std::span<const Vec3> vertices() const { return m_vertices; }
    /// The header's lowest vertex z (+0x98; no reader traced in the original).
    [[nodiscard]] float lowestZ() const { return m_lowestZ; }
    /// The cell holding `point`: the header matrix, clamped to the clamp box, truncated.
    [[nodiscard]] std::array<std::uint32_t, 3> cellOf(Vec3 point) const;
    /// The triangle indices listed in cell (x, y, z), which must be inside the grid (checked by CONEY_ASSERT).
    [[nodiscard]] std::span<const std::uint16_t> cellTriangles(std::uint32_t x, std::uint32_t y, std::uint32_t z) const;

    /// The unit face normal of triangle `index`: (v1 - v0) × (v2 - v0), normalised (zero for a degenerate triangle).
    [[nodiscard]] Vec3 faceNormal(std::uint32_t index) const;

  private:
    CollisionMesh() = default;

    // A point in grid space: the header matrix applied to `point` (rows: g = x·r0 + y·r1 + z·r2 + r3), unclamped.
    [[nodiscard]] Vec3 toGrid(Vec3 point) const;
    // The cell box covering the grid-space points `a` and `b`, clamped and truncated per axis.
    [[nodiscard]] std::array<std::array<std::uint32_t, 2>, 3> cellBox(Vec3 a, Vec3 b) const;
    // Clears the checked set before a query.
    void clearChecked() const;
    // Marks triangle `index` checked; returns false when it already was.
    bool checkOnce(std::uint32_t index) const;
    // Tests the triangles of one cell against `ray`, keeping the nearest hit in `best`.
    // @orig 0x00350778 CollisionMesh_RayTestCell (CollisionMesh.cpp)
    void rayTestCell(std::uint32_t x, std::uint32_t y, std::uint32_t z, const Ray& ray,
                     std::span<const std::uint8_t> excludeMaterials, std::uint32_t mask,
                     std::optional<RayHit>& best) const;

    std::array<std::array<float, 4>, 4> m_matrix{};
    std::array<float, 3> m_clampMin{};
    std::array<float, 3> m_clampMax{};
    std::uint32_t m_nx = 0;
    std::uint32_t m_ny = 0;
    std::uint32_t m_nz = 0;
    float m_lowestZ = 0.0F;
    std::vector<std::uint32_t> m_grid;
    std::vector<std::uint16_t> m_lists;
    std::vector<CollisionTriangle> m_triangles;
    std::vector<Vec3> m_vertices;
    mutable std::vector<std::uint8_t> m_checked; // one bit per triangle, cleared at the start of each query
};

/// The chunk `0x03` handler: pops the header just loaded, then the index lists (`0x06`), grid (`0x05`), triangles
/// (`0x04`), vertices (`0x07`) and checked set (`0x52`), and pushes the CollisionMesh built from them as an object,
/// for the level header's handler to take. Fails when a chunk is missing or out of order, or as CollisionMesh::build.
/// @orig 0x00350580 CollisionMesh_OnLoaded (CollisionMesh.cpp)
[[nodiscard]] std::expected<void, Error> onCollisionMeshLoaded(chunk::ChunkStacks& stacks, std::uint32_t type);

/// The ray's straight-down direction, (0, 0, -1) in the game's axes.
inline constexpr Vec3 kDown{0.0F, 0.0F, -1.0F};

/// Drops `position` onto the ground below it: casts down `length` from it and, on a hit, moves it to 0.1 above the
/// hit. Returns whether it hit; `position` is unchanged otherwise. Coney casts with mask 0 and no exclusions (the
/// original's arguments to its world cast are not on the page).
/// @orig 0x0034f950 Collision_DropToGround (unknown)
[[nodiscard]] bool dropToGround(const CollisionMesh& mesh, float length, Vec3& position);

/// Drops `position` onto the first ground below it whose area byte is not 0: casts down; a hit on a triangle with area
/// byte 0 continues the cast from 0.01 past it with the length shortened by as much; on a marked triangle `position`
/// moves to 0.25 above the hit. Returns false, leaving `position` unchanged, when a cast misses.
/// @orig 0x0034fa28 Collision_DropToMarkedGround (unknown)
[[nodiscard]] bool dropToMarkedGround(const CollisionMesh& mesh, float length, Vec3& position);

/// A long ray cast in steps: casts `direction × step` from `origin`, then from the end of that, and so on until `max`
/// is covered. Returns the first hit point, or nothing. The last step is shortened to end at `max` (a Coney choice:
/// the page does not say whether the original's overshoots).
/// @orig 0x0034f740 Collision_MarchRay (unknown)
[[nodiscard]] std::optional<Vec3> marchRay(const CollisionMesh& mesh, float step, float max, Vec3 origin,
                                           Vec3 direction);

} // namespace coney::raycast
