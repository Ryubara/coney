// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/frame_rate_meter.h"

#include <algorithm>

namespace coney {

FrameRateMeter::FrameRateMeter(std::uint64_t windowNanoseconds)
    : m_window(std::max<std::uint64_t>(windowNanoseconds, 1)) {}

bool FrameRateMeter::add(std::uint64_t nanoseconds, std::uint32_t steps) {
    if (nanoseconds == 0) {
        return false;
    }
    m_spanNanoseconds += nanoseconds;
    ++m_frames;
    m_steps += steps;
    if (m_spanNanoseconds < m_window) {
        return false;
    }
    // Rates over the span the frames really took, which ends a little past the window.
    const auto seconds = static_cast<double>(m_spanNanoseconds) / static_cast<double>(kSecond);
    m_reading = FrameRateReading{.framesPerSecond = static_cast<double>(m_frames) / seconds,
                                 .frameMilliseconds = static_cast<double>(m_spanNanoseconds) / 1'000'000.0 /
                                                      static_cast<double>(m_frames),
                                 .stepsPerSecond = static_cast<double>(m_steps) / seconds};
    m_spanNanoseconds = 0;
    m_frames = 0;
    m_steps = 0;
    return true;
}

} // namespace coney
