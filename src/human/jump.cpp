// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/jump.h"

#include <numbers>

namespace coney::human {

JumpTuning& jumpTuning() {
    static JumpTuning tuning;
    return tuning;
}

bool jumpAllowed(float speed, const Speeds& speeds, bool climbableAhead) {
    // Near a climbable wall triangle climbs or does nothing; too slow, or below a jog, does nothing.
    if (climbableAhead || speed <= jumpTuning().minSpeed) {
        return false;
    }
    const Gait reached = gaitForSpeed(speed, speeds);
    return reached == Gait::Jog || reached == Gait::Run || reached == Gait::Sprint;
}

float launchSpeed(Gait takeOffGait, const Speeds& speeds) {
    switch (takeOffGait) {
    case Gait::Sprint:
        return speeds.sprint;
    case Gait::Jog:
    case Gait::Run:
        return speeds.run;
    case Gait::Standing:
    case Gait::Sneak:
    case Gait::Walk:
        break;
    }
    return speeds.jog;
}

float airTurnLimit() { return jumpTuning().airTurnDegrees * std::numbers::pi_v<float> / 180.0F; }

} // namespace coney::human
