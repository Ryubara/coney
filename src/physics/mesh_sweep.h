// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/sphere_triangle.h"
#include "raycast/collision_mesh.h"

// A shape against the level's collision mesh: the cell walk and its visited bits, the per-triangle rules of each shape
// (the walking capsule's walls, low and thin rules and corner slide; the sphere's options), the sort with its smear,
// the airborne push-out and the sphere's overlap. Each writes contacts as the original's mesh functions write them, so
// a body meets the level from where its move starts, at the fraction it first touches, and never tunnels through a
// wall. Research: docs/research/physics-bodies.md#unit-2

namespace coney::physics {

/// An axis-aligned box: the original's `{min, max}` AABB.
struct Box {
    anim::Vec3 min;
    anim::Vec3 max;
};

/// Triangle flag bits the mesh functions read (docs/research/collision.md#triangles).
inline constexpr std::uint16_t kTriangleBit3 = 0x0008;  ///< Skipped by a sphere sweep of a body with flag 0x100.
inline constexpr unsigned kTriangleSlideEdgeShift = 14; ///< Bits 14-15: the corner slide's edge, 3 for none.
inline constexpr unsigned kTriangleNoSlideEdge = 3;

/// Sphere query options (the query's `+0x10` word as the mesh functions read it).
inline constexpr std::uint32_t kQueryMinNormalZ = 0x4;      ///< Skip triangles whose `n.z` is below the minimum.
inline constexpr std::uint32_t kQueryMaxNormalZ = 0x8;      ///< Skip triangles whose `n.z` is above the maximum.
inline constexpr std::uint32_t kQueryKeepAll = 0x40;        ///< Keep faces behind or not moved into, flagged.
inline constexpr std::uint32_t kQuerySkipBit3 = 0x8000;     ///< Skip kTriangleBit3 triangles.
inline constexpr std::uint32_t kQueryOnePerPlane = 0x20000; ///< Keep one contact per plane (|n_i · n_j| > 0.9).

/// A body flag the sphere sweep reads: with it, kTriangleBit3 triangles are skipped.
inline constexpr std::uint32_t kBodySkipBit3 = 0x100;

/// A mesh function's own record of a hit (the original's 0xb0 bytes): the sort key, the triangle and the working
/// triangle as it stood when the kernel ran.
struct MeshContact {
    float key = 0.0F;           ///< `+0x00`: the fraction, depth or squared distance the list is sorted by.
    std::uint32_t triangle = 0; ///< `+0x04`: the triangle's index.
    std::uint32_t flags = 0;    ///< `+0x08`: the sphere sweep's kContactBehind / Away / Along.
    float startDistance = 0.0F; ///< `+0x0c`: the sphere sweep's `n · c − n · v0`, before any flip.
    unsigned region = 0;        ///< `+0x14`: the capsule's inside mask from the kernel.
    WorkingTriangle working;    ///< `+0x20`-`+0xaf`: the copy of the working triangle.
};

/// The memory a mesh query reuses between calls: the visited bits (one per triangle; the original keeps them in the
/// mesh, Coney beside it so a const mesh can be queried), the hits and their sorted order.
struct MeshScratch {
    std::vector<std::uint8_t> visited;
    std::vector<MeshContact> found;
    std::vector<std::size_t> order;
};

/// The grid cells a query walks, inclusive.
struct CellRange {
    std::array<std::uint32_t, 3> min{};
    std::array<std::uint32_t, 3> max{};
};

/// Starts a query of `worldBox`: clears the visited bits and gives the cells its two corners fall in, each corner
/// taken through the grid matrix, clamped and truncated on its own.
/// @orig 0x00347170 CollisionMesh_BeginShapeQuery (unknown)
[[nodiscard]] CellRange beginShapeQuery(const raycast::CollisionMesh& mesh, const Box& worldBox, MeshScratch& scratch);

/// Fills `order` with the indices of `contacts` sorted by key, as the original's insertion sort does it: equal keys
/// keep their arrival order, and **an insertion before the end overwrites the entries after it with copies of the one
/// it displaced** (keys 0.5 A, 0.6 B, 0.3 C give C, A, A). The first entry is always the true minimum.
/// @orig 0x00347300 MeshContacts_SortByFraction (unknown)
void sortByFraction(std::span<const MeshContact> contacts, std::vector<std::size_t>& order);

/// The walking capsule's sweep: its sphere and the rules for its walls.
struct CapsuleSweep {
    anim::Vec3 feet;             ///< Shape `+0x50`: the feet at the start of the move.
    float radius = 0.0F;         ///< Shape `+0x40` × body `+0x60` × the human's scale.
    anim::Vec3 move;             ///< The pass's whole move.
    bool ownerAirborne = false;  ///< The owner's state has `0x4000000`: no corner slide.
    float footGap = 0.05F;       ///< The sphere's centre is `radius + footGap` above the feet.
    float minWallHeight = 0.25F; ///< The low and thin rules' 0.25 m.
};

/// The walking capsule's sphere swept by `sweep.move` against the walls of `mesh` in `worldBox`'s cells: enabled
/// triangles with `|n.z|` ≤ 0.65, the start centre in front of them (two-sided ones turned round), the move going into
/// them by more than 0.001, not low or thin; each kernel hit from 0 to 1 is kept. They are written to `out` (appended)
/// nearest first, each with the corner slide built from all of them: for each hit beyond the edge its triangle names
/// in bits 14-15, while the owner is on the ground, a horizontal unit direction along the wall past that edge, the sum
/// made `max(radius, |move|)` long and flagged kContactAddSlide. Returns how many were written (all of them: the
/// original ignores its `max`). The original's walk reads one index past a cell's list after a hit above 1; Coney
/// does not.
/// @orig 0x00347c08 PhysicsMesh_SweepCapsule (unknown)
int sweepCapsuleMesh(const raycast::CollisionMesh& mesh, const Box& worldBox, const CapsuleSweep& sweep,
                     MeshScratch& scratch, std::vector<Contact>& out);

/// A sphere shape's sweep and its query.
struct SphereSweep {
    Sphere sphere;               ///< Shape `+0x50` and `+0x40`: no raise, no scale.
    anim::Vec3 move;             ///< The whole move.
    std::uint32_t options = 0;   ///< kQuery* bits.
    float minNormalZ = 0.0F;     ///< With kQueryMinNormalZ.
    float maxNormalZ = 0.0F;     ///< With kQueryMaxNormalZ.
    std::uint32_t bodyFlags = 0; ///< The sweeping body's flags (`+0x40`), for kBodySkipBit3.
    int max = 1;                 ///< The most contacts written.
};

/// A sphere swept against `mesh` in `worldBox`'s cells: enabled triangles inside the query's `n.z` range, the start
/// centre in front (two-sided ones turned round; with kQueryKeepAll others are kept and flagged), the move going in;
/// each kernel hit from 0 to 1 is kept. Sorted, cut to `max`, then with kQueryOnePerPlane one per plane; written to
/// `out` (appended) with their flags and start distances. Returns how many were written.
/// @orig 0x00348a50 PhysicsMesh_SweepSphere (unknown)
int sweepSphereMesh(const raycast::CollisionMesh& mesh, const Box& worldBox, const SphereSweep& sweep,
                    MeshScratch& scratch, std::vector<Contact>& out);

/// A sphere overlapping `mesh` in `worldBox`'s cells, from either side of every enabled triangle inside the query's
/// `n.z` range: each closer than the radius is kept with its depth. The `room` shallowest are written to `out`
/// (appended) deepest first, with the triangles' own normals. Returns **1** when any was found, not the count, so its
/// one caller sees only the first.
/// @orig 0x003473c8 PhysicsMeshOverlap_Sphere (unknown)
int overlapSphereMesh(const raycast::CollisionMesh& mesh, const Box& worldBox, const Sphere& sphere,
                      std::uint32_t options, float minNormalZ, float maxNormalZ, int room, MeshScratch& scratch,
                      std::vector<Contact>& out);

/// The box and capsule overlaps against the mesh: they find nothing, as in the original.
/// @orig 0x003473b8 PhysicsMeshOverlap_Box (unknown)
/// @orig 0x003473c0 PhysicsMeshOverlap_Capsule (unknown)
[[nodiscard]] int overlapNothing();

/// The airborne push-out: the nearest wall (`|n.z|` ≤ 0.65, the sphere in front, two-sided ones turned round) whose
/// face, edge or corner is closer than the radius, and the push out of it along its normal's **horizontal part** by
/// `radius − distance`. The original adds it to the body's pending push-out for the next sweep, undivided by the push
/// weight; nothing when no wall is in reach.
/// @orig 0x003477c0 PhysicsBody_PushOutOfWalls (unknown)
[[nodiscard]] std::optional<anim::Vec3> pushOutOfWalls(const raycast::CollisionMesh& mesh, const Sphere& sphere,
                                                       MeshScratch& scratch);

} // namespace coney::physics
