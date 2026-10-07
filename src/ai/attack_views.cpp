// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_views.h"

#include <cmath>
#include <numbers>

#include "ai/attack_action.h"
#include "ai/attack_places.h"
#include "ai/brain.h"
#include "ai/fight_goal.h"
#include "ai/reaction_goals.h"
#include "ai/targeting.h"
#include "combat/attacks.h"
#include "combat/combat_tuning.h"
#include "combat/grab.h"
#include "combat/reactions.h"
#include "human/body.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The snap's human stands within this of A, and beyond this angle off his facing (sectors 2-6), radians.
constexpr float kSnapRange = 2.5F;
constexpr float kSnapAngle = 67.5F * std::numbers::pi_v<float> / 180.0F;
// Below this speed short of the run speed a human is still at the run speed (`speed >= run - 0.01`).
constexpr float kRunSpeedSlack = 0.01F;
// The grab needs a fifth of the power meter.
constexpr int kGrabPowerShare = 5;

// Whether `human` is free: on its feet, in no pair, not lying, stunned or reacting.
bool freeHuman(const human::Human& human) {
    const human::Fighter& fighter = human.fighter();
    return human.state() == human::TargetState::Standing && !fighter.inPair() && !fighter.victim().grounded() &&
           !fighter.victim().stunned() && !fighter.helpless(human.animator());
}

// Whether `human` is down, out of the fight or arrested (state any of `0xe0000`).
bool downOrOut(const human::Human& human) {
    return human.fighter().victim().grounded() || human.fighter().health().depleted() ||
           human.state() == human::TargetState::Grounded;
}

} // namespace

AttackerView attackerViewOf(const Brain& attacker, const Brain* target) {
    const human::Human& a = attacker.human();
    const human::Fighter& fighter = a.fighter();
    const combat::CombatMode mode = fighter.combat().mode();
    AttackerView view;
    view.free = freeHuman(a);
    view.atRunSpeed = a.gait() >= human::Gait::Run && a.speed() >= a.speeds().run - kRunSpeedSlack;
    view.padControlled = attacker.type() == BrainType::Player;
    view.grabPower = fighter.combat().power().value() * kGrabPowerShare >= fighter.combat().power().maximum();
    view.grabbing = mode == combat::CombatMode::Grabbing || mode == combat::CombatMode::Mugging;
    view.tackling = mode == combat::CombatMode::Tackling;
    view.grabbed = fighter.grabbed() || fighter.holdState() == human::TargetState::Held;
    view.grabbedFromRear = grabbedFromRear(attacker);
    view.tackled = fighter.holdState() == human::TargetState::Mounted;
    view.knockedDown = fighter.victim().grounded();
    const Goal* reaction = attacker.reactionGoal();
    view.groundedGoal = reaction != nullptr && reaction->type() == GoalType::ReactKnockedDown;
    view.holdMovesFree = (a.animator().flags() & kAttackWaitFlags) == 0;
    view.characterClass = attacker.characterClass();
    if (target != nullptr) {
        const human::Human& t = target->human();
        view.holdsTarget = fighter.held() == &t;
        view.heldByTarget = t.fighter().held() == &a;
        // The snap's human: the target, close and beside or behind.
        const anim::Vec3 to = anim::subtract(t.position(), a.position());
        const float distance = std::hypot(to.x, to.y);
        view.snapTargetAside = attacker.hasAttackSlot() && distance <= kSnapRange && distance > 1e-4F &&
                               std::fabs(human::wrapAngle(human::headingOf(to) - a.heading())) > kSnapAngle;
    }
    return view;
}

TargetView targetViewOf(const Brain& target, const Brain& attacker) {
    const human::Human& t = target.human();
    TargetView view;
    view.isAttacker = &target == &attacker;
    view.downOrOut = downOrOut(t);
    view.knockedDown = t.fighter().victim().grounded();
    view.notDown = !view.knockedDown && t.state() != human::TargetState::Grounded;
    view.ungrabbable = t.hasFlag(human::flag::kUngrabbable);
    view.free = freeHuman(t);
    view.specialRefused = !view.free;
    view.grabRefused = !view.free;
    return view;
}

PickContext pickContextOf(const Brain& attacker, const Brain& target) {
    const human::Human& a = attacker.human();
    PickContext context;
    context.targetSlotsTaken = static_cast<int>(target.attackSlots().size());
    context.ownSlotsTaken = static_cast<int>(attacker.attackSlots().size());
    context.running = a.gait() >= human::Gait::Run && a.animator().flags() == 0;
    context.armed = a.fighter().animSet() != 0;
    context.targetChasable = canBeChased(target) && attacker.mayApproach();
    context.swingsObject = false;
    context.hurtAndAttackable = a.fighter().hurt() && attackableBy(attacker, nullptr);
    context.targetRearGrabbed = grabbedFromRear(target);
    return context;
}

float kindReach(const Brain& attacker, int kind) {
    const combat::AnimRangeList* ranges = attacker.human().ranges();
    if ((kind == 21 || kind == 22) && ranges != nullptr) {
        return combat::grabSearchRange(*ranges, kind == 21 ? combat::GrabKind::Tackle : combat::GrabKind::Grab,
                                       combat::combatTuning().grabSearchScale);
    }
    return attackReach(attacker.human(), kind);
}

StartGuard startGuardOf(const Brain& attacker, const Brain& target, int kind) {
    StartGuard guard;
    guard.attackerHeldBusy = (attacker.human().animator().flags() & kAttackWaitFlags) != 0;
    guard.damagePending = attacker.human().fighter().victim().pending();
    guard.inReach = attacker.distanceTo(target) <= kindReach(attacker, kind);
    return guard;
}

bool dispatchable(int kind) {
    switch (kind) {
    case 8:
    case 9:
    case 11:
    case 12:
    case 13:
    case 15:
    case 18:
    case 23:
    case 34:
    case 36:
    case 41:
    case 44:
        return false;
    default:
        return kind >= 0 && kind < kNoAttackKind;
    }
}

std::optional<int> pickAttackFor(Brain& attacker, const Brain& target, PickFilter filter) {
    return pickAttackFor(attacker, target, filter, attacker.attackWeights());
}

std::optional<int> pickAttackFor(Brain& attacker, const Brain& target, PickFilter filter,
                                 const AttackWeights& weights) {
    const AttackerView a = attackerViewOf(attacker, &target);
    const TargetView t = targetViewOf(target, attacker);
    PickContext context = pickContextOf(attacker, target);
    context.targetSlotsMax = static_cast<int>(target.attackSlotCount());
    context.ownSlotsMax = static_cast<int>(attacker.attackSlotCount());
    const auto test = [&](int kind) {
        if (!dispatchable(kind)) {
            return false;
        }
        if (filter == PickFilter::CanStart) {
            return canStartAttack(startGuardOf(attacker, target, kind), a, t, kind);
        }
        return canUseAttackKind(a, t, kind);
    };
    return pickAttackKind(weights, context, test, attacker.random());
}

const Brain* rearGrabberOf(const Brain& target) {
    for (const Brain* holder : target.attackSlots()) {
        const human::Fighter& fighter = holder->human().fighter();
        if (fighter.held() == &target.human() && fighter.fromRear()) {
            return holder;
        }
    }
    return nullptr;
}

bool grabbedFromRear(const Brain& target) {
    return target.human().fighter().grabbedFromRear() || rearGrabberOf(target) != nullptr;
}

bool behind(const Brain& attacker, const Brain& target) {
    const human::Human& t = target.human();
    return combat::victimSide(t.position(), t.heading(), attacker.human().position()) == combat::Side::Rear;
}

float capsuleRadius(const Brain& brain) { return human::bodyTuning().radius * brain.human().scale(); }

void thinkTackle(Brain& brain) {
    const Brain* target = brain.target();
    if (target == nullptr) {
        return;
    }
    // Pressing: he moves, and either holds a weapon or moves away from the thinker's facing.
    const human::Human& t = target->human();
    const anim::Vec3 velocity = t.velocity();
    const anim::Vec3 facing = human::facing(brain.human().heading());
    const bool moving = t.speed() > 0.0F;
    const bool away = velocity.x * facing.x + velocity.y * facing.y >= 0.0F;
    const bool armed = t.fighter().animSet() != 0;
    brain.fightBook().tackle.think(moving && (armed || away), static_cast<int>(t.gait()));
}

} // namespace coney::ai
