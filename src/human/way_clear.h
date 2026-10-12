// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "animation/anim_math.h"
#include "human/human_contacts.h"

// Whether the way from one human to another is clear (`Human_IsWayClearTo`): what the revive tests before a downed
// player takes an AI's help, and a Warrior before he swaps with the player.
// Research: docs/research/combat.md#revive, docs/research/ai.md (the swap's step 4)

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

namespace coney::human {

/// The radius of the capsule the test sweeps, m.
inline constexpr float kWayClearRadius = 0.2F;

/// Whether a capsule of kWayClearRadius, upright from `from` to `height` above it, swept from `from` to `goal`'s feet
/// meets nothing before `goal` (`Human_IsWayClearTo`, confirmed (code)): clear when it meets nothing or meets `goal`
/// first. It meets the humans of `others` (their capsules, the reviver's and `goal`'s left out by the caller) and the
/// walls of `mesh` it moves into. **Coney's readings**: the collision mask `0x2803` is read as the humans and the
/// level's walls, a wall being a triangle the walking sweep counts (taller than 0.25 m); objects, cars and glass are
/// not swept; the walls are tested by the capsule's spheres every 0.1 m of the way. No mesh: only the humans count.
/// @orig 0x0021c0a8 Human_IsWayClearTo (unknown)
[[nodiscard]] bool wayClear(const raycast::CollisionMesh* mesh, anim::Vec3 from, float height,
                            const UprightCapsule& goal, std::span<const UprightCapsule> others);

} // namespace coney::human
