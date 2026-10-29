// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace coney::script {

/// Where the handles the bindings give out come from (`HuCreate`, `AddFlag`, the stubs that make an object): one
/// counter per session, kept by the game mode, so a human, flag or object made in one Lua state is named by the same
/// handle in every other state of the session, and a state made again after a level unload never hands out a handle
/// an earlier one gave (docs/guides/studio/lua-sdk.md#two-states). Handles start at 1, above `NilHandle` (0).
struct ScriptHandles {
    double next = 1; ///< The next handle to give out.
};

} // namespace coney::script
