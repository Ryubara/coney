// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/time_control.h"

#include <algorithm>

namespace coney::debug {

void TimeControl::stepOnce() {
    if (m_paused) {
        ++m_queued;
    }
}

void TimeControl::setSlowMotion(int divisor) {
    m_divisor = std::clamp(divisor, 1, kMaxDivisor);
    m_phase = 0;
}

bool TimeControl::shouldStep() {
    ++m_frames;
    bool step = false;
    if (m_paused) {
        // Paused: only the steps asked for, one per frame.
        if (m_queued > 0) {
            --m_queued;
            step = true;
        }
    } else {
        // Running: one step in every `divisor` frames; the first frame after a change steps at once.
        step = m_phase == 0;
        m_phase = (m_phase + 1) % m_divisor;
    }
    if (step) {
        ++m_steps;
    }
    return step;
}

} // namespace coney::debug
