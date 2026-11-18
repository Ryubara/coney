// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/player_commands.h"

// @coney-only the Coney console's player commands (docs/guides/console.md#player-commands); the original has no
// console.

namespace coney::console {

// A stub: no handlers yet, so every catalog entry stays out of the registry, as before the player commands.
std::vector<BuiltinHandler> playerHandlers(Console& /*console*/) { return {}; }

std::expected<std::size_t, Error> registerPlayerCommands(Console& console) {
    return registerBuiltins(console.registry(), playerCatalog(), {});
}

} // namespace coney::console
