// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenes/letterbox.h"

#include <algorithm>
#include <cmath>

namespace coney::scenes {

LetterboxSettings& letterboxSettings() {
    static LetterboxSettings settings;
    return settings;
}

void Letterbox::start(bool in, float seconds, std::uint64_t nowMs) {
    // From wherever the bars are now, towards closed or open.
    m_from = amount(nowMs);
    m_to = in ? 1.0F : 0.0F;
    m_startMs = nowMs;
    m_durationMs = seconds > 0.0F ? static_cast<std::uint64_t>(std::lround(seconds * 1000.0F)) : 0;
}

float Letterbox::amount(std::uint64_t nowMs) const {
    if (m_durationMs == 0 || nowMs >= m_startMs + m_durationMs) {
        return m_to;
    }
    const float t =
        nowMs <= m_startMs ? 0.0F : static_cast<float>(nowMs - m_startMs) / static_cast<float>(m_durationMs);
    return m_from + ((m_to - m_from) * std::clamp(t, 0.0F, 1.0F));
}

} // namespace coney::scenes
