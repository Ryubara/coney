// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "raycast/collision_mesh.h"

// The human's body against the level's walls: the walking sphere with the 0.25 m rule that is all the step-up there
// is, and the larger sphere a player is pushed out of walls with while airborne. Pure functions over the collision
// mesh, so each rule can be tested on its own.
// Research: docs/research/characters.md#walls

namespace coney::human {

/// A player's physics body `+0x60` (and `+0x64`), read in the street save: 0.5 / 0.35. The player's walking sphere is
/// the shape's radius (0.35 × scale) × this × the scale again: 0.4704 m for Rembrandt
/// (docs/research/characters.md#walls).
inline constexpr float kPlayerBodyFactor = 1.4286F;

/// The body's sizes the debug menus may edit while the game runs (docs/guides/debug-menu.md#tunables). Each defaults
/// to the researched value; read through bodyTuning().
struct BodyTuning {
    float radius = 0.35F;        ///< The capsule shape's radius (shape `+0x40`), before the human's scale.
    float footGap = 0.05F;       ///< The sphere's bottom above the feet: its centre is radius + this high.
    float minWallHeight = 0.25F; ///< A wall triangle whose steepest edge rises less than this is not a wall.
    float airRadius = 0.5F;      ///< A player's push-out sphere while airborne, before the scale (0.35 for others).
    float playerFactor = kPlayerBodyFactor; ///< A player's body `+0x60`: its walking sphere is radius × this × scale².
    float contactClear = 0.01F;             ///< How far clear of a face the sweep's resolver leaves the walking sphere.
};

/// The one BodyTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] BodyTuning& bodyTuning();

/// Materials a climbing human's body passes through (record `+0x08` bit `0x40`): 30 `LOW_FENCE`, 31 `OPAQUE_FENCE`
/// and 122 `RAILING` (docs/research/collision.md#materials).
inline constexpr std::uint8_t kMaterialLowFence = 30;
inline constexpr std::uint8_t kMaterialOpaqueFence = 31;
inline constexpr std::uint8_t kMaterialRailing = 122;
inline constexpr std::array<std::uint8_t, 3> kFenceMaterials{kMaterialLowFence, kMaterialOpaqueFence, kMaterialRailing};

/// A wall triangle's normal has |z| at most this; anything flatter is a floor or a ceiling.
inline constexpr float kWallNormalZ = 0.65F;

/// What a wall test skips, beyond floors, ceilings, disabled triangles and walls the sphere is behind.
struct WallFilter {
    /// The move being made: a wall the move does not go into (`n · move` ≥ −0.001) is skipped. Zero tests every wall.
    anim::Vec3 move;
    /// Skip low and thin triangles (the walking sweep's 0.25 m rule, wallTooLow()); the airborne push-out does not.
    bool skipLow = false;
    /// Materials whose triangles are skipped (the fences while climbing over); empty for none.
    std::span<const std::uint8_t> excludeMaterials;
    /// Push the sphere only until it touches the triangle, the walking sweep's contact: a face lower than the sphere's
    /// centre then stops the body where the sphere meets its top edge, `sqrt(r² − (c − h)²)` from the face
    /// (docs/research/characters.md#walls). Otherwise out of the face's plane by `n × (r − distance)`, as the
    /// airborne push-out does.
    bool toContact = false;
    /// How far clear of the face the push leaves the sphere: the walking sweep's resolver backs off 0.01 m along the
    /// normal (`0x0033d9d8`), so the player stops 0.480 m from a wall; 0 for the airborne push-out.
    float keepClear = 0.0F;
};

/// Whether the triangle `a b c` is too low or too thin to be a wall for a walking body: the edge whose direction is
/// steepest rises less than `minHeight`; or, unless the longest edge is nearly vertical (|unit z| ≥ 0.8), the third
/// corner lies less than `minHeight` from the longest edge's line.
/// @orig 0x00347c08 PhysicsMesh_SweepCapsule (unknown)
[[nodiscard]] bool wallTooLow(anim::Vec3 a, anim::Vec3 b, anim::Vec3 c, float minHeight);

/// The nearest wall a sphere at `centre` of `radius` overlaps: an enabled triangle of `mesh` with |n.z| at most
/// kWallNormalZ, that the sphere reaches (by the distance to its closest point: face, edge or corner) from its front
/// (either side for a two-sided one), and that `filter` keeps. Nearness by the closest point is **Coney's choice**:
/// the page says "nearest" without saying how it measures, and a face-only test lets a body slip between two walls at
/// a convex edge. Returns the push along the face's normal that moves the sphere out of it: to `radius` from the
/// face's plane, or with `filter.toContact` to `radius` from the triangle's closest point; nothing when none is
/// reached. `scratch` is reused between calls to save allocations.
/// @orig 0x003477c0 PhysicsBody_PushOutOfWalls (unknown)
[[nodiscard]] std::optional<anim::Vec3> nearestWallPush(const raycast::CollisionMesh& mesh, anim::Vec3 centre,
                                                        float radius, const WallFilter& filter,
                                                        std::vector<std::uint16_t>& scratch);

/// The corner slide's direction for a walking sphere at `centre` of `radius` (docs/research/characters.md#walls, step
/// 6): over the walls `filter` keeps that the sphere reaches **beyond the edge its triangle names** in flag bits 14-15
/// (0: v0→v1, 1: v1→v2, 2: v2→v0, 3: none), the wall's tangent `n × up`, taken away when that edge rises and added
/// otherwise, so each points along its wall toward the named edge, round the corner. Returns the sum, not normalised
/// (zero when nothing counts). **Coney's reading**: the hit centre is where the moved sphere stands, not a swept
/// contact's; a two-sided triangle met from behind adds nothing (the original swaps its corners first, in an order not
/// traced; four triangles on the disc are two-sided).
/// @orig 0x00347c08 PhysicsMesh_SweepCapsule (unknown)
[[nodiscard]] anim::Vec3 edgeSlide(const raycast::CollisionMesh& mesh, anim::Vec3 centre, float radius,
                                   const WallFilter& filter, std::vector<std::uint16_t>& scratch);

/// The walking body's sphere radius for a human of `scale` (`+0x65c`): 0.35 × scale.
[[nodiscard]] float walkingRadius(float scale);
/// The walking sphere's centre height above the feet: its radius plus 0.05, so its bottom is 0.05 above the feet.
[[nodiscard]] float walkingCentreHeight(float scale);
/// A player's walking sphere radius for a human of `scale`: 0.35 × scale × the player's body factor (1.4286) × scale
/// again (0.4704 m at 0.97).
[[nodiscard]] float playerWalkingRadius(float scale);
/// A player's walking sphere's centre height above the feet: its radius plus 0.05.
[[nodiscard]] float playerWalkingCentreHeight(float scale);

} // namespace coney::human
