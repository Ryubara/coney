// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::ai {

class Brain;

/// The swap prompt's `GSTRING.HUD` strings: both hold an object, the Warrior holds one, the player holds one.
inline constexpr std::uint32_t kSwapBothHold = 0xc;
inline constexpr std::uint32_t kSwapWarriorHolds = 0xd;
inline constexpr std::uint32_t kSwapPlayerHolds = 0xe;
/// The swap prompt's reach, metres.
inline constexpr float kSwapReach = 1.5F;

/// Step 7 of a Warrior's think, the swap prompt: the human is talkable (`+0x1b2`) exactly when the nearest player
/// within 1.5 m stands at a gait below jog, one of the two holds an object, and this Warrior stands (gait 0) in the
/// player's gang with no attacker slots taken on him and in front of the player (`Human_GetSideOf` = 0); then its
/// prompt is `GSTRING.HUD` 0xc (both hold), 0xd (he holds) or 0xe (the player holds), which replaces any text of its
/// own. **Coney's stand-ins**: the blocked and held-flag tests and the sparring byte (`+0x2e5`) and `+0x121` are not
/// modelled, so they pass or fail as if clear; the two-player swap (`PlayerBrain_Think`) is not built.
///
/// Research: docs/research/ai.md#think-warrior, docs/research/hud.md#talk-prompt
/// @orig 0x003052f0 WarriorBrain_Think (unknown)
void thinkSwapPrompt(Brain& brain);

} // namespace coney::ai
