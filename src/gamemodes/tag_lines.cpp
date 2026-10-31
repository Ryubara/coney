// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/tag_lines.h"

#include <cstdint>

namespace coney {

namespace {

// A later candidate replaces the pick on a roll of 0-100 below this.
constexpr std::int32_t kReplaceBelow = 50;
constexpr std::int32_t kRollMax = 100;

} // namespace

std::optional<std::size_t> pickTagSpeaker(std::size_t candidates, GameRandom& random) {
    if (candidates == 0) {
        return std::nullopt;
    }
    // The first is taken without a draw; each later one draws, and replaces it below kReplaceBelow.
    std::size_t picked = 0;
    for (std::size_t candidate = 1; candidate < candidates; ++candidate) {
        if (random.range(RandomStream::Tactics, 0, kRollMax) < kReplaceBelow) {
            picked = candidate;
        }
    }
    return picked;
}

} // namespace coney
