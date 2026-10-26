// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the fails-first run of the Entity editor's tests: it compiles and edits nothing.
#include "platform/play_level_controls.h"

namespace coney::platform {

std::expected<void, Error> PlayLevelControls::deleteAiHuman(std::size_t /*index*/) {
    return fail(ErrorCode::InvalidArgument, "stub");
}

std::expected<void, Error> PlayLevelControls::applyEdit(const debug::EntityCommand& /*command*/) {
    return fail(ErrorCode::InvalidArgument, "stub");
}

std::optional<std::array<double, 3>> PlayLevelControls::configuredValues(debug::EntityId /*entity*/,
                                                                         debug::EditOp /*op*/) const {
    return std::nullopt;
}

std::optional<double> PlayLevelControls::currentValue(debug::EntityId /*entity*/, debug::EditOp /*op*/) const {
    return std::nullopt;
}

} // namespace coney::platform
