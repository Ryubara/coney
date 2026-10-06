// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
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
/// The pick-up clips: one-handed, low and high (a type's pick-up animation 5), and a weapon's from the ground.
inline constexpr int kPickupLowClip = 463;
inline constexpr int kPickupHighClip = 464;
inline constexpr int kPickupGroundClip = 461;
/// The pick-up animation (`CfgObj` argument 14) of the one-handed clips.
inline constexpr int kPickupOneHanded = 5;
/// The pick-up's blend into its clip, seconds.
inline constexpr float kPickupBlend = 0.2F;

/// One loose object the search may consider.
struct PickupCandidate {
    double handle = 0;     ///< The object's (its spawn record's) handle.
    anim::Vec3 position{}; ///< Where it is.
    bool pickable = false; ///< It may be picked up by triangle (pickable(), and not a `powerup_item`).
};

/// Whether a ray from `from` to `to` meets something solid.
using SightBlocked = std::function<bool(anim::Vec3 from, anim::Vec3 to)>;

/// Whether triangle picks up an object of class `className` whose model hash is `modelHash`: the classes whose init
/// sets flag `0x8000` (docs/research/objects.md#pickable), `melee_weapon`, `thrown_weapon`, `overhead_weapon` and
/// `pickup_item`, but for the hashes they exclude, and a `simple_object` whose hash adds it. A `powerup_item` has the
/// flag but is walked over instead, never taken by triangle. **Coney's reading**: the messages that set or clear the
/// flag later (`0x19`) are not modelled.
/// @orig 0x003fd420 MeleeWeapon_Init (unknown)
/// @orig 0x003f17e0 PickupItem_Init (unknown)
[[nodiscard]] bool pickable(std::string_view className, std::uint32_t modelHash);

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

/// The pick-up clip for an object of pick-up animation `pickupAnim` `height` metres above the feet: the one-handed
/// pair (kPickupOneHanded) kPickupLowClip up to kPickupLowHeight, else kPickupHighClip. **Coney stand-in**: any other
/// animation plays kPickupGroundClip, as a bat (animation 1) on the ground did at runtime; how the others choose is
/// not traced.
[[nodiscard]] int pickupClip(int pickupAnim, float height);

} // namespace coney::world_objects
