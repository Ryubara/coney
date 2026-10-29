// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace coney {

namespace {

// The d-pad bit of each hold counter, in the counters' order (Pad::Direction).
constexpr std::array<std::uint16_t, 4> kCounterButtons{pad::kUp, pad::kRight, pad::kDown, pad::kLeft};

// The diagonal rule (step 4 of the original's update): with more than one d-pad direction held, keep only the one
// with the highest pressure, or none when none has any pressure. Ties go to the first in pressure order (right, left,
// up, down), a Coney choice: the page does not say which wins.
std::uint16_t keepOneDirection(std::uint16_t word, const std::array<std::uint8_t, pad::kPressureCount>& pressure) {
    const std::uint16_t directions = word & pad::kDpad;
    // Zero or one direction held (a power of two, or nothing) needs no choice.
    if ((directions & (directions - 1)) == 0) {
        return word;
    }
    std::uint16_t kept = 0;
    std::uint8_t best = 0;
    // The first four pressure bytes are the d-pad's: right, left, up, down.
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint16_t bit = pad::kPressureButtons.at(i);
        if ((directions & bit) != 0 && pressure.at(i) > best) {
            best = pressure.at(i);
            kept = bit;
        }
    }
    return static_cast<std::uint16_t>((word & ~pad::kDpad) | kept);
}

} // namespace

// The table at 0x0050b8d0 in closed form: the dead zone, then each side scaled to exactly [-1, 1].
float pad::stickValue(std::uint8_t raw) {
    constexpr float kSpan = 95.0F;
    if (raw < kStickDeadLow) {
        return (static_cast<float>(raw) - static_cast<float>(kStickDeadLow)) / kSpan;
    }
    if (raw > kStickDeadHigh) {
        return (static_cast<float>(raw) - static_cast<float>(kStickDeadHigh)) / kSpan;
    }
    return 0.0F;
}

void Pad::update(const PadSample& sample, const std::optional<StickTurn>& turn) {
    // The rest mark belongs to this update alone.
    m_followStickRest = false;
    // Step 1: a new ring slot for this sample.
    m_current = (m_current + 1) % kHistory;
    m_connected = sample.connected;
    if (!sample.connected) {
        // The original stops here for a disconnected pad; Coney also empties the slot and the counters and centres the
        // sticks (the turned one too), so a pad pulled out mid-hold neither stays held nor keeps auto-repeating (a
        // Coney choice).
        m_ring.at(m_current) = 0;
        m_holdCounts = {};
        m_rawSticks = {pad::kStickCentre, pad::kStickCentre, pad::kStickCentre, pad::kStickCentre};
        m_pressure = {};
        m_leftX = m_leftY = m_rightX = m_rightY = 0.0F;
        m_turnedX = m_turnedY = 0.0F;
        return;
    }
    m_rawSticks = sample.sticks;
    m_pressure = sample.pressure;

    // The sticks, with y negated so that up is positive (the original's read stores them before step 2 reads them).
    m_rightX = pad::stickValue(m_rawSticks[0]);
    m_rightY = -pad::stickValue(m_rawSticks[1]);
    m_leftX = pad::stickValue(m_rawSticks[2]);
    m_leftY = -pad::stickValue(m_rawSticks[3]);

    // Step 2: the left stick turned by the player's camera, when the caller has one.
    if (turn) {
        // Reversed while the player's own follow camera looks behind him (it then faces the player).
        float x = m_leftX;
        float y = m_leftY;
        if (turn->followIsPlayerCamera && turn->lookingBehind) {
            x = -x;
            y = -y;
        }
        const float length = std::sqrt((x * x) + (y * y));
        if (length > 0.0F) {
            // Along the camera's axes, then back to the stick's own length (a tilted camera's axes are shorter seen
            // from above); a turn of length 0 is kept as it is.
            float turnedX = (x * turn->xAxis[0]) + (y * turn->yAxis[0]);
            float turnedY = (x * turn->xAxis[1]) + (y * turn->yAxis[1]);
            const float turnedLength = std::sqrt((turnedX * turnedX) + (turnedY * turnedY));
            if (turnedLength != 0.0F) {
                const float scale = length / turnedLength;
                turnedX *= scale;
                turnedY *= scale;
            }
            m_turnedX = turnedX;
            m_turnedY = turnedY;
        } else {
            // At rest: the stick as it is, and the player's follow camera marked to take its orientation afresh.
            m_turnedX = x;
            m_turnedY = y;
            // A stand-in for the original's write of 1 to that camera's +0x473: the record reports it for the caller,
            // as no Coney camera keeps the flag yet.
            m_followStickRest = turn->hasFollow;
        }
    }

    // Step 3: the hold counters, from the word as read.
    countHolds(sample.buttons);

    // Step 4: the diagonal rule, on the word the queries see.
    m_ring.at(m_current) = keepOneDirection(sample.buttons, m_pressure);
}

void Pad::countHolds(std::uint16_t word) {
    for (std::size_t i = 0; i < m_holdCounts.size(); ++i) {
        std::uint8_t& count = m_holdCounts.at(i);
        if ((word & kCounterButtons.at(i)) == 0) {
            count = 0;
        } else if (count == kRepeatCount) {
            // Wrap 15 to 12, so the count is 15 again after four more samples: the repeat beat.
            count = kRepeatCount - 3;
        } else {
            ++count;
        }
    }
}

std::uint16_t Pad::buttons(std::size_t back) const {
    const std::size_t steps = std::min(back, kHistory - 1);
    return m_ring.at((m_current + kHistory - steps) % kHistory);
}

std::uint16_t Pad::pressed() const { return static_cast<std::uint16_t>(buttons(0) & ~buttons(1)); }

std::uint16_t Pad::released() const { return static_cast<std::uint16_t>(~buttons(0) & buttons(1)); }

std::uint16_t Pad::pressedWithRepeat() const {
    std::uint16_t word = pressed();
    for (std::size_t i = 0; i < m_holdCounts.size(); ++i) {
        if (m_holdCounts.at(i) == kRepeatCount) {
            word |= kCounterButtons.at(i);
        }
    }
    return word;
}

} // namespace coney
