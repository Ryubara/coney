// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/console_lua.h"

namespace coney::debug {

// Stub: the command is not built yet.
std::expected<console::CommandId, Error> registerLuaCommand(console::Registry& /*registry*/,
                                                            DebugSession& /*session*/) {
    return std::unexpected(Error{ErrorCode::InvalidArgument, "lua: not built"});
}

} // namespace coney::debug
