// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_goal.h"

#include <algorithm>
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
#include "ai/sectors.h"
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
// The walk into reach holds A at least this beyond twice T's radius, m.
constexpr float kMoveBandSlack = 0.1F;
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
// The grab's moves in: to the grab reach (1500 ms), and a refused grab's step back to 0.8 × near (+ 1 m, 1000 ms).
constexpr std::uint32_t kGrabMoveMs = 1500;
constexpr std::uint32_t kGrabRefusedMoveMs = 1000;
constexpr float kGrabRefusedShare = 0.8F;
constexpr float kGrabRefusedBand = 1.0F;
// The rear grab's chance per CfgGang value 8, percent.
constexpr int kRearGrabPercent = 25;
// Half a quarter turn, radians.
constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0F;

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
bool canStart(Brain& brain, const Brain& target, int kind) {
    return canStartAttack(startGuardOf(brain, target, kind), attackerViewOf(brain, &target),
                          targetViewOf(target, brain), kind);
}

// Whether `brain` may use `kind` on `target` (`Human_CanUseAttackKind`).
bool canUse(Brain& brain, const Brain& target, int kind) {
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

// The snap's stick from the sector snapSectorOf() chose: behind him (3-5) straight back, at a side (2, 6) a quarter
// turn toward that side. **Coney reading**: the page gives the stick in the original's clockwise stick angle (heading −
// k × 45°); Coney's stick is a world heading, which grows to the left, so sector 2 (his left) is heading + 90°.
std::optional<float> snapHeading(Brain& brain) {
    const std::optional<int> sector = snapSectorOf(brain);
    if (!sector.has_value()) {
        return std::nullopt;
    }
    const float heading = brain.human().heading();
    if (*sector >= 3 && *sector <= 5) {
        return human::wrapAngle(heading + std::numbers::pi_v<float>);
    }
    return human::wrapAngle(heading + (*sector == 2 ? kQuarterTurn : -kQuarterTurn));
}

// The fight goal's re-target (`0x002b3c30`): the nearest human in sector 0 of A's record (its first word; inferred to
// be the nearest human) becomes the target when he is not already it, is a threat and no GrabTarget goal is on the
// stack. **Coney stand-in**: a threat is a valid enemy on A's enemy list (`Brain_IsThreat`'s gang test is not built).
void retargetFromSectors(Brain& brain) {
    const Brain* ahead = brain.sectors(kSectorAgeMs)[0].nearest;
    if (ahead == nullptr || ahead == brain.target() || brain.findGoal(GoalType::GrabTarget) != nullptr) {
        return;
    }
    const std::vector<Brain*>& enemies = brain.enemies();
    const auto listed = std::ranges::find(enemies, ahead);
    if (listed != enemies.end() && Brain::fightable(**listed) && validEnemy(brain, **listed)) {
        brain.setTarget(*listed);
    }
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
        retargetFromSectors(brain);
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
    // Out of the kind's reach (3D, feet to feet): move in, to between 2 × his radius and max(reach, that + 0.1 m).
    const float reach = kindReach(brain, *target, m_kind);
    if (reachDistance(brain, *target) > reach) {
        const int limit = brain.characterClass() == kBossClass ? kShortMoveMs : kLongMoveMs;
        const float stop = std::max(reach, 2.0F * capsuleRadius(*target) + kMoveBandSlack);
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
    // A's sector round T: 2 or 6 is at his side.
    const int side = sectorOf(target.human(), brain.human().position());
    const bool atSide = side == 2 || side == 6;
    const bool rearGrabbed = grabbedFromRear(target);
    const AttackWeights& weights = brain.attackWeights();
    if (m_kind == kGrabKind) {
        // At his side: step in while a mate holds him from behind, else an X1.
        if (atSide && rearGrabbed) {
            const float stop =
                std::max(kindReach(brain, target, kGrabKind), 2.0F * capsuleRadius(target) + kMoveBandSlack);
            brain.queueAction(std::make_unique<MoveToHumanAction>(kGrabMoveMs, stop));
            return true;
        }
        if (atSide) {
            m_kind = kX1Kind;
        }
    } else {
        // A rear grab: empty-handed, the near human in his sector 4 (straight behind), when the gang's chance comes up.
        const int g = brain.gangFight().rearGrab;
        if (brain.human().fighter().animSet() == 0 && g > 0 && weights[static_cast<std::size_t>(kGrabKind)] > 0 &&
            nearestIn(target, brain, {4}) && brain.rand100() < g * kRearGrabPercent &&
            canStart(brain, target, kGrabKind)) {
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

} // namespace coney::ai
