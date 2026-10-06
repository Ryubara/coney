// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "animation/anim_math.h"

// The triangle pick-up search (`0x0024d810`) and its choices: which loose object a human with nothing in hand picks
// up, and the clip he picks it up with.
// Research: docs/research/combat.md#breakables

namespace coney::world_objects {

/// The search takes objects within this many metres of the human.
inline constexpr float kPickupReach = 1.5F;
/// The point the direction to an object is measured from lies this far behind the human.
inline constexpr float kPickupBehind = 0.1F;
/// The sight rays start this far and, when the first is blocked, this far above the feet.
inline constexpr float kPickupSightLow = 1.0F;
inline constexpr float kPickupSightHigh = 2.0F;
/// An object at least this much ahead (the cosine to the facing) scores 3, from 0 up to it 2, behind 1.
inline constexpr float kPickupAhead = 0.38F;
/// An object up to this far above the feet is picked up low (463), higher high (464).
inline constexpr float kPickupLowHeight = 0.8F;
/// The pick-up clips: one-handed, low and high.
inline constexpr int kPickupLowClip = 463;
inline constexpr int kPickupHighClip = 464;
/// The pick-up's blend into its clip, seconds.
inline constexpr float kPickupBlend = 0.2F;

/// One loose object the search may consider.
struct PickupCandidate {
    double handle = 0;     ///< The object's (its spawn record's) handle.
    anim::Vec3 position{}; ///< Where it is.
    bool pickable = false; ///< It may be picked up by triangle (object flag `0x8000`, and not a `powerup_item`).
};

/// Whether a ray from `from` to `to` meets something solid.
using SightBlocked = std::function<bool(anim::Vec3 from, anim::Vec3 to)>;

/// Whether a class's objects are picked up by triangle. **Coney stand-in** for the object flag `0x8000` (which classes
/// set it is not traced): a `pickup_item`. A `powerup_item` is walked over instead, never taken by triangle.
[[nodiscard]] bool pickableClass(std::string_view className);

/// The search for a human whose feet are at `feet`, facing `facing` (a unit vector in plan): of the pickable
/// candidates within kPickupReach, those in sight (a ray from the feet + kPickupSightLow to the object, or, when that
/// is blocked, from the feet + kPickupSightHigh), the best scored by the direction to it from kPickupBehind behind the
/// feet dotted with the facing (3 from kPickupAhead, 2 from 0, 1 behind), the first among equals. Returns its index
/// in `candidates`, or nothing. **Coney choices**: the reach is measured in plan from the feet, and the second point
/// the original also searches round (the human's position + `+0x4e0`, not traced) is left out.
/// @orig 0x0024d810 Pickup_Search (unknown)
[[nodiscard]] std::optional<std::size_t> searchPickup(anim::Vec3 feet, anim::Vec3 facing,
                                                      std::span<const PickupCandidate> candidates,
                                                      const SightBlocked& blocked);

/// The pick-up clip for an object `height` metres above the feet: kPickupLowClip up to kPickupLowHeight, else
/// kPickupHighClip.
[[nodiscard]] int pickupClip(float height);

} // namespace coney::world_objects
