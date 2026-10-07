// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "animation/anim_math.h"

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

namespace coney::human {
class Human;
} // namespace coney::human

// What an AI human sees: the line of sight between two humans (two rays through the level's collision, which pass
// through fences, railings and glass), the field of view, and "can see" (a range, then the line). The brains' attack
// warnings, the enemy scores and the run-in ask these. Research: docs/research/ai.md#sight

namespace coney::ai {

/// The eye height the first sight ray leaves from and aims at, above the feet, m (`0x00222288`).
inline constexpr float kEyeHeight = 1.7F;
/// The height the second ray aims at when the first is blocked, m.
inline constexpr float kChestHeight = 1.0F;
/// The materials a sight ray passes through (the list `Ray_IsClear` builds): `LOW_FENCE` 30, `GLASS` 2,
/// `STOREDOOR_GLASS` 187, `RAILING` 122, `CHAINLINK_NOCLIMB` 107 and `NONE` 1. So an AI sees through fences and glass.
inline constexpr std::array<std::uint8_t, 6> kSightSeeThrough{30, 2, 187, 122, 107, 1};

/// What a line-of-sight test found.
struct SightLine {
    bool clear = false;             ///< Either ray reached the other human.
    std::uint8_t crossedMaterial{}; ///< The material the first ray hit (0 when it hit nothing).
};

/// The line of sight from `from`'s feet to `to`'s (`Human_HasLineOfSight`): a ray from 1.7 m above `from` to 1.7 m
/// above `to` through `mesh` (mask 0, kSightSeeThrough passed); when it hits, a second ray to 1.0 m above `to`. Clear
/// when either reaches. The distance is not limited (callers test range). With no mesh every line is clear. **Coney
/// choice**: only the level's static collision blocks a line (the world manager's other objects are not cast against).
/// @orig 0x00222288 Human_HasLineOfSight (unknown)
/// @orig 0x0024df40 Ray_IsClear (unknown)
[[nodiscard]] SightLine lineOfSight(const raycast::CollisionMesh* mesh, anim::Vec3 from, anim::Vec3 to);

/// Whether `point` lies within `fieldOfView` (a full angle, radians) of `human`'s facing: the angle between the
/// facing and the direction to the point in plan is at most half of it (the dot product against cos(fov × 0.5)). A
/// point on the human counts as in view.
/// @orig 0x00222710 Human_IsInFieldOfView (unknown)
[[nodiscard]] bool inFieldOfView(float fieldOfView, const human::Human& human, anim::Vec3 point);

/// Whether `viewer` can see `other` (`Human_CanSeeHuman`): `other` within `range` in 3D, then the line of sight.
/// **Coney stand-in**: no human hides in shadow yet, so the shorter shadow range never applies.
/// @orig 0x002223e8 Human_CanSeeHuman (unknown)
[[nodiscard]] bool canSeeHuman(const raycast::CollisionMesh* mesh, const human::Human& viewer,
                               const human::Human& other, float range);

} // namespace coney::ai
