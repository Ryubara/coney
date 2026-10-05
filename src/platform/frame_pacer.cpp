// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/frame_pacer.h"

#include <format>
#include <utility>

#include <SDL3/SDL.h>

namespace coney::platform {

namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;

// A count over a span of nanoseconds, per second.
double perSecond(std::uint64_t count, std::uint64_t nanoseconds) {
    return nanoseconds == 0 ? 0.0
                            : static_cast<double>(count) * static_cast<double>(kNanosecondsPerSecond) /
                                  static_cast<double>(nanoseconds);
}

} // namespace

FramePacer::FramePacer(std::uint32_t fpsCap, std::function<void(std::string_view)> report)
    : m_cap(fpsCap), m_report(std::move(report)) {}

std::uint64_t FramePacer::waitForFrame() {
    std::uint64_t now = SDL_GetTicksNS();
    if (m_cap != 0 && m_lastStart != 0) {
        // The next frame starts a whole number of periods after the epoch, so rounding never accumulates.
        ++m_capFrames;
        const std::uint64_t deadline = m_epoch + (m_capFrames * kNanosecondsPerSecond / m_cap);
        if (now < deadline) {
            SDL_DelayPrecise(deadline - now);
            now = SDL_GetTicksNS();
        } else if (now - deadline > kNanosecondsPerSecond / m_cap) {
            // More than a period late: start counting again from now instead of running frames back to back.
            m_epoch = now;
            m_capFrames = 0;
        }
    } else if (m_lastStart == 0) {
        m_epoch = now;
        m_firstStart = now;
        m_windowStart = now;
    }
    const std::uint64_t elapsed = m_lastStart == 0 ? 0 : now - m_lastStart;
    m_lastStart = now;
    return elapsed;
}

void FramePacer::endFrame(std::uint32_t steps) {
    ++m_frames;
    m_steps += steps;
    ++m_windowFrames;
    m_windowSteps += steps;
    if (!m_report) {
        return;
    }
    // One line per second of real time.
    const std::uint64_t now = SDL_GetTicksNS();
    if (now - m_windowStart >= kNanosecondsPerSecond) {
        const std::uint64_t span = now - m_windowStart;
        m_report(std::format("frame rate: {:.1f} frames/s, {:.1f} steps/s\n", perSecond(m_windowFrames, span),
                             perSecond(m_windowSteps, span)));
        m_windowStart = now;
        m_windowFrames = 0;
        m_windowSteps = 0;
    }
}

std::string FramePacer::summary() const {
    const std::uint64_t span = m_lastStart - m_firstStart;
    return std::format("frame rate: {} frames and {} steps in {:.2f} s: {:.1f} frames/s, {:.2f} steps/s\n", m_frames,
                       m_steps, static_cast<double>(span) / static_cast<double>(kNanosecondsPerSecond),
                       perSecond(m_frames, span), perSecond(m_steps, span));
}

} // namespace coney::platform
