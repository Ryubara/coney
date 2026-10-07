// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/step_control.h"

#include <cmath>
#include <numbers>

#include "human/fighter_clips.h"
#include "human/locomotion.h"

namespace coney::human {

StepClip stepClipFor(float heading, float facing, bool stance, int boost) {
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kForwardLimit = kPi / 8.0F;        // 22.5°
    constexpr float kBackwardFrom = 5.0F * kPi / 8.0F; // 112.5°
    const float d = wrapAngle(heading - facing);
    const bool dash = !stance && boost > 0;
    // Forward: the clip goes along the facing, which ends on the heading.
    if (std::fabs(d) <= kForwardLimit) {
        return {.clip = stance ? clips::kStanceStepForward : (dash ? clips::kDashForward : clips::kStepForward),
                .endFacing = heading};
    }
    // Backward: the clip goes against the facing.
    if (std::fabs(d) >= kBackwardFrom) {
        return {.clip = stance ? clips::kStanceStepBackward : (dash ? clips::kDashBackward : clips::kStepBackward),
                .endFacing = wrapAngle(heading - kPi)};
    }
    // To the left (the heading anticlockwise of the facing): the clip goes a quarter turn anticlockwise of it.
    if (d > 0.0F) {
        return {.clip = stance ? clips::kStanceStepLeft : (dash ? clips::kDashLeft : clips::kStepLeft),
                .endFacing = wrapAngle(heading - (kPi / 2.0F))};
    }
    return {.clip = stance ? clips::kStanceStepRight : (dash ? clips::kDashRight : clips::kStepRight),
            .endFacing = wrapAngle(heading + (kPi / 2.0F))};
}

} // namespace coney::human
