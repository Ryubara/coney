// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "animation/anim_math.h"

namespace coney::world_objects {

/// The dealers' icons, by dealer type (0 flash, 1 weapons, 2 spray paint): what `DealerGoal_Start` attaches.
inline constexpr std::string_view kFlashDealerIcon = "dyn_flashdeal";
inline constexpr std::string_view kWeaponDealerIcon = "dyn_weapdeal";
inline constexpr std::string_view kSprayDealerIcon = "dyn_spraydeal";

/// The icon a dealer of `type` wears; empty for another type.
[[nodiscard]] std::string_view dealerIcon(int type);

/// Where a spinning icon object (script type `dyn_icon`) is drawn: its world position and rotation.
struct IconPose {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// The pose of icon object `name` attached to a human whose feet are at `feet` (game axes) at game time `ms` (the
/// turn's phase is Coney's: the original starts it at the attachment): (0, 0, 2.5) m above the feet for the dealers'
/// icons, (0, 0, 2.25) for the others, turning about the world z axis at 180° a second, except the player markers
/// (`dyn_play_one`, `dyn_play_two`, their `_euro` forms) and `dyn_lizziestarget`, which do not turn. **Coney
/// stand-in**: `dyn_cross` and `dyn_cuffs`, which the original puts at bone 11, sit at 2.25 m like the others. Inferred
/// (the page): the attachment's origin is the feet.
///
/// Research: docs/research/ai.md#dealer-icon
/// @orig 0x003e8fa0 DynIcon_Init (unknown)
/// @orig 0x003e92e0 DynIcon_Update (unknown)
[[nodiscard]] IconPose spinningIconPose(std::string_view name, anim::Vec3 feet, std::uint64_t ms);

} // namespace coney::world_objects
