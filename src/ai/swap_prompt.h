// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace coney::human {
struct ScriptState;
} // namespace coney::human

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

/// The speech command the presser says after a swap that left him holding something: 146 `give_me`.
inline constexpr std::uint32_t kGiveMeCommand = 146;

/// What a swap did: the objects that changed hands and those left at their giver's feet.
struct SwapOutcome {
    double toReceiver = 0.0;     ///< The presser's object, now in the receiver's hand (0 for none).
    double toPresser = 0.0;      ///< The receiver's object, now in the presser's hand (0 for none).
    std::vector<double> dropped; ///< Objects dropped that no hand took (a left-hand or hat object).
};

/// The swap (`Human_SwapHeldObjects(receiver, presser)`): each side's held object is dropped and placed straight in
/// the other's hand, at once and with no clip; one held alone simply passes across. An object whose type
/// `placeable(typeName)` says has no hand placement (pick-up animation 5 or 6) stays dropped. Nothing else changes
/// hands. Research: docs/research/ai.md#warrior-swap
/// @orig 0x00233b08 Human_SwapHeldObjects (unknown)
SwapOutcome swapHeldObjects(human::ScriptState& receiver, human::ScriptState& presser,
                            const std::function<bool(std::string_view typeName)>& placeable);

/// Whether an object of pick-up animation `pickupAnim` can be placed in a hand by a swap: all but 5 (left hand) and 6
/// (hat), whose low pick-up clips (463, 465) have no placement event (inferred).
[[nodiscard]] constexpr bool swapPlaceable(int pickupAnim) { return pickupAnim != 5 && pickupAnim != 6; }

} // namespace coney::ai
