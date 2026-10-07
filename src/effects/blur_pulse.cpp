// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/blur_pulse.h"

#include <algorithm>
#include <cmath>

namespace coney::effects {

void BlurPulse::start(float seconds, std::uint32_t delayMs) { begin(seconds, false, delayMs, false); }

void BlurPulse::end(float seconds) { begin(seconds, true, 0, false); }

void BlurPulse::queueStart() { begin(m_look.inSeconds, false, 0, true); }

void BlurPulse::queueEnd(float seconds) { end(seconds > 0.0F ? m_look.outSeconds : 0.0F); }

void BlurPulse::begin(float seconds, bool reverse, std::uint32_t delayMs, bool autoEnd) {
    m_state = State::Started;
    m_delayMs = delayMs;
    m_startMs = m_nowMs;
    m_autoEnd = autoEnd;
    if (!reverse) {
        if (seconds <= 0.0F) {
            m_level = 1.0F;
            m_rate = 1.0F;
        } else if (m_level <= 0.0F) {
            m_rate = 1.0F / seconds;
        } else {
            // Part-way up: the level is kept and the rate is the original's 1 / (s × (1 − level)).
            const float span = seconds * (1.0F - m_level);
            m_rate = span != 0.0F ? 1.0F / span : 1.0F;
        }
        return;
    }
    // Reversed: a fall from full, or off at once.
    m_autoEnd = false;
    if (seconds > 0.0F) {
        m_level = 1.0F;
        m_rate = -1.0F / seconds;
    } else {
        m_level = 0.0F;
        m_rate = -1.0F;
        m_state = State::Off;
    }
}

void BlurPulse::step(float seconds) {
    // The clock first, in whole milliseconds, carrying the rest.
    m_carryMs += static_cast<double>(seconds) * 1000.0;
    const double whole = std::floor(m_carryMs);
    m_nowMs += static_cast<std::uint64_t>(whole);
    m_carryMs -= whole;

    m_passes.reset();
    if (m_state == State::Off) {
        return;
    }
    // Waiting out the delay: nothing moves or draws.
    if (m_level == 0.0F && m_nowMs < m_startMs + m_delayMs) {
        return;
    }
    switch (m_state) {
    case State::Off:
        return;
    case State::Started:
        // Moving from the next update; drawn now only at full.
        m_state = State::Moving;
        if (m_level < 1.0F) {
            return;
        }
        break;
    case State::Moving:
        m_level = std::clamp(m_level + m_rate * seconds, 0.0F, 1.0F);
        if (m_level >= 1.0F && m_rate > 0.0F) {
            m_state = State::Held;
            m_heldMs = m_nowMs;
        } else if (m_level <= 0.0F && m_rate < 0.0F) {
            m_state = State::Off;
            m_autoEnd = false;
            return;
        }
        break;
    case State::Held:
        if (m_autoEnd && m_nowMs - m_heldMs >= m_look.holdMs) {
            end(m_look.outSeconds);
        }
        break;
    }
    m_passes = static_cast<int>(m_level * static_cast<float>(m_look.passes));
}

} // namespace coney::effects
