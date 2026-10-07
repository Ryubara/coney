// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/ai_counter.h"

#include "combat/reactions.h"

namespace coney::combat {

bool aiCounterAllowed(const AiCounterSide& side) {
    return !side.padControlled && side.standing && (side.phaseFlags & kAiCounterRefusingPhases) == 0 &&
           (side.phaseFlags & kAiCounterBusyPhases) == 0 && !side.holdingObject && !side.hurt;
}

int aiCounterFor(int attackerAnim) {
    if (attackerAnim >= anim_id::kGrabMiss && attackerAnim <= anim_id::kGrabPlayerIntro) {
        return kAiGrabCounter;
    }
    if (attackerAnim >= anim_id::kTackleMiss && attackerAnim <= anim_id::kTacklePlayerIntro) {
        return kAiTackleCounter;
    }
    return anim_id::kNone;
}

bool faceToFace(anim::Vec3 a, float headingA, anim::Vec3 b, float headingB) {
    return victimSide(a, headingA, b) == Side::Front && victimSide(b, headingB, a) == Side::Front;
}

} // namespace coney::combat
