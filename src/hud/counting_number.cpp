// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/counting_number.h"

namespace coney::hud {

int countStep(int shown, int target) {
    const int difference = target - shown;
    if (difference == 0) {
        return 0;
    }
    // The sixteenth closes most of a large gap; the unit makes sure a gap under 16 still closes, and never overshoots.
    int step = difference / 16 + (difference > 0 ? 1 : -1);
    if ((difference > 0 && step > difference) || (difference < 0 && step < difference)) {
        step = difference;
    }
    return step;
}

bool CountingNumber::update(int value, std::uint64_t nowMs) {
    if (!m_started) {
        m_started = true;
        m_shown = value;
        m_value = value;
        return false;
    }
    bool changed = false;
    if (value != m_value) {
        m_popup = Popup{value - m_value, nowMs};
        m_value = value;
        changed = true;
    }
    m_shown += countStep(m_shown, m_value);
    return changed;
}

} // namespace coney::hud
