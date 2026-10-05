// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/frame_clock.h"

#include <algorithm>

namespace coney {

// Interpolated pacing starts with one step's worth of time credited, so the first frame runs one step at once.
FrameClock::FrameClock(FramePacing pacing)
    : m_pacing(pacing), m_accumulated(pacing == FramePacing::Interpolated ? kStepUnits : 0) {}

FramePlan FrameClock::advance(std::uint64_t elapsedNanoseconds) {
    if (m_pacing == FramePacing::Lockstep) {
        return FramePlan{.steps = 1, .alpha = 1.0F};
    }
    // Credit at most kMaxStepsPerFrame steps of real time: the rest is dropped (the game slows down rather than
    // catching up). Clamping the nanoseconds to a second first keeps the multiplication far from overflowing.
    const std::uint64_t nanoseconds = std::min(elapsedNanoseconds, kNanosecondsPerSecond);
    m_accumulated += std::min(nanoseconds * kUnitsPerNanosecond, kMaxStepsPerFrame * kStepUnits);

    // The accumulator held less than one step before, so now less than kMaxStepsPerFrame + 1: never more steps.
    const auto steps = static_cast<std::uint32_t>(m_accumulated / kStepUnits);
    m_accumulated -= static_cast<std::uint64_t>(steps) * kStepUnits;
    const auto alpha = static_cast<float>(static_cast<double>(m_accumulated) / static_cast<double>(kStepUnits));
    // A leftover just under one step can round to 1.0F in float; keep alpha below 1 as the plan promises.
    return FramePlan{.steps = steps, .alpha = std::min(alpha, 0.99999994F)};
}

} // namespace coney
