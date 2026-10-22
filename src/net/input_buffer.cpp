// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/input_buffer.h"

#include <cstdint>

#include "core/assert.h"
#include "core/pad.h"

namespace coney::net {

InputBuffer::InputBuffer(InputBufferConfig config) : m_config(config) {
    CONEY_ASSERT(m_config.delaySteps + m_config.slackSteps < kCapacity / 2);
}

void InputBuffer::add(std::uint32_t step, const PadSample& pad) {
    const auto at = static_cast<std::int64_t>(step);
    if (!m_started) {
        // The first step to arrive sets the delay: play delaySteps behind it. The steps before it that are still on
        // their way fill that gap; any that never come play as missing.
        m_started = true;
        m_next = at - static_cast<std::int64_t>(m_config.delaySteps);
    }
    if (at < m_next) {
        return; // played already, or skipped: too late to matter
    }
    Slot& slot = slotOf(at);
    if (slot.step == at) {
        return; // every packet repeats the steps before it; the first copy is the one
    }
    slot.step = at;
    slot.pad = pad;
    if (at > m_newest) {
        m_newest = at;
        m_quiet = 0;
    }
}

PadSample InputBuffer::take() {
    if (!m_started) {
        return m_current;
    }
    // Steps since a new step arrived: 0 on the server step it arrived in.
    const std::uint32_t quiet = m_quiet++;

    // Too far behind the newest step (a stall that has ended, or a burst): skip ahead to the delay wanted, dropping
    // the oldest steps rather than playing every one late.
    const auto delay = static_cast<std::int64_t>(m_config.delaySteps);
    const auto most = delay + static_cast<std::int64_t>(m_config.slackSteps);
    if (m_newest - m_next > most) {
        const std::int64_t to = m_newest - delay;
        m_stats.skipped += static_cast<std::uint64_t>(to - m_next);
        m_next = to;
    }

    if (m_next > m_newest) {
        // Nothing new has come: hold the last pad and wait for it, so the delay grows rather than the step being lost.
        ++m_stats.stalled;
        if (quiet > m_config.holdSteps && m_played) {
            PadSample rest; // let go: still plugged in (if it was), nothing held
            rest.connected = m_current.connected;
            m_current = rest;
        }
        return m_current;
    }

    // This step's pad if it came; else the step before's played again (none: before the first, still unplugged).
    // Steps below 0 are the delay filling at the start: never sent, so not missed either.
    if (m_next >= 0) {
        const Slot& slot = slotOf(m_next);
        if (slot.step == m_next) {
            m_current = slot.pad;
            m_played = true;
            ++m_stats.applied;
        } else {
            ++m_stats.missed;
        }
    }
    ++m_next;
    return m_current;
}

std::int64_t InputBuffer::lead() const { return m_started ? m_newest - m_next + 1 : 0; }

} // namespace coney::net
