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

/// The class of the items lying in the world (docs/research/script-types.md#pickup-item): the store's jewellery,
/// the cash, the bottles of pills; each keeps a glint while it lies (docs/research/particles.md#glints), unless
/// neverGlints().
inline constexpr std::string_view kPickupItemClass = "pickup_item";
/// The class of the power-ups a human takes by walking over them (docs/research/player-state.md#walk-over).
inline constexpr std::string_view kPowerupItemClass = "powerup_item";

/// Whether a `pickup_item` of model `modelHash` (CRC-32 of the model's name) never glints: the seven hobo foods
/// (`dyn_hobo_donut_a`/`_b`/`_c`, `dyn_hobo_hotdog`, `dyn_hobo_mug`, `dyn_hobo_salami`, `dyn_hobo_steak`), item
/// `+0x20` set by `PickupItem_Init` (`0x003f17e0`; docs/research/script-types.md#pickup-item).
[[nodiscard]] bool neverGlints(std::uint32_t modelHash);

/// The search takes objects within this many metres of the human.
inline constexpr float kPickupReach = 1.5F;
/// The point the direction to an object is measured from lies this far behind the human.
inline constexpr float kPickupBehind = 0.1F;
/// The sight rays start this far and, when the first is blocked, this far above the feet.
inline constexpr float kPickupSightLow = 1.0F;
inline constexpr float kPickupSightHigh = 2.0F;
/// An object at least this much ahead (the cosine to the facing) scores 3, from 0 up to it 2, behind 1.
inline constexpr float kPickupAhead = 0.38F;
/// An object up to this far above the feet is picked up with its pair's low clip, higher with the high one.
inline constexpr float kPickupLowHeight = 0.8F;
/// The left-handed pair (pick-up animation 5 `LeftHandPickUp`), low and high, and the one-handed low clip (0, 1 and
/// any value above 7).
inline constexpr int kPickupLowClip = 463;
inline constexpr int kPickupHighClip = 464;
inline constexpr int kPickupGroundClip = 461;
/// The pick-up animation (`CfgObj` argument 14) of the left-handed pair.
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

/// Whether an object at `at` is in sight of a human whose feet are at `feet`: a ray from the feet + kPickupSightLow to
/// it, or, when that is blocked, one from the feet + kPickupSightHigh. Always in sight without `blocked`.
/// @orig 0x0021c570 Human_CanSeeObject (unknown)
[[nodiscard]] bool inSight(anim::Vec3 feet, anim::Vec3 at, const SightBlocked& blocked);

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

/// The pick-up clip for an object of pick-up animation `pickupAnim` `height` metres above the feet: the animation's
/// pair, low up to kPickupLowHeight, else high (0, 1 and above 7: 461/462; 2: 503/504; 3: 481/482; 4: 498/499;
/// 5: 463/464; 6: 465 at any height; 7: 549/550; docs/research/combat.md#bat).
/// @orig 0x0025e5a8 Human_PickUpMessage (unknown)
[[nodiscard]] int pickupClip(int pickupAnim, float height);

} // namespace coney::world_objects
