// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/stop_watch.h"

#include <utility>

#include "core/game_timer.h"

namespace coney {

namespace {

// The gap between warning beeps, ms.
constexpr std::int32_t kBeepGapMs = 1000;
// GameTimer ticks in a millisecond.
constexpr std::uint64_t kTicksPerMs = GameTimer::kTicksPerSecond / 1000;

} // namespace

void StopWatch::set(std::int32_t timeMs, std::int32_t targetMs, std::string callback) {
    m_time = timeMs;
    m_target = targetMs;
    m_callback = std::move(callback);
    m_warning = 0;
    m_lastBeep = kBeepGapMs;
}

void StopWatch::start(bool run, std::uint64_t nowTicks) {
    m_running = run;
    m_lastTicks = nowTicks;
}

void StopWatch::setWarning(std::int32_t warnMs) {
    m_warning = warnMs;
    m_lastBeep = warnMs + kBeepGapMs;
}

StopWatch::Step StopWatch::step(std::uint64_t nowTicks) {
    Step result;
    // The whole milliseconds since the last reading (GameTimer_MsSince truncates), and nothing until one has passed;
    // the reading is kept whole, so each update drops its fraction of a millisecond (33 of a step's 33.33).
    if (!m_running || nowTicks <= m_lastTicks) {
        return result;
    }
    const std::uint64_t passedMs = (nowTicks - m_lastTicks) / kTicksPerMs;
    if (passedMs == 0) {
        return result;
    }
    const auto passed = static_cast<std::int64_t>(static_cast<float>(passedMs) * m_rate);
    m_lastTicks = nowTicks;
    const bool down = m_target < m_time;
    const std::int64_t next =
        down ? static_cast<std::int64_t>(m_time) - passed : static_cast<std::int64_t>(m_time) + passed;
    // Reaching or passing the target clamps the time there and stops the watch.
    if ((down && next <= m_target) || (!down && next >= m_target)) {
        m_time = m_target;
        m_running = false;
        result.finished = true;
        return result;
    }
    m_time = static_cast<std::int32_t>(next);
    // Counting down inside the warning window: a beep at most once a second.
    if (down && m_time <= m_warning && m_time <= m_lastBeep - kBeepGapMs) {
        m_lastBeep = m_time;
        result.beep = true;
    }
    return result;
}

} // namespace coney
