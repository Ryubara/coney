// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/body.h"

namespace coney::human {

BodyTuning& bodyTuning() {
    static BodyTuning tuning;
    return tuning;
}

float walkingRadius(float scale) { return bodyTuning().radius * scale; }

float walkingCentreHeight(float scale) { return walkingRadius(scale) + bodyTuning().footGap; }

float playerWalkingRadius(float scale) { return walkingRadius(scale * bodyTuning().playerFactor) * scale; }

float playerWalkingCentreHeight(float scale) { return playerWalkingRadius(scale) + bodyTuning().footGap; }

float aiWalkingRadius(float scale) { return walkingRadius(scale) * scale; }

} // namespace coney::human
