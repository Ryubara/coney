// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "animation/anim_math.h"

namespace coney::world_objects {

/// The dealers' icons, by dealer type (0 flash, 1 weapons, 2 spray paint): what `DealerGoal_Start` attaches.
inline constexpr std::string_view kFlashDealerIcon = "dyn_flashdeal";
inline constexpr std::string_view kWeaponDealerIcon = "dyn_weapdeal";
inline constexpr std::string_view kSprayDealerIcon = "dyn_spraydeal";

/// The icon a dealer of `type` wears; empty for another type.
[[nodiscard]] std::string_view dealerIcon(int type);

/// The object type an icon name spawns (`Human_ShowOverheadIcon`, docs/research/characters.md#spinning-icon): a name
/// whose first 9 characters are `dyn_p_one` or `dyn_p_two` becomes `dyn_play_one` or `dyn_play_two` (the `_euro`
/// forms are for the European discs' languages; Coney plays the English disc); any other is its own type.
[[nodiscard]] std::string iconTypeName(std::string_view name);

/// Where a spinning icon object (script type `dyn_icon`) is drawn: its world position and rotation.
struct IconPose {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// The pose of icon object `name` attached to a human whose feet are at `feet` (game axes) facing `heading` (radians)
/// at game time `ms` (docs/research/characters.md#spinning-icon): (0, 0, 2.5) m above the feet for the dealers' icons,
/// (0, 0, 2.25) for the others; its local rotation a half turn about z, turning at 180° a second about the human's up
/// axis (`dyn_cuffs` at 90°), except the player markers (`dyn_play_one`, `dyn_play_two`, their `_euro` forms) and
/// `dyn_lizziestarget`, which keep the half turn and so face with the human. **Coney stand-ins**: the turn's phase
/// follows the game time, not the attachment; `dyn_cross` and `dyn_cuffs`, which take their type record's offset, sit
/// at 2.25 m like the others, and the cuffs stay attached. Inferred (the page): the attachment's origin is the feet.
///
/// Research: docs/research/ai.md#dealer-icon
/// @orig 0x003e8fa0 DynIcon_Init (unknown)
/// @orig 0x003e92e0 DynIcon_Update (unknown)
[[nodiscard]] IconPose spinningIconPose(std::string_view name, anim::Vec3 feet, float heading, std::uint64_t ms);

/// Whether icon `name` is hidden while player 1's letterbox is up (alpha 0): every icon but `dyn_cross`.
[[nodiscard]] bool hiddenByLetterbox(std::string_view name);

} // namespace coney::world_objects
