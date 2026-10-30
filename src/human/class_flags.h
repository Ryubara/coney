// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "combat/power_class.h"

// The power class's switches in the human flags: `Human_ApplyClassFlags` copies bytes `+0x3a`-`+0x3f` of a human's
// power class into its flag word when it is made, made a player or given back to the AI.
// Research: docs/research/human-init.md#class-switches, docs/research/combat/index.md#human-flags

namespace coney::human {

/// What a change does to the flag word: the bits it sets and the bits it clears (never both for one bit).
struct FlagChanges {
    std::uint64_t set = 0;     ///< Bits turned on.
    std::uint64_t cleared = 0; ///< Bits turned off.
};

/// The flags `power`'s switches give: each of `+0x3a`-`+0x3d` at 1 sets its bit (flag::kUngroundable,
/// flag::kHardToStun, flag::kNoEscape, flag::kBlockFromReact) and at any other value clears it; `+0x3e` at 1 sets
/// flag::kIncreasedReact and clears flag::kIncreasedReactOff, at -1 the reverse, at any other value clears both;
/// `+0x3f` the same for flag::kComboStrength (1) and flag::kReducedReact (-1). No other bit changes.
/// @orig 0x00222c98 Human_ApplyClassFlags (unknown)
[[nodiscard]] FlagChanges classFlagChanges(const combat::PowerClass& power);

/// `flags` with `changes` made.
/// @coney-only Coney's write of FlagChanges into a flag word; the original writes each switch's bits in place
[[nodiscard]] constexpr std::uint64_t applied(std::uint64_t flags, FlagChanges changes) {
    return (flags & ~changes.cleared) | changes.set;
}

} // namespace coney::human
