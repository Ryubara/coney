// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/present_mode.h"

namespace coney::platform {

// Stub for the test commit.
PresentMode choosePresentMode(bool /*vsync*/, std::uint32_t /*fpsCap*/, PresentModes /*offered*/) {
    return PresentMode::Vsync;
}

std::string_view presentModeName(PresentMode /*mode*/) { return {}; }

} // namespace coney::platform
