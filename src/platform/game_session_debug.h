// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include "debug/debug_session.h"
#include "platform/game_session.h"

namespace coney::platform {

/// Points the debug menus' game services (the scripts the Lua console and the Cheats page call into, the recorded
/// configuration, the game state) at the session `session` returns, whichever way it started; while it returns null
/// they have none and the menus use their sandbox state. `session` is asked anew each time.
/// In the debug tools' target (docs/guides/building.md#debug-tools).
void connectDebugServices(debug::DebugServices& services, const std::function<GameSession*()>& session);

} // namespace coney::platform
