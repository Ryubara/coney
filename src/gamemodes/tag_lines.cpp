// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/tag_lines.h"

namespace coney {

std::optional<std::size_t> pickTagSpeaker(std::size_t candidates, GameRandom& /*random*/) {
    // Stub: the first candidate, no draw.
    if (candidates == 0) {
        return std::nullopt;
    }
    return 0;
}

} // namespace coney
