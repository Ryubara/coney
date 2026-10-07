// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/targeting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/perception.h"
#include "ai/route_planner.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// The dog's character class (the attackable exception).
constexpr int kDogClass = 221;
// The gang kind of the civilians the Warriors may hit only when chosen.
constexpr int kStreetGangKind = 0x17;
// After every this many enemies the walk stops once it has a winner.
constexpr std::size_t kPickBatch = 5;

// Whether `brain`'s human is knocked down (on the ground).
bool down(const Brain& brain) { return brain.human().state() == human::TargetState::Grounded; }

// Whether `brain`'s human holds a weapon (an anim set from what it holds).
bool armed(const Brain& brain) { return brain.human().fighter().animSet() != 0; }

// The plan distance between two brains' humans.
float planDistance(const Brain& a, const Brain& b) { return a.distanceTo(b); }

} // namespace

float sightRangeOf(const Brain& brain) {
    const Gang* gang = brain.gang();
    if (gang != nullptr && gang->kind() == kPoliceGangKind) {
        return std::max(brain.sightRange(), kPoliceSightRange);
    }
    return brain.sightRange();
}

bool validEnemy(const Brain& scorer, const Brain& human) {
    const human::Human& them = human.human();
    // Dead or out of the fight.
    if (them.outOfWorld() || them.fighter().health().depleted() || them.script().knockedOut) {
        return false;
    }
    if (!them.targetable()) {
        return false;
    }
    // Under arrest: only for the player's and the Warriors' brains.
    if (them.script().arrested) {
        return scorer.type() == BrainType::Player || scorer.type() == BrainType::Warrior;
    }
    return true;
}

bool canBeChased(const Brain& target) { return target.senses().reachable; }

bool attackableBy(const Brain& target, const Brain* attacker) {
    if (attacker != nullptr) {
        // A street civilian a Warrior goes for, who is not after that Warrior and has no target or an AI one: only
        // the Warrior gang's chosen target.
        const Gang* gang = target.gang();
        const Brain* theirs = target.target();
        if (target.type() == BrainType::Civilian && gang != nullptr && gang->kind() == kStreetGangKind &&
            attacker->type() == BrainType::Warrior && theirs != attacker &&
            (theirs == nullptr || theirs->type() != BrainType::Player)) {
            return attacker->gang() != nullptr && attacker->gang()->chosenTarget() == &target;
        }
        // The dog stays attackable.
        if (!target.attackable() && target.characterClass() == kDogClass) {
            return true;
        }
    }
    return target.attackable();
}

bool canTakeSlotOn(const Brain& scorer, const Brain& enemy) {
    const std::vector<Brain*>& slots = enemy.attackSlots();
    if (slots.size() < enemy.attackSlotCount() || std::ranges::find(slots, &scorer) != slots.end()) {
        return true;
    }
    const float mine = planDistance(scorer, enemy);
    return std::ranges::any_of(slots, [&](const Brain* holder) { return planDistance(*holder, enemy) > mine; });
}

float scoreEnemy(const Brain& scorer, const Brain& enemy, std::uint32_t flags) {
    using namespace score_term;
    if (!canTakeSlotOn(scorer, enemy)) {
        return kRuledOut;
    }
    const TargetingPoints& points = scorer.settings().targeting;
    const human::Human& them = enemy.human();
    const float range = sightRangeOf(scorer);
    const float distance = anim::distance(scorer.human().position(), them.position());
    float score = 0.0F;
    // The base: one the scorer cannot chase.
    if (!canBeChased(enemy) || !scorer.mayApproach()) {
        score += static_cast<float>(points.cannotChase);
    }
    if ((flags & kDistance) != 0) {
        if (distance > range) {
            return kRuledOut;
        }
        score += points.perMetre * (range - distance);
    }
    if ((flags & kNearLeader) != 0 && scorer.gang() != nullptr) {
        if (const Brain* leader = scorer.gang()->leader(); leader != nullptr && leader != &enemy) {
            const float fromLeader = anim::distance(leader->human().position(), them.position());
            if (fromLeader <= range) {
                score += points.leaderPerMetre * (range - fromLeader);
            }
        }
    }
    if ((flags & kEnemyLeads) != 0 && enemy.gang() != nullptr && enemy.gang()->leader() == &enemy &&
        scorer.type() != BrainType::Warrior) {
        score += static_cast<float>(points.enemyLeads);
    }
    const bool isDown = down(enemy);
    if ((flags & kStunned) != 0 && them.fighter().victim().stunned() && !isDown) {
        score += static_cast<float>(points.stunned);
    }
    if ((flags & kOutOfView) != 0 && !inFieldOfView(scorer.fieldOfView(), scorer.human(), them.position())) {
        score += static_cast<float>(points.outOfView);
    }
    if ((flags & kDown) != 0 && isDown) {
        score += static_cast<float>(points.down);
    }
    if ((flags & kGrabbed) != 0 && them.fighter().grabbed()) {
        score += static_cast<float>(points.grabbed);
    }
    if ((flags & kGrabbedFromRear) != 0 && them.fighter().grabbedFromRear()) {
        score += static_cast<float>(points.grabbedFromRear);
    }
    const bool targetsMe = enemy.target() == &scorer;
    if ((flags & kTargetsMe) != 0 && targetsMe) {
        score += static_cast<float>(points.targetsMe);
    }
    if ((flags & kRunning) != 0 && them.gait() == human::Gait::Run && them.animator().flags() == 0) {
        score += static_cast<float>(points.running);
    }
    if ((flags & kTargetsMeArmed) != 0 && targetsMe && armed(enemy)) {
        score += static_cast<float>(points.targetsMeArmed);
    }
    if ((flags & kPlayer) != 0 && enemy.type() == BrainType::Player && !enemy.dead()) {
        score += static_cast<float>(points.player);
    }
    if ((flags & kArrested) != 0 && them.script().arrested) {
        score += static_cast<float>(points.arrested);
    }
    if ((flags & kPolice) != 0 && enemy.type() == BrainType::Cop) {
        score += static_cast<float>(points.police);
    }
    // The terms that always count.
    if (const RoutePlanner* planner = scorer.planner();
        planner != nullptr && !planner->lineClear(scorer.human().position(), them.position())) {
        score += static_cast<float>(points.noWalkableLine);
    }
    if (!attackableBy(enemy, nullptr)) {
        score += static_cast<float>(points.notAttackable);
    }
    if (scorer.gang() != nullptr && scorer.gang()->chosenTarget() == &enemy) {
        score += static_cast<float>(points.gangTarget);
    }
    return score;
}

float Goal::adjustEnemyScore(const Brain& scorer, const Brain& candidate, const Brain* previous, float score) const {
    if (&candidate == previous) {
        score += static_cast<float>(scorer.settings().targeting.previousTarget);
    }
    return score;
}

Brain* pickBestEnemy(Brain& brain, const Goal* goal, std::uint32_t flags) {
    const Brain* previous = brain.target();
    brain.setTarget(nullptr);
    Brain* best = nullptr;
    float bestScore = 0.0F;
    // A copy: setting the target below does not change the list, but a scored human's brain is not touched either.
    const std::vector<Brain*> enemies = brain.enemies();
    for (std::size_t index = 0; index < enemies.size(); ++index) {
        if (index > 0 && index % kPickBatch == 0 && best != nullptr) {
            break;
        }
        Brain& enemy = *enemies[index];
        if (!validEnemy(brain, enemy)) {
            continue;
        }
        float score = scoreEnemy(brain, enemy, flags);
        score =
            goal != nullptr
                ? goal->adjustEnemyScore(brain, enemy, previous, score)
                : (&enemy == previous ? score + static_cast<float>(brain.settings().targeting.previousTarget) : score);
        if (score > 0.0F && score > bestScore) {
            bestScore = score;
            best = &enemy;
        }
    }
    brain.setTarget(best);
    return best;
}

} // namespace coney::ai
