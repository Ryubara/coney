// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/frame_clock.h"

#include <algorithm>

namespace coney {

// Interpolated pacing starts with one step's worth of time credited, so the first frame runs one step at once.
FrameClock::FrameClock(FramePacing pacing)
    : m_pacing(pacing), m_accumulated(pacing == FramePacing::Interpolated ? kScaledStepUnits : 0) {}

void FrameClock::setPacing(FramePacing pacing) {
    m_pacing = pacing;
    m_accumulated = 0;
    m_lockstepCarry = 0;
}

void FrameClock::setSpeed(std::uint32_t percent) {
    const std::uint32_t clamped = std::clamp(percent, std::uint32_t{1}, kMaxSpeedPercent);
    if (clamped == m_speed) {
        return;
    }
    // The interpolated leftover is real time already credited at the old speed: it is kept, so nothing is lost or
    // invented. The lockstep carry counts hundredths of a step at the old speed, so it starts again.
    m_speed = clamped;
    m_lockstepCarry = 0;
}

std::uint32_t FrameClock::maxStepsPerFrame() const {
    // The cap grows with the speed, rounded up to whole multiples: full speed down to 7.5 frames a second at any speed.
    const std::uint32_t multiple = std::max<std::uint32_t>(1, (m_speed + 99) / 100);
    return kMaxStepsPerFrame * multiple;
}

FramePlan FrameClock::advance(std::uint64_t elapsedNanoseconds) {
    if (m_pacing == FramePacing::Lockstep) {
        // One step a frame at 100 %; another speed runs its hundredths of a step a frame, whole steps only (1.5x: 1, 2,
        // 1, 2).
        m_lockstepCarry += m_speed;
        const std::uint32_t steps = m_lockstepCarry / 100;
        m_lockstepCarry %= 100;
        return FramePlan{.steps = steps, .alpha = 1.0F};
    }
    // Credit at most maxStepsPerFrame() steps of real time, scaled by the speed: the rest is dropped (the game slows
    // down rather than catching up). Clamping the nanoseconds to a second first keeps the products far from
    // overflowing (at most 10^9 x 294,912 x kMaxSpeedPercent, under 3 x 10^17).
    const std::uint64_t nanoseconds = std::min(elapsedNanoseconds, kNanosecondsPerSecond);
    const std::uint64_t cap = static_cast<std::uint64_t>(maxStepsPerFrame()) * kScaledStepUnits;
    m_accumulated += std::min(nanoseconds * kUnitsPerNanosecond * m_speed, cap);

    // The accumulator held less than one step before, so now less than the cap + 1: never more steps.
    const auto steps = static_cast<std::uint32_t>(m_accumulated / kScaledStepUnits);
    m_accumulated -= static_cast<std::uint64_t>(steps) * kScaledStepUnits;
    const auto alpha = static_cast<float>(static_cast<double>(m_accumulated) / static_cast<double>(kScaledStepUnits));
    // A leftover just under one step can round to 1.0F in float; keep alpha below 1 as the plan promises.
    return FramePlan{.steps = steps, .alpha = std::min(alpha, 0.99999994F)};
}

} // namespace coney
