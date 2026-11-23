// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>

#include "console/registry.h"
#include "core/error.h"
#include "debug/debug_session.h"

namespace coney::debug {

/// Registers the developer set's `lua <line>` (docs/guides/studio/console.md#developer-set) in `registry`: the line
/// runs in `session`'s script state (the level's own Lua 4.0 state while a level plays, else the sandbox's), with the
/// Lua console page's evaluator (LuaConsole::run(), one entry of the state's scheduler), and what it prints and returns
/// goes to the terminal. A session command: it waits for the step boundary, and online only the host may run it.
/// `session` must outlive the registry. Fails as Registry::addBuiltin() does, so a registry that takes Player commands
/// only (the release build's) refuses it.
/// @coney-only debug tooling: the console's developer set; the original has no console
[[nodiscard]] std::expected<console::CommandId, Error> registerLuaCommand(console::Registry& registry,
                                                                          DebugSession& session);

} // namespace coney::debug
