// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/slow_motion.h"

namespace coney::camera {

void SlowMotion::event(std::uint16_t type, int player) {
    if (player < 0 || player >= kPlayers) {
        return;
    }
    const std::uint32_t bit = 1U << static_cast<unsigned>(player);
    if (type == kEventOn) {
        m_marked |= bit;
        m_step = m_factor * kNormalStep;
    } else if (type == kEventOff) {
        m_marked &= ~bit;
        if (m_marked == 0) {
            m_step = kNormalStep;
        }
    }
}

} // namespace coney::camera
