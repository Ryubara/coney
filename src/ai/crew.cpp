// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/crew.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/engage_goals.h"
#include "ai/fight_goal.h"
#include "ai/formations.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/move_to_human_action.h"
#include "ai/script_services.h"
#include "ai/targeting.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The follow tactic's type id.
constexpr int kFollowTacticType = 0x12;
// The chief's formation takes this many slots (`0x00295dd8`).
constexpr int kFollowSlots = 9;
// The member events the follow tactic answers: 19 (its goal is given again in mode 1) and 22 with argument 1 (every
// member's goal is given again).
constexpr int kEventRegoal = 19;
constexpr int kEventGoalsAgain = 22;
constexpr int kRegoalMode = 1;
// The follow goal's mode: 3 turns to the leader's heading.
constexpr int kModeLeaderHeading = 3;
constexpr int kTacticMode = 3;
// Beyond this from its slot the formation walk runs (**Coney stand-in**), metres; the gaits.
constexpr float kRunBeyond = 3.0F;
constexpr int kWalkGait = 2;
constexpr int kRunGait = 4;

// Whether `brain` is an AI human's that can act: not a player's, fightable and not on the ground.
bool canAct(const Brain& brain) {
    return brain.type() != BrainType::Player && Brain::fightable(brain) &&
           brain.human().state() != human::TargetState::Grounded;
}

// The nearest attacker of a member of `gang` within `reach` of `from`; null for none.
Brain* nearestCrewAttacker(const Gang& gang, const Brain& from, float reach) {
    Brain* best = nullptr;
    float bestDistance = reach;
    for (const Brain* member : gang.members()) {
        for (Brain* attacker : member->attackSlots()) {
            if (attacker == nullptr || !Brain::fightable(*attacker)) {
                continue;
            }
            const float distance = from.distanceTo(*attacker);
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = attacker;
            }
        }
    }
    return best;
}

// The hold goal: its fight goal's limit, ms; the walk back's gait and how near it walks, metres; it turns to an enemy
// more than this off him (radians, 60°).
constexpr int kHoldFightMs = 4000;
constexpr int kHoldWalkGait = 2;
constexpr float kHoldBackReach = 0.5F;
constexpr float kHoldTurnAngle = 1.0471976F;
// The attack goal turns to the leader's heading every this long, ms.
constexpr std::uint64_t kAttackTurnMs = 4000;
// The hold tactic's events: 17 with no other human, 19, and 22 with argument 1 hold again.
constexpr int kEventHoldAgain = 17;
// The hold tactic's turn delay, ms: 0-1 s with enemies about, else 2-4 s.
constexpr int kHoldTurnSoonMaxMs = 1000;
constexpr int kHoldTurnLaterMinMs = 2000;
constexpr int kHoldTurnLaterMaxMs = 4000;

// The distance from `a` to `b` in plan.
float flatDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Whether any member of `gang` has a fightable enemy.
bool gangHasEnemy(const Gang& gang) {
    return std::ranges::any_of(gang.members(), [](const Brain* member) {
        return std::ranges::any_of(member->enemies(), [](const Brain* enemy) { return Brain::fightable(*enemy); });
    });
}

} // namespace

// ---- FollowPlayerGoal ----

void FollowPlayerGoal::start(Brain& brain) {
    Brain* leader = m_services->brain(m_leader);
    if (leader == nullptr) {
        return;
    }
    if (Formation* formation = m_formations->of(*leader, true); formation != nullptr) {
        formation->join(brain);
    }
}

bool FollowPlayerGoal::fightMode(const Brain& brain) const {
    const Gang* gang = brain.gang();
    if (gang == nullptr) {
        return false;
    }
    // Off while one of its own attackers is a player.
    if (std::ranges::any_of(brain.attackSlots(),
                            [](const Brain* attacker) { return attacker->type() == BrainType::Player; })) {
        return false;
    }
    // On when a member has attackers one of which holds this follower as an enemy (`+0x152`, inferred).
    return std::ranges::any_of(gang->members(), [&brain](const Brain* member) {
        return std::ranges::any_of(member->attackSlots(), [&brain](const Brain* attacker) {
            return Brain::fightable(*attacker) && std::ranges::contains(attacker->enemies(), &brain);
        });
    });
}

void FollowPlayerGoal::fight(Brain& brain, Brain& leader) {
    // A valid target, else the nearest attacker within 20 m, else the leader's.
    Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target)) {
        target = brain.gang() != nullptr ? nearestCrewAttacker(*brain.gang(), brain, kCrewAttackerReach) : nullptr;
        if (target == nullptr && leader.target() != nullptr && Brain::fightable(*leader.target())) {
            target = leader.target();
        }
        brain.setTarget(target);
    }
    if (target == nullptr) {
        return;
    }
    // Within the far melee range (the near one when the target is busy) it turns to him, else it moves to him.
    const float range = target->human().state() == human::TargetState::Standing ? brain.meleeFar() : brain.meleeNear();
    if (brain.distanceTo(*target) <= range) {
        brain.queueAction(TurnAction::toPoint(target->human().position()));
    } else {
        brain.queueAction(std::make_unique<MoveToHumanAction>(kCrewMoveMs, range));
    }
}

GoalStatus FollowPlayerGoal::process(Brain& brain) {
    Brain* leader = m_services->brain(m_leader);
    if (leader == nullptr || leader->human().outOfWorld()) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    if (fightMode(brain)) {
        fight(brain, *leader);
        return GoalStatus::Stop;
    }
    // Back to its slot every 31 updates.
    if (brain.updates() % kCrewSlotPeriod == 0) {
        if (brain.pushGoal(std::make_unique<FollowFormationGoal>(kCrewSlotReach))) {
            return GoalStatus::Again;
        }
    }
    // Mode 3: toward the leader's heading when more than 60° off it, every 2-4 s.
    if (m_mode == kModeLeaderHeading && brain.nowMs() >= m_nextTurnMs) {
        m_nextTurnMs =
            brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), kCrewTurnMinMs, kCrewTurnMaxMs));
        const float heading = leader->human().heading();
        if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kCrewHeadingTurn) {
            brain.queueAction(TurnAction::toHeading(heading));
        }
    }
    return GoalStatus::Stop;
}

void FollowPlayerGoal::end(Brain& brain) {
    if (Formation* formation = brain.following(); formation != nullptr) {
        formation->leave(brain);
    }
}

// ---- FollowFormationGoal ----

GoalStatus FollowFormationGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const Formation* formation = brain.following();
    const std::optional<anim::Vec3> slot = formation != nullptr ? formation->slotPoint(brain) : std::nullopt;
    if (!slot) {
        return GoalStatus::Done;
    }
    const anim::Vec3 position = brain.human().position();
    const float away = std::hypot(slot->x - position.x, slot->y - position.y);
    // Done once there, or once the walk it queued came back without getting there.
    if (away <= m_reach || (m_walked && brain.moveFailure() != MoveFailure::None)) {
        brain.setMoveFailure(MoveFailure::None);
        return GoalStatus::Done;
    }
    m_walked = true;
    brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = *slot,
                                                               .radius = m_reach,
                                                               .gait = away > kRunBeyond ? kRunGait : kWalkGait,
                                                               .option = true,
                                                               .delayMs = 0,
                                                               .flagKind = false}));
    return GoalStatus::Stop;
}

// ---- WarriorFollowTactic ----

WarriorFollowTactic::WarriorFollowTactic(double chief, ScriptServices& services, Formations& formations)
    : Tactic(kFollowTacticType, {}), m_chief(chief), m_services(&services), m_formations(&formations) {}

void WarriorFollowTactic::follow(Brain& member, int mode) {
    // Popped to the goal base, marked, then the goal on top: what the member had at the tactic's start stays under it.
    static_cast<void>(member.pushTacticGoal(
        std::make_unique<FollowPlayerGoal>(*m_services, *m_formations, m_chief, kCrewFollowDistance, mode)));
}

void WarriorFollowTactic::start(Gang& gang) {
    Brain* chief = m_services->brain(m_chief);
    if (chief == nullptr) {
        return;
    }
    if (Formation* formation = m_formations->of(*chief, true); formation != nullptr) {
        formation->setSlotCount(kFollowSlots, kFollowSlots, gang.owner().nowMs());
    }
    for (Brain* member : gang.members()) {
        if (member != chief && canAct(*member)) {
            follow(*member, kTacticMode);
        }
    }
}

int WarriorFollowTactic::update(Gang& gang) { return gang.leader() == nullptr ? 1 : 0; }

bool WarriorFollowTactic::event(Gang& gang, Brain& member, const BrainEvent& event) {
    if (event.id == kEventRegoal && member.handle() != m_chief && member.goalCount() > 0 && canAct(member)) {
        follow(member, kRegoalMode);
        return true;
    }
    if (event.id == kEventGoalsAgain && event.value == 1) {
        start(gang);
        return true;
    }
    return false;
}

// ---- FollowAndAttackGoal ----

void FollowAndAttackGoal::start(Brain& brain) {
    Brain* leader = m_services->brain(m_leader);
    if (leader == nullptr) {
        return;
    }
    if (Formation* formation = m_formations->of(*leader, true); formation != nullptr) {
        formation->join(brain);
    }
}

Brain* FollowAndAttackGoal::enemy(Brain& brain, Brain& leader) {
    // The crew's nearest attacker joins its enemies (stand-in), then the best of them near the leader.
    if (const Gang* gang = brain.gang(); gang != nullptr) {
        if (Brain* attacker = nearestCrewAttacker(*gang, leader, kCrewAttackReach); attacker != nullptr) {
            brain.addEnemy(*attacker);
        }
    }
    if (brain.enemies().empty()) {
        return nullptr;
    }
    Brain* best = pickBestEnemy(brain, this, score_term::kMelee);
    if (best != nullptr && leader.distanceTo(*best) > kCrewAttackReach) {
        brain.setTarget(nullptr);
        return nullptr;
    }
    return best;
}

GoalStatus FollowAndAttackGoal::process(Brain& brain) {
    Brain* leader = m_services->brain(m_leader);
    if (leader == nullptr || leader->human().outOfWorld()) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    if (Brain* target = enemy(brain, *leader); target != nullptr) {
        // A new target may be closed on again.
        if (target != m_lastTarget) {
            m_lastTarget = target;
            brain.setMayApproach(true);
        }
        if (brain.distanceTo(*target) <= brain.meleeFar() * kFightRangeScale) {
            brain.pushGoal(std::make_unique<FightGoal>(kHoldFightMs));
            return GoalStatus::Again;
        }
        if (brain.mayApproach()) {
            brain.pushGoal(std::make_unique<EngageEnemyGoal>());
            return GoalStatus::Again;
        }
        brain.queueAction(std::make_unique<MoveToHumanAction>(kCrewMoveMs, kInReachShare * brain.meleeFar()));
        return GoalStatus::Stop;
    }
    m_lastTarget = nullptr;
    // No enemy: near the leader it stays, turning to his heading every 4 s; farther, back to its slot.
    if (brain.distanceTo(*leader) > kCrewAttackStay) {
        if (brain.pushGoal(std::make_unique<FollowFormationGoal>(kCrewSlotReach))) {
            return GoalStatus::Again;
        }
        return GoalStatus::Stop;
    }
    if (brain.nowMs() >= m_nextTurnMs) {
        m_nextTurnMs = brain.nowMs() + kAttackTurnMs;
        const float heading = leader->human().heading();
        if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kCrewHeadingTurn) {
            brain.queueAction(TurnAction::toHeading(heading));
        }
    }
    return GoalStatus::Stop;
}

void FollowAndAttackGoal::end(Brain& brain) {
    if (Formation* formation = brain.following(); formation != nullptr) {
        formation->leave(brain);
    }
    brain.stopMove();
}

// ---- WarriorAttackTactic ----

WarriorAttackTactic::WarriorAttackTactic(double chief, ScriptServices& services, Formations& formations)
    : Tactic(kCrewAttack, {}), m_chief(chief), m_services(&services), m_formations(&formations) {}

void WarriorAttackTactic::start(Gang& gang) {
    Brain* chief = m_services->brain(m_chief);
    if (chief == nullptr) {
        return;
    }
    for (Brain* member : gang.members()) {
        if (member == chief || !canAct(*member)) {
            continue;
        }
        member->flush();
        member->pushGoal(std::make_unique<FollowAndAttackGoal>(*m_services, *m_formations, m_chief));
    }
}

// ---- HoldPositionGoal ----

GoalStatus HoldPositionGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const anim::Vec3 position = brain.human().position();
    const bool inside = flatDistance(position, m_point) <= m_radius;
    // The walk back: done at once within reach of the point, failing when it cannot get there.
    const auto walkBack = [&brain, this] {
        brain.queueAction(std::make_unique<MoveAction>(
            MoveRequest{.point = m_point, .radius = kHoldBackReach, .gait = kHoldWalkGait}));
    };
    Brain* enemy = brain.enemies().empty() ? nullptr : pickBestEnemy(brain, this, score_term::kMelee);
    if (enemy == nullptr) {
        if (!inside) {
            walkBack();
        }
        return GoalStatus::Stop;
    }
    const float off = std::fabs(human::wrapAngle(human::headingOf(anim::subtract(enemy->human().position(), position)) -
                                                 brain.human().heading()));
    const auto turnToEnemy = [&brain, enemy, off] {
        if (off > kHoldTurnAngle) {
            brain.queueAction(TurnAction::toPoint(enemy->human().position()));
        }
    };
    // Not after it: it only watches him.
    if (enemy->target() != &brain) {
        turnToEnemy();
        return GoalStatus::Stop;
    }
    // After it: fights from inside the radius (or when the way back failed), else goes back.
    if (inside || brain.moveFailure() != MoveFailure::None) {
        if (brain.distanceTo(*enemy) <= brain.meleeFar() * kFightRangeScale && brain.hasLineOfSight(*enemy)) {
            brain.setMoveFailure(MoveFailure::None);
            brain.pushGoal(std::make_unique<FightGoal>(kHoldFightMs));
            return GoalStatus::Again;
        }
        turnToEnemy();
        return GoalStatus::Stop;
    }
    walkBack();
    return GoalStatus::Stop;
}

void HoldPositionGoal::end(Brain& brain) { brain.stopMove(); }

// ---- WarriorHoldTactic ----

WarriorHoldTactic::WarriorHoldTactic(double chief, ScriptServices& services)
    : Tactic(kCrewHold, {}), m_chief(chief), m_services(&services) {}

void WarriorHoldTactic::hold(Brain& member) {
    member.flush();
    member.pushGoal(std::make_unique<HoldPositionGoal>(member.human().position(), kCrewHoldRadius));
}

void WarriorHoldTactic::start(Gang& gang) {
    Brain* chief = m_services->brain(m_chief);
    if (chief == nullptr) {
        return;
    }
    // Each holds where he stands, then turns outward: headings spread evenly round the chief's.
    const std::size_t count = gang.members().size();
    const float spread = count > 1 ? 2.0F * std::numbers::pi_v<float> / static_cast<float>(count - 1) : 0.0F;
    const bool enemies = gangHasEnemy(gang);
    int turn = 0;
    for (Brain* member : gang.members()) {
        if (member == chief || !canAct(*member)) {
            continue;
        }
        hold(*member);
        const int delayMs = enemies ? rollRange(member->random(), 0, kHoldTurnSoonMaxMs)
                                    : rollRange(member->random(), kHoldTurnLaterMinMs, kHoldTurnLaterMaxMs);
        const float heading = human::wrapAngle(chief->human().heading() + spread * static_cast<float>(turn));
        member->queueAction(TurnAction::toHeading(heading, static_cast<std::int16_t>(delayMs)));
        ++turn;
    }
}

bool WarriorHoldTactic::event(Gang& /*gang*/, Brain& member, const BrainEvent& event) {
    const bool again = (event.id == kEventHoldAgain && event.other == nullptr) || event.id == kEventRegoal ||
                       (event.id == kEventGoalsAgain && event.value == 1);
    if (!again || member.handle() == m_chief || !canAct(member)) {
        return false;
    }
    hold(member);
    return true;
}

// ---- CrewOrders ----

std::optional<int> CrewOrders::update(const Brain& chief, int last, std::uint64_t nowMs) {
    const Gang* gang = chief.gang();
    if (last == kCrewFollow) {
        m_calmSince.reset();
        // His target (**stand-in**: his nearest attacker) within his far melee range while he has attackers.
        bool near = false;
        for (const Brain* attacker : chief.attackSlots()) {
            near = near || (Brain::fightable(*attacker) && chief.distanceTo(*attacker) <= chief.meleeFar());
        }
        if (!near) {
            m_attackSince.reset();
            return std::nullopt;
        }
        if (!m_attackSince) {
            m_attackSince = nowMs;
        }
        if (nowMs - *m_attackSince < kCrewAutoDelayMs) {
            return std::nullopt;
        }
        m_attackSince.reset();
        return kCrewAttack;
    }
    m_attackSince.reset();
    if (last != kCrewAttack && last != kCrewDefend) {
        m_calmSince.reset();
        return std::nullopt;
    }
    // Back to follow 1.5 s after no member of the gang has an enemy.
    const bool anyEnemy =
        gang != nullptr && std::ranges::any_of(gang->members(), [](const Brain* member) {
            return std::ranges::any_of(member->enemies(), [](const Brain* enemy) { return Brain::fightable(*enemy); });
        });
    if (anyEnemy) {
        m_calmSince.reset();
        return std::nullopt;
    }
    if (!m_calmSince) {
        m_calmSince = nowMs;
    }
    if (nowMs - *m_calmSince < kCrewAutoDelayMs) {
        return std::nullopt;
    }
    m_calmSince.reset();
    return kCrewFollow;
}

} // namespace coney::ai
