// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_goal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "ai/attack_action.h"
#include "ai/attack_kinds.h"
#include "ai/attack_views.h"
#include "ai/block_goal.h"
#include "ai/brain.h"
#include "ai/fight_checks.h"
#include "ai/move_to_human_action.h"
#include "ai/route_planner.h"
#include "ai/targeting.h"
#include "combat/player_combat.h"
#include "combat/reactions.h"
#include "human/fighter.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The re-target's period, ms.
constexpr std::uint64_t kRetargetMs = 1000;
// The walkable-line check's period, in updates.
constexpr std::uint32_t kLineCheckUpdates = 30;
// The move into reach stops this share of the reach inside it, so the next update finds the target in reach.
constexpr float kMoveStopShare = 0.9F;
// The kinds the try and the pick name.
constexpr int kX1Kind = 0;
constexpr int kSquareKind = 1;
constexpr int kSnapKind = 10;
constexpr int kChargeKind = 19;
constexpr int kDiveKind = 20;
constexpr int kTackleKind = 21;
constexpr int kGrabKind = 22;
// A cop tackles with X1 this often, percent.
constexpr int kCopX1Percent = 75;
// The class whose moves into reach are short (`+0x11b` 13: the bosses).
constexpr int kBossClass = 13;
// The snap's human stands within this of A.
constexpr float kSnapRange = 2.5F;
// The grab's moves in: to the grab reach (1500 ms), and a refused grab's step back to 0.8 × near (+ 1 m, 1000 ms).
constexpr std::uint32_t kGrabMoveMs = 1500;
constexpr std::uint32_t kGrabRefusedMoveMs = 1000;
constexpr float kGrabRefusedShare = 0.8F;
constexpr float kGrabRefusedBand = 1.0F;
// The rear grab's chance per CfgGang value 8, percent.
constexpr int kRearGrabPercent = 25;
// Half a quarter turn, radians.
constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0F;
// Degrees to radians.
constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;

// Whether `target` is down, out of the fight or arrested (state any of `0xe0000`).
bool downOrOut(const Brain& target) {
    const human::Human& t = target.human();
    return t.fighter().victim().grounded() || t.fighter().health().depleted() ||
           t.state() == human::TargetState::Grounded;
}

// Whether `target`'s own target is `brain` (its brain's, or its fighter's for a player).
bool targetsBack(const Brain& target, const Brain& brain) {
    return target.target() == &brain || target.human().fighter().target() == &brain.human();
}

// Whether `brain` takes or has one of `target`'s active-attacker places (`Brain_ClaimActiveAttacker`).
bool claimPlace(Brain& brain, Brain& target) {
    FightBook& book = target.fightBook();
    return book.places.claim(&brain, book.spacing.inUse(downOrOut(target)), brain.nowMs() >= target.attackableAtMs(),
                             targetsBack(target, brain));
}

// Whether `brain` may start `kind` on `target` now (`Human_CanStartAttack`).
bool canStart(const Brain& brain, const Brain& target, int kind) {
    return canStartAttack(startGuardOf(brain, target, kind), attackerViewOf(brain, &target),
                          targetViewOf(target, brain), kind);
}

// Whether `brain` may use `kind` on `target` (`Human_CanUseAttackKind`).
bool canUse(const Brain& brain, const Brain& target, int kind) {
    return canUseAttackKind(attackerViewOf(brain, &target), targetViewOf(target, brain), kind);
}

// Row 7 of Brain_CheckAttack: a Warrior holds back from a boss (class 13) not targeting him while a player in the
// boss's slot list who is tackling, throwing or front-grabbing stands within 0.75 × near of him.
bool bossHeldByPlayer(const Brain& brain, const Brain& target) {
    if (brain.type() != BrainType::Warrior || target.characterClass() != kBossClass || targetsBack(target, brain)) {
        return false;
    }
    constexpr float kBossHoldShare = 0.75F;
    return std::ranges::any_of(target.attackSlots(), [&](const Brain* holder) {
        if (holder->type() != BrainType::Player) {
            return false;
        }
        const human::Fighter& fighter = holder->human().fighter();
        const combat::CombatMode mode = fighter.combat().mode();
        const bool holds =
            mode == combat::CombatMode::Tackling || (mode == combat::CombatMode::Grabbing && !fighter.fromRear());
        return holds && holder->distanceTo(target) <= kBossHoldShare * brain.meleeNear();
    });
}

// Row 5 of Brain_CheckAttack, as far as Coney keeps it: for a non-cop, the target's own target is a cop.
bool policeHaveHim(const Brain& brain, const Brain& target) {
    const Brain* theirs = target.target();
    return brain.type() != BrainType::Cop && theirs != nullptr && theirs != &brain && theirs->type() == BrainType::Cop;
}

// The snap's stick: the first of A's attackers within 2.5 m behind him (sectors 3-5), then on either side (6, 2),
// gives the stick's heading (behind: heading + π; a side: a quarter turn toward him).
std::optional<float> snapHeading(const Brain& brain) {
    const human::Human& a = brain.human();
    std::optional<float> side;
    for (const Brain* other : brain.attackSlots()) {
        const anim::Vec3 to = anim::subtract(other->human().position(), a.position());
        const float distance = std::hypot(to.x, to.y);
        if (distance > kSnapRange || distance < 1e-4F) {
            continue;
        }
        const float rel = human::wrapAngle(human::headingOf(to) - a.heading());
        // Sectors 3-5 lie more than 112.5° off the facing; 2 and 6 between 67.5° and 112.5°.
        if (std::fabs(rel) > 112.5F * kDegrees) {
            return human::wrapAngle(a.heading() + std::numbers::pi_v<float>);
        }
        if (!side.has_value() && std::fabs(rel) > 67.5F * kDegrees) {
            side = human::wrapAngle(a.heading() + (rel > 0.0F ? kQuarterTurn : -kQuarterTurn));
        }
    }
    return side;
}

} // namespace

void FightGoal::start(Brain& brain) {
    m_deadlineMs = m_durationMs < 0 ? brain.nowMs() : brain.nowMs() + static_cast<std::uint64_t>(m_durationMs);
    m_retargetAtMs = brain.nowMs() + kRetargetMs;
    m_kind = kNoAttackKind;
    m_angle.reset();
}

void FightGoal::suspend(Brain& brain) { releasePlace(brain); }

void FightGoal::end(Brain& brain) { releasePlace(brain); }

void FightGoal::releasePlace(Brain& brain) {
    if (m_place && brain.target() != nullptr) {
        brain.target()->fightBook().places.release(&brain);
    }
    m_place = false;
}

GoalStatus FightGoal::process(Brain& brain) {
    ++m_updates;
    // 1. A valid enemy, and a slot on him.
    Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target) || !validEnemy(brain, *target) || !brain.hasAttackSlot()) {
        return GoalStatus::Done;
    }
    // 2. The tackle try.
    if (tryTackle(brain, *target)) {
        return GoalStatus::Stop;
    }
    // 3. Within the far melee range × 1.1 (any distance with a throwable in hand: never, in Coney).
    if (brain.distanceTo(*target) > brain.meleeFar() * kFightRangeScale) {
        return GoalStatus::Done;
    }
    // 4. Every 30 updates, a walkable straight line to him.
    if (m_updates % kLineCheckUpdates == 0 && brain.planner() != nullptr &&
        !brain.planner()->lineClear(brain.human().position(), target->human().position())) {
        return GoalStatus::Done;
    }
    // 5. Once a second, the re-target.
    if (brain.nowMs() >= m_retargetAtMs) {
        m_retargetAtMs = brain.nowMs() + kRetargetMs;
        Brain* before = target;
        brain.retarget();
        target = brain.target();
        if (target == nullptr || !brain.hasAttackSlot()) {
            return GoalStatus::Done;
        }
        if (target != before) {
            m_place = false; // the old target's slot list gave it up with the slot
            m_kind = kNoAttackKind;
        }
    }
    // 6. The block try: the block goal is then the top, processed at once.
    if (tryBlock(brain)) {
        releasePlace(brain);
        return GoalStatus::Again;
    }
    // 7. While actions are queued, wait; once the attack is over, give up the place.
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    releasePlace(brain);
    // 8. The deadline.
    if (brain.nowMs() >= m_deadlineMs && !brain.hasAttackSlot()) {
        return GoalStatus::Done;
    }
    // 9. The kind.
    if (m_kind == kNoAttackKind) {
        m_kind = pickAttackFor(brain, *target, PickFilter::CanUse).value_or(kNoAttackKind);
        m_angle.reset();
    }
    // 10. May A attack now?
    CheckAttackInput input;
    input.cooledDown = brain.nowMs() >= brain.nextAttackMs();
    input.kindChosen = m_kind != kNoAttackKind;
    input.attackable = attackableBy(*target, &brain);
    input.chargeOutOfStance = m_kind == kChargeKind || m_kind == kDiveKind;
    input.policeHaveHim = policeHaveHim(brain, *target);
    input.bossHeldByPlayer = bossHeldByPlayer(brain, *target);
    input.grabKind = m_kind == kGrabKind;
    input.behindTarget = behind(brain, *target);
    const int answer = checkAttack(input, [&] {
        m_place = claimPlace(brain, *target);
        return m_place;
    });
    const float distance = brain.distanceTo(*target);
    if (answer != check::kGo) {
        // The reposition: hold a ring round him.
        RepositionInput ring;
        ring.reason = answer;
        ring.fewSlotHolders = target->attackSlots().size() < 2;
        ring.targetFacesMe = targetsBack(*target, brain);
        ring.firstOnFreeTarget =
            target->target() == nullptr && !target->attackSlots().empty() && target->attackSlots().front() == &brain;
        ring.cop = brain.type() == BrainType::Cop;
        ring.targetRadius = capsuleRadius(*target);
        ring.nearRange = brain.meleeNear();
        ring.farRange = brain.meleeFar();
        const RepositionRing band = repositionRing(ring);
        if (distance > band.outer) {
            brain.queueAction(std::make_unique<MoveToHumanAction>(band.limitMs, band.outer));
        }
        return GoalStatus::Stop;
    }
    // Out of the kind's reach: move in.
    const float reach = kindReach(brain, m_kind);
    if (distance > reach) {
        const int limit = brain.characterClass() == kBossClass ? kShortMoveMs : kLongMoveMs;
        const float stop = std::max(reach * kMoveStopShare, 2.0F * capsuleRadius(*target));
        brain.queueAction(std::make_unique<MoveToHumanAction>(static_cast<std::uint32_t>(limit), stop));
        return GoalStatus::Stop;
    }
    // In reach: the grab and snap try.
    if (tryGrab(brain, *target)) {
        return GoalStatus::Stop;
    }
    // The start test: a refused grab steps back to 0.8 × near; then another kind that can start.
    if (!canStart(brain, *target, m_kind)) {
        if (m_kind == kGrabKind) {
            const float stop = kGrabRefusedShare * brain.meleeNear() + kGrabRefusedBand;
            brain.queueAction(std::make_unique<MoveToHumanAction>(kGrabRefusedMoveMs, stop));
        }
        m_kind = pickAttackFor(brain, *target, PickFilter::CanStart).value_or(kNoAttackKind);
        m_angle.reset();
        return GoalStatus::Stop;
    }
    // The press.
    queueAttack(brain, m_kind, m_angle);
    m_kind = kNoAttackKind;
    m_angle.reset();
    return GoalStatus::Stop;
}

bool FightGoal::tryTackle(Brain& brain, Brain& target) {
    TackleMeter& meter = brain.fightBook().tackle;
    const int k = brain.gangFight().tackle;
    if (k == 0 || brain.attackWeights()[static_cast<std::size_t>(kTackleKind)] == 0) {
        meter.clear();
        return false;
    }
    if (!meter.ready(k)) {
        return false;
    }
    const int kind = brain.type() == BrainType::Cop && brain.rand100() < kCopX1Percent ? kX1Kind : kTackleKind;
    if (!canStart(brain, target, kind) || !attackableBy(target, &brain) || policeHaveHim(brain, target) ||
        !claimPlace(brain, target)) {
        return false;
    }
    queueAttack(brain, kind);
    meter.clear();
    m_place = true;
    return true;
}

bool FightGoal::tryGrab(Brain& brain, Brain& target) {
    // The snap: square with the stick to the side or back.
    if (m_kind == kSnapKind) {
        if (const std::optional<float> heading = snapHeading(brain); heading.has_value()) {
            m_kind = kSquareKind;
            m_angle = heading;
        }
        return false;
    }
    const human::Human& t = target.human();
    const combat::Side side = combat::victimSide(t.position(), t.heading(), brain.human().position());
    const bool atSide = side == combat::Side::Left || side == combat::Side::Right;
    const bool rearGrabbed = grabbedFromRear(target);
    const AttackWeights& weights = brain.attackWeights();
    if (m_kind == kGrabKind) {
        // At his side: step in while a mate holds him from behind, else an X1.
        if (atSide && rearGrabbed) {
            const float stop = std::max(kindReach(brain, kGrabKind) * kMoveStopShare, 2.0F * capsuleRadius(target));
            brain.queueAction(std::make_unique<MoveToHumanAction>(kGrabMoveMs, stop));
            return true;
        }
        if (atSide) {
            m_kind = kX1Kind;
        }
    } else {
        // A rear grab: empty-handed, behind him, when the gang's chance comes up.
        const int g = brain.gangFight().rearGrab;
        if (brain.human().fighter().animSet() == 0 && g > 0 && weights[static_cast<std::size_t>(kGrabKind)] > 0 &&
            behind(brain, target) && brain.rand100() < g * kRearGrabPercent && canStart(brain, target, kGrabKind)) {
            m_kind = kGrabKind;
        }
    }
    // A man held from behind by a mate is grabbed in front.
    if (rearGrabbed && m_kind != kGrabKind && weights[static_cast<std::size_t>(kGrabKind)] > 0 &&
        canUse(brain, target, kGrabKind)) {
        m_kind = kGrabKind;
        return true;
    }
    return false;
}

void queueAttack(Brain& brain, int kind, std::optional<float> stickHeading) {
    const std::vector<int> chain = chainOf(kind);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const int delay = i == 0 ? 0 : chainDelayMs(chain[i - 1], brain.human().animator().anims());
        brain.queueAction(std::make_unique<AttackAction>(chain[i], static_cast<std::int16_t>(delay),
                                                         i == 0 ? stickHeading : std::nullopt));
    }
}

float attackReach(const human::Human& human, int kind) {
    const combat::AnimRangeList* ranges = human.ranges();
    const float far = ranges != nullptr ? ranges->farRange(firstAnimOf(kind)) : 0.0F;
    return kInReachShare * (far > 0.0F ? far : human::kDefaultStrikeReach);
}

} // namespace coney::ai
