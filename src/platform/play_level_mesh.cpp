// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the tests' commit: no human has atomics here; the next commit implements them.
#include "platform/play_level_controls.h"

namespace coney::platform {

std::vector<const rw::Atomic*> PlayLevelControls::atomicsOf(const PlayLevelMode& /*mode*/, debug::EntityId /*id*/) {
    return {};
}

} // namespace coney::platform
