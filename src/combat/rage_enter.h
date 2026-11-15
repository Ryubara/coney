// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::combat {

/// The record `+0x08` bits that keep a pair from being broken as rage starts (`Human_HasHeldFlag(h, 0x2fefefff)`).
inline constexpr std::uint32_t kRagePairHeldMask = 0x2fefefff;
/// The record `+0x08` bits that keep rage's start from playing its clip (`Human_HasHeldFlag(h, 0xfc1fe7b)`).
inline constexpr std::uint32_t kRageClipBusyMask = 0x0fc1fe7b;

/// What `Rage_Enter` reads of the human whose rage starts (docs/research/combat-rage.md#rage-effects).
struct RageEnterState {
    bool paired = false;       ///< In a pair or a paired move: state `0x18003ff0` (`Human_IsInPairOrBlock`).
    std::uint32_t held = 0;    ///< The record `+0x08` bits its moves hold.
    bool downOrCuffed = false; ///< State `0xe0000`: cuffed, knocked out or knocked down (`Human_HasStateE0000`).
};

/// What rage's start does besides the rage sound and starting rage, which it always does.
struct RageEnterSteps {
    bool breakPair = false; ///< The pair is broken first (`Human_BreakPair`).
    bool playClip = false;  ///< 643 `RAGE_START` plays full-body.
};

/// Rage's start's two decisions: whether the pair is broken, and whether 643 plays.
[[nodiscard]] constexpr RageEnterSteps rageEnterSteps(const RageEnterState& /*state*/) {
    return RageEnterSteps{.breakPair = false, .playClip = true};
}

} // namespace coney::combat
