// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Which side a fighter the debug menus spawn takes (docs/guides/debug-menu.md#pages). @coney-only debug tooling: the
// original has no spawner, but each side is made of the original's own gang machinery (ai::FighterSides).

namespace coney::ai {

/// The side of a spawned AI fighter, relative to the player: an enemy fights him and his friends, an ally is in his
/// camp and fights his enemies, a neutral is nobody's friend or enemy until someone hits it.
/// @coney-only debug tooling: the original has no spawner
enum class FighterSide : std::uint8_t { Enemy, Ally, Neutral };

/// The three sides, in the order the AI fighters page lists them.
inline constexpr std::array<FighterSide, 3> kFighterSides{FighterSide::Enemy, FighterSide::Ally, FighterSide::Neutral};

/// The side's name as the page shows it.
[[nodiscard]] constexpr std::string_view sideName(FighterSide side) {
    static_cast<void>(side);
    return {};
}

} // namespace coney::ai
