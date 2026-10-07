// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/engage_goals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "ai/attack_action.h"
#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/fight_goal.h"
#include "ai/move_action.h"
#include "ai/route_planner.h"
#include "ai/script_services.h"
#include "animation/anim_math.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// Whether `brain`'s human is down: out of health or on the ground.
bool down(const Brain& brain) {
    return brain.human().fighter().health().depleted() || brain.human().state() == human::TargetState::Grounded;
}

// Below this speed a stopping human stands, m/s.
constexpr float kStoppedSpeed = 0.05F;
// A turn of the target beyond this re-plans the run, radians (45 degrees).
constexpr float kReplanTurn = std::numbers::pi_v<float> / 4.0F;
// The lead applies while the target faces within this of the runner's way, radians (stand-in for sectors 3-5).
constexpr float kLeadAngle = std::numbers::pi_v<float> / 3.0F;
// Degrees to radians.
constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;
// The run's gaits.
constexpr int kRunGait = 4;
constexpr int kSprintGait = 5;

} // namespace

GoalStatus MoveToHumanGoal::process(Brain& brain) {
    const Brain* target = m_services->brain(m_target);
    if (down(brain) || target == nullptr || target->human().outOfWorld() || !target->human().alive()) {
        return GoalStatus::Done;
    }
    if (anim::distance(brain.human().position(), target->human().position()) <= m_radius) {
        brain.clearActions();
        return GoalStatus::Done;
    }
    // A failed route is waited out before the next try.
    if (brain.moveFailure() != MoveFailure::None) {
        if (++m_failedUpdates < kMoveToHumanRetryUpdates) {
            return GoalStatus::Stop;
        }
        brain.setMoveFailure(MoveFailure::None);
        m_failedUpdates = 0;
        m_nextMoveMs = 0;
    }
    if (brain.nowMs() >= m_nextMoveMs || brain.actionCount() == 0) {
        brain.clearActions();
        brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = target->human().position(),
                                                                   .radius = m_radius,
                                                                   .gait = m_gait,
                                                                   .option = false,
                                                                   .delayMs = 0,
                                                                   .flagKind = false}));
        m_nextMoveMs = brain.nowMs() + kMoveToHumanReissueMs;
    }
    return GoalStatus::Stop;
}

Brain* EngageEnemyGoal::targetOf(Brain& brain) const {
    return m_services != nullptr ? m_services->brain(m_enemy) : brain.target();
}

void EngageEnemyGoal::start(Brain& brain) {
    m_stopping = false;
    m_moved = false;
    m_nextPlanMs = 0;
    m_nextRetargetMs = brain.nowMs() + kEngageRetargetMs;
    m_startSlowly = brain.human().gait() < human::Gait::Jog;
    const Brain* target = targetOf(brain);
    m_charge = target != nullptr && brain.distanceTo(*target) >= brain.meleeFar();
}

void EngageEnemyGoal::end(Brain& brain) { brain.clearActions(); }

GoalStatus EngageEnemyGoal::beginStop(Brain& brain) {
    m_stopping = true;
    brain.clearActions();
    brain.stopMove();
    return GoalStatus::Stop;
}

GoalStatus EngageEnemyGoal::arrive(Brain& brain, const Brain& target) {
    if (m_services == nullptr) {
        return GoalStatus::Done;
    }
    m_stopping = false;
    m_charge = brain.distanceTo(target) >= brain.meleeFar();
    brain.pushGoal(std::make_unique<FightGoal>(kMeleeFightMs));
    return GoalStatus::Again;
}

GoalStatus EngageEnemyGoal::process(Brain& brain) {
    Brain* target = targetOf(brain);
    if (target == nullptr || target == &brain || target->human().outOfWorld() || !Brain::fightable(*target)) {
        return GoalStatus::Done;
    }
    if (down(brain)) {
        return GoalStatus::Stop;
    }
    if (m_services != nullptr) {
        brain.addEnemy(*target);
        if (brain.target() != target) {
            brain.setTarget(target);
        }
    }
    const human::Human& human = brain.human();
    // 2. The stop: once standing, turn to him and end.
    if (m_stopping) {
        if (human.speed() > kStoppedSpeed) {
            return GoalStatus::Stop;
        }
        brain.setMoveHeading(human::headingOf(anim::subtract(target->human().position(), human.position())), 0.0F);
        return arrive(brain, *target);
    }
    // 4. Every 2 s, the nearest enemy close by instead of a target beyond the far range.
    if (m_services == nullptr && brain.nowMs() >= m_nextRetargetMs) {
        m_nextRetargetMs = brain.nowMs() + kEngageRetargetMs;
        if (brain.distanceTo(*target) > brain.meleeFar()) {
            Brain* nearest = nullptr;
            float best = kEngageSeeRange;
            for (Brain* enemy : brain.enemies()) {
                if (Brain::fightable(*enemy) && !enemy->human().outOfWorld() && brain.distanceTo(*enemy) < best) {
                    best = brain.distanceTo(*enemy);
                    nearest = enemy;
                }
            }
            if (nearest != nullptr && nearest != target) {
                brain.setTarget(nearest);
                target = nearest;
            }
        }
    }
    const human::Human& them = target->human();
    // 5. He may be attacked at once.
    target->setAttackableAtMs(0);
    // 6. A new plan every 250 ms, or at once when he turned, came close or slowed from a run.
    const float distance = brain.distanceTo(*target);
    const bool running = them.gait() > human::Gait::Jog;
    const bool turned = std::fabs(human::wrapAngle(them.heading() - m_planHeading)) > kReplanTurn;
    const bool replan =
        brain.nowMs() >= m_nextPlanMs || distance <= kChargeRange || turned || (m_planRunning && !running);
    if (!replan && brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    m_nextPlanMs = brain.nowMs() + kEngageReplanMs;
    m_planHeading = them.heading();
    m_planRunning = running;
    // 7. A failed move ends the run.
    if (brain.moveFailure() != MoveFailure::None) {
        return beginStop(brain);
    }
    // 8. Run; sprint after a runner.
    const int gait = running ? kSprintGait : kRunGait;
    // 9. Close by: stop for a busy target, or for one who walks or stands when the charge is not armed.
    if (distance <= kEngageStopShare * brain.meleeFar()) {
        const bool busy =
            them.state() != human::TargetState::Standing || (them.animator().flags() & kAttackWaitFlags) != 0;
        if (busy || (!m_charge && !running)) {
            return beginStop(brain);
        }
        m_charge = m_charge || running;
    }
    // 10. Give up when he is far off and out of sight.
    if (distance >= kEngageGiveUpRange && distance > brain.sightRange()) {
        return GoalStatus::Done;
    }
    // 11. The charge: an attack out of the run.
    if (distance <= kChargeRange && (m_charge || running)) {
        if (const std::optional<int> kind = pickAttack(brain.attackWeights(), brain.random()); kind.has_value()) {
            brain.clearActions();
            queueAttack(brain, *kind);
            m_charge = false;
            return GoalStatus::Stop;
        }
    }
    // 12. The move: at him, led by his facing and speed while he faces away, fanned out by attack slot.
    anim::Vec3 aim = them.position();
    const anim::Vec3 toThem = anim::subtract(them.position(), human.position());
    if (them.speed() > 0.0F && std::fabs(human::wrapAngle(them.heading() - human::headingOf(toThem))) < kLeadAngle) {
        const std::vector<Brain*>& slots = target->attackSlots();
        const auto index = static_cast<int>(std::ranges::find(slots, &brain) - slots.begin());
        const float side = index % 2 == 0 ? -1.0F : 1.0F;
        const float fan = side * static_cast<float>((index + 1) / 2) * kEngageFanDegrees * kDegrees;
        const anim::Vec3 led =
            anim::add(them.position(), anim::scale(human::facing(them.heading() + fan), them.speed()));
        const RoutePlanner* planner = brain.planner();
        if (planner == nullptr || planner->lineClear(human.position(), led)) {
            aim = led;
        }
    }
    const bool slowStart = !m_moved && m_startSlowly;
    brain.clearActions();
    brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = aim,
                                                               .radius = kEngageMoveRadius,
                                                               .gait = gait,
                                                               .delayMs = slowStart ? kRandomDelay : std::int16_t{0}}));
    m_moved = true;
    return GoalStatus::Stop;
}

} // namespace coney::ai
