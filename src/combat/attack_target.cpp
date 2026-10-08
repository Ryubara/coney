// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/attack_target.h"

namespace coney::combat {

namespace {

// The cross steps of the chain, which search again when not locked.
bool crossChainStep(int animId) {
    return animId == anim_id::kAttackSX2 || animId == anim_id::kAttackXX2 || animId == anim_id::kAttackSSX3;
}

// The moving attacks square starts outside the stance, which pick afresh.
bool movingAttack(int animId) {
    return animId == anim_id::kAttackFromWalk || animId == anim_id::kAttackFromRun ||
           animId == anim_id::kRunningAttackCharge || animId == anim_id::kRunningAttackDive;
}

} // namespace

AttackTarget attackTarget(int animId, bool cross, bool armed, bool chainStep, bool locked) {
    if (animId == anim_id::kSnapRight || animId == anim_id::kSnapLeft || animId == anim_id::kSnapBack) {
        return AttackTarget{.search = TargetSearch::Snap};
    }
    // The special searches at the far range of the charge (id 0).
    if (animId == anim_id::kSpecial || animId == anim_id::kSpecialRage || animId == anim_id::kSpecialRear ||
        animId == anim_id::kSpecialRageRear) {
        return AttackTarget{.search = TargetSearch::FindAttack, .farId = anim_id::kRunningAttackCharge};
    }
    if (chainStep) {
        if (crossChainStep(animId) && !locked) {
            return AttackTarget{.search = TargetSearch::FindAttack, .farId = animId};
        }
        return AttackTarget{.search = TargetSearch::KeepCurrent};
    }
    if (armed) {
        return AttackTarget{.range = kArmedPickRange};
    }
    // Cross always picks afresh; square's moving attacks too. (The charge takes no target and the dive searches its
    // own band; Fighter::steer() runs those, docs/research/combat.md#charge-aim.)
    if (cross || movingAttack(animId)) {
        return AttackTarget{};
    }
    return AttackTarget{.search = TargetSearch::KeepCurrent};
}

} // namespace coney::combat
