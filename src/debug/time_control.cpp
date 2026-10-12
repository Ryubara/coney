// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/time_control.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace coney::debug {

void TimeControl::stepOnce() {
    if (m_paused) {
        ++m_queued;
    }
}

void TimeControl::setScale(double scale) {
    // Clamped before rounding, so a huge, negative or not-a-number value cannot overflow the conversion.
    const double hundredths =
        std::isnan(scale) ? 100.0 : std::clamp(scale * 100.0, 0.0, static_cast<double>(kMaxScalePercent));
    setScalePercent(static_cast<int>(std::lround(hundredths)));
}

void TimeControl::setScalePercent(int percent) {
    m_percent = std::clamp(percent, kMinScalePercent, kMaxScalePercent);
    // Owed just short of a step, so the first due step after a change runs at once.
    m_credit = 100 - std::min(m_percent, 100);
}

std::uint32_t TimeControl::dueSpeedPercent() const { return static_cast<std::uint32_t>(std::max(m_percent, 100)); }

bool TimeControl::shouldStep() {
    ++m_frames;
    bool step = false;
    if (m_paused) {
        // Paused: only the steps asked for, one per due step.
        if (m_queued > 0) {
            --m_queued;
            step = true;
        }
    } else if (m_percent >= 100) {
        // At 1x or faster every due step runs; the frame clock makes more of them due.
        step = true;
    } else {
        // Slower: each due step owes the scale's hundredths of a step, and a step runs for each whole one owed.
        m_credit += m_percent;
        if (m_credit >= 100) {
            m_credit -= 100;
            step = true;
        }
    }
    if (step) {
        ++m_steps;
    }
    return step;
}

std::string formatTimeScale(int percent) {
    // std::format's shortest form: 0.25, 1, 2.5.
    return std::format("{}x", percent / 100.0);
}

} // namespace coney::debug
