// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "core/game_random.h"

// Which crew mate says a tag line (docs/research/sound-events.md#tag-lines).

namespace coney {

/// Which of `candidates` crew mates, in their gang's slot order, says a tag line: the first, and each later one
/// replaces the pick when `Random_Int(100)` on the tactics stream (RandomStream::Tactics, `0x006eb8a0`) is below 50,
/// so it is not the nearest. One draw per candidate after the first; nothing for no candidates.
/// @orig 0x00273a68 Tag_SayNearbyLine (unknown)
[[nodiscard]] std::optional<std::size_t> pickTagSpeaker(std::size_t candidates, GameRandom& random);

} // namespace coney
