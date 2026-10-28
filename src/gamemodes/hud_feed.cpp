// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/hud_feed.h"

namespace coney {

void feedHumanToHud(hud::HudFrame& frame, std::size_t slot, const human::Human& human) {
    static_cast<void>(frame);
    static_cast<void>(slot);
    static_cast<void>(human);
}

} // namespace coney
