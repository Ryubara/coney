// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "animation/anim_math.h"
#include "raycast/collision_mesh.h"

// The human's body sizes: the walking sphere the physics world sweeps against the walls (src/physics/) and the
// larger sphere a player is pushed out of walls with while airborne, with the materials a climb passes through.
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
};

/// The one BodyTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] BodyTuning& bodyTuning();

/// Materials a climbing human's body passes through (record `+0x08` bit `0x40`): 30 `LOW_FENCE`, 31 `OPAQUE_FENCE`
/// and 122 `RAILING` (docs/research/collision.md#materials).
inline constexpr std::uint8_t kMaterialLowFence = 30;
inline constexpr std::uint8_t kMaterialOpaqueFence = 31;
inline constexpr std::uint8_t kMaterialRailing = 122;
inline constexpr std::array<std::uint8_t, 3> kFenceMaterials{kMaterialLowFence, kMaterialOpaqueFence, kMaterialRailing};

/// The walking body's sphere radius for a human of `scale` (`+0x65c`): 0.35 × scale.
[[nodiscard]] float walkingRadius(float scale);
/// The walking sphere's centre height above the feet: its radius plus 0.05, so its bottom is 0.05 above the feet.
[[nodiscard]] float walkingCentreHeight(float scale);
/// A player's walking sphere radius for a human of `scale`: 0.35 × scale × the player's body factor (1.4286) × scale
/// again (0.4704 m at 0.97).
[[nodiscard]] float playerWalkingRadius(float scale);
/// A player's walking sphere's centre height above the feet: its radius plus 0.05.
[[nodiscard]] float playerWalkingCentreHeight(float scale);
/// An AI human's walking sphere radius for a human of `scale`: its body `+0x60` is 1.0, so 0.35 × scale × the scale
/// again, as the player's (0.357 m for level99's bums at 1.01; docs/research/characters.md#walls, step 1).
[[nodiscard]] float aiWalkingRadius(float scale);

/// What a triangle is to the walking sweep, before the move and the sphere's side are known
/// (docs/research/physics-bodies.md#unit-2, the capsule's per-triangle rules).
enum class WallClass : std::uint8_t {
    Disabled,   ///< Not enabled (a door's or a pane's switched off): no test meets it.
    Degenerate, ///< No usable normal (zero area).
    Floor,      ///< n.z above 0.65: the ground snap stands on it, the walking sweep ignores it.
    Ceiling,    ///< n.z below −0.65: the walking sweep ignores it.
    LowWall,    ///< Steep, but too low or thin for a walking body (physics::capsuleLowOrThin() at minWallHeight).
    Wall,       ///< Steep and tall enough: it stops a walking body that moves into it from its front.
};

/// How the walking sweep classes triangle `index` of `mesh`, through the physics sweep's own rules
/// (physics::capsuleWallNormal(), physics::capsuleLowOrThin() at bodyTuning().minWallHeight). Materials a climb passes
/// through are not looked at. The debug menus colour the collision wireframe by it.
[[nodiscard]] WallClass wallClassOf(const raycast::CollisionMesh& mesh, std::uint16_t index);

} // namespace coney::human
