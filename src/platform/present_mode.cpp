// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/present_mode.h"

namespace coney::platform {

PresentMode choosePresentMode(bool vsync, std::uint32_t fpsCap, PresentModes offered) {
    if (vsync) {
        return PresentMode::Vsync;
    }
    // Uncapped: the newest frame at each refresh, without tearing; else each frame at once.
    if (fpsCap == 0) {
        if (offered.mailbox) {
            return PresentMode::Mailbox;
        }
        return offered.immediate ? PresentMode::Immediate : PresentMode::Vsync;
    }
    // Capped: the pacer holds the rate, so each frame is shown at once; else the newest at each refresh.
    if (offered.immediate) {
        return PresentMode::Immediate;
    }
    return offered.mailbox ? PresentMode::Mailbox : PresentMode::Vsync;
}

std::string_view presentModeName(PresentMode mode) {
    switch (mode) {
    case PresentMode::Mailbox:
        return "mailbox";
    case PresentMode::Immediate:
        return "immediate";
    case PresentMode::Vsync:
        break;
    }
    return "vsync";
}

} // namespace coney::platform
