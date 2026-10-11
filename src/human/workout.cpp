// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/workout.h"

#include <algorithm>
#include <cmath>

namespace coney::human {

namespace {

// Below a third of full effort (3 × effort < 1) the slow clip plays alone, slowing down; the decay there is cut to 0.3
// of its value.
constexpr float kLowDecayShare = 0.3F;
constexpr std::size_t kLevels = 3;

} // namespace

std::size_t WorkoutEffort::levelOf(float value) {
    const float level = std::floor(value + 0.5F);
    return static_cast<std::size_t>(std::clamp(level, 0.0F, static_cast<float>(kLevels - 1)));
}

float WorkoutEffort::timingFactor(const WorkoutParams& params, float lowerTime, float lowerDuration) {
    const float half = lowerDuration / 2.0F;
    if (params.factor == 1.0F || half <= 0.0F || lowerTime > half) {
        return 1.0F;
    }
    return params.factor * (1.0F - (lowerTime / half));
}

WorkoutStep WorkoutEffort::padUpdate(const WorkoutParams& params, float value, float lowerTime, float lowerDuration,
                                     bool pumped, bool quit) {
    const std::size_t level = levelOf(value);
    const float factor = timingFactor(params, lowerTime, lowerDuration);
    // 1. The pump: a press restarts the hold counter; a running counter adds the gain, counted down first when no
    // press came. **Coney's reading**: a factor of 0 (no CfgWorkoutParams yet) makes the gain unbounded, so the
    // effort goes to its cap.
    bool added = false;
    if (pumped) {
        m_hold = params.hold.at(level);
    } else if (m_hold != 0) {
        --m_hold;
    }
    if (m_hold != 0) {
        m_effort = factor > 0.0F ? std::min(1.0F, m_effort + (params.gain.at(level) / factor)) : 1.0F;
        added = true;
    }
    // 2. The decay, on an update that added nothing.
    const float decay = params.decay.at(level);
    if (!added) {
        m_effort -= 3.0F * m_effort < 1.0F ? decay * kLowDecayShare : decay * factor;
        m_effort = std::max(0.0F, m_effort);
    }
    // 3. The blend's target and rate.
    WorkoutStep step;
    if (3.0F * m_effort < 1.0F) {
        step.target = 0.0F;
        if (value == 0.0F) {
            step.rate = (3.0F * m_effort) + (2.0F * decay);
        }
    } else {
        step.target = (3.0F * m_effort) - 1.0F;
    }
    // 4. The end: triangle, or worn down at the slow clip (the rate the update before set).
    step.end = quit || (value == 0.0F && m_lastRate <= 3.0F * decay);
    m_lastRate = step.rate;
    return step;
}

WorkoutStep WorkoutEffort::aiUpdate(const WorkoutParams& params, float value) const {
    WorkoutStep step;
    if (m_effort < 1.0F) {
        step.target = 0.0F;
        if (value == 0.0F) {
            step.rate = m_effort + (3.0F * params.decay.at(0));
        }
    } else {
        step.target = m_effort - 1.0F;
    }
    return step;
}

} // namespace coney::human
