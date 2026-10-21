// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/link_quality.h"

#include <cstdint>

namespace coney::net {

std::uint32_t LinkQuality::pingSent(std::uint64_t nowNs) {
    const std::uint32_t sequence = m_nextSequence++;
    m_entries[sequence % kWindow] = Entry{.sequence = sequence, .sentNs = nowNs, .used = true, .answered = false};
    return sequence;
}

void LinkQuality::pongReceived(std::uint32_t sequence, std::uint64_t nowNs) {
    Entry& entry = m_entries[sequence % kWindow];
    if (!entry.used || entry.sequence != sequence || entry.answered || nowNs < entry.sentNs) {
        return;
    }
    entry.answered = true;
    ++m_answered;
    m_lastNs = nowNs - entry.sentNs;
    // The first answer sets the average; later ones move it an eighth of the way, as TCP smooths its round trip.
    m_smoothedNs = m_answered == 1 ? m_lastNs : m_smoothedNs - (m_smoothedNs / 8) + (m_lastNs / 8);
}

float LinkQuality::lossFraction() const {
    std::uint32_t judged = 0;
    std::uint32_t lost = 0;
    for (const Entry& entry : m_entries) {
        // Only pings at least kGrace older than the newest are judged.
        if (!entry.used || m_nextSequence - entry.sequence <= kGrace) {
            continue;
        }
        ++judged;
        lost += entry.answered ? 0U : 1U;
    }
    return judged == 0 ? 0.0F : static_cast<float>(lost) / static_cast<float>(judged);
}

} // namespace coney::net
