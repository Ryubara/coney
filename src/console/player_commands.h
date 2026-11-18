// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <string_view>
#include <vector>

#include "console/catalog.h"
#include "console/console.h"
#include "core/error.h"

// @coney-only the Coney console's player commands (docs/guides/console.md#player-commands); the original has no
// console.

namespace coney::console {

/// The banner `help` starts with: the string the release build's `coney.release.has_player_console` check looks for in
/// the executable, so the build is proved to carry the console (docs/guides/studio/console.md#release).
inline constexpr std::string_view kConsoleBanner = "Coney console";

/// The handlers of the player catalog's commands that are built: `help`, `clear`, `quit`, `version`, `where`,
/// `bugreport`, `settings`, `get`, `set`, `missions`, `mission`, `host`, `connect` and `disconnect`. Each reaches the
/// game through `console`'s hosts when it runs, so a host that is absent (a test's, a run with no network play) is
/// answered with a line saying so. `console` must outlive the handlers, as it owns the registry they are registered in.
[[nodiscard]] std::vector<BuiltinHandler> playerHandlers(Console& console);

/// Registers the player catalog's built entries on `console` with their handlers (registerBuiltins() over
/// playerCatalog() and playerHandlers()) and returns how many it registered. An entry that waits for a feature that is
/// not built, or for a handler a later build issue adds, stays absent from the registry.
[[nodiscard]] std::expected<std::size_t, Error> registerPlayerCommands(Console& console);

} // namespace coney::console
