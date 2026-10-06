// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/riot_goals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/play_anim_action.h"
#include "ai/script_services.h"
#include "ai/story_goals.h"
#include "ai/turn_action.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The distance between two points in plan.
float flatDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The heading that faces from `from` to `to` (the game's: 0 along +y, turning toward -x).
float headingTo(anim::Vec3 from, anim::Vec3 to) { return std::atan2(-(to.x - from.x), to.y - from.y); }

// Coney choice: "about half the time" is read as an even roll.
constexpr int kDecisionPercent = 50;
// Coney stand-in: the roaming rioter walks (gait 2) and arrives within 1 m.
constexpr int kRoamGait = 2;
constexpr float kRoamRadius = 1.0F;
// The thrower returns to its spot at a walk.
constexpr int kThrowerGait = 2;
// A thrower turns until it faces its target within this many radians (**Coney choice**, as the throw goal's).
constexpr float kFaceAngle = 0.35F;
// The thrower looks this share of its sight range for an enemy.
constexpr float kThrowerSightShare = 0.75F;
// Milliseconds in a second, for the wait.
constexpr int kMsPerSecond = 1000;

} // namespace

// ---- RiotGoal ----

RiotGoal::RiotGoal(const RiotOrder& order, const RiotServices& services)
    : Goal(GoalType::Riot), m_order(order), m_services(&services) {}

void RiotGoal::start(Brain& brain) { m_centre = brain.human().position(); }

bool RiotGoal::playerNear(const Brain& brain) const {
    if (!m_services->players) {
        return false;
    }
    const anim::Vec3 position = brain.human().position();
    const std::vector<anim::Vec3> players = m_services->players();
    return std::ranges::any_of(players,
                               [&](anim::Vec3 player) { return anim::distance(player, position) <= m_order.radius; });
}

bool RiotGoal::tryPickFight(Brain& brain) {
    // Only a gang member picks fights: with a civilian, or (by the gang-fight chance) with a gang member too.
    if (brain.type() != BrainType::Gang || !m_services->candidates) {
        return false;
    }
    const bool gangToo = brain.rand100() < m_order.gangFightChance;
    std::vector<Brain*> candidates = m_services->candidates();
    std::erase_if(candidates, [&brain, gangToo](const Brain* other) {
        if (other == &brain || other->type() == BrainType::Player) {
            return true;
        }
        const bool civilian = other->type() == BrainType::Civilian || other->type() == BrainType::CivilianDi;
        return !civilian && !(gangToo && other->type() == BrainType::Gang);
    });
    Brain* nearest = nullptr;
    float best = kRiotFightRange;
    for (Brain* other : candidates) {
        if (!Brain::fightable(*other) || other->human().state() != human::TargetState::Standing) {
            continue;
        }
        if (const float distance = brain.distanceTo(*other); distance <= best) {
            nearest = other;
            best = distance;
        }
    }
    return nearest != nullptr && brain.fight(*nearest);
}

GoalStatus RiotGoal::process(Brain& brain) {
    if (m_state == RiotState::Leave) {
        // Leaving: the exit goal goes over this one; should it end, it is pushed again.
        if (m_services->leave) {
            m_services->leave(brain);
        }
        return GoalStatus::Stop;
    }
    // A decision: acting out only while a player is near.
    if (brain.updates() % kRiotDecisionUpdates == 0 && brain.rand100() < kDecisionPercent && playerNear(brain)) {
        if (brain.rand100() < m_order.fightChance && tryPickFight(brain)) {
            m_state = RiotState::Leave;
            return GoalStatus::Stop;
        }
        // The smash and loot searches find nothing in Coney (see the class), so no act counts down.
    }
    if (m_order.acts <= 0) {
        m_state = RiotState::Leave;
        return GoalStatus::Stop;
    }
    // The roam: on to another point near where it started.
    if (brain.actionCount() == 0) {
        const float angle = static_cast<float>(brain.rand100()) * 2.0F * std::numbers::pi_v<float> / 100.0F;
        const float reach = kRiotRoamRange * static_cast<float>(brain.rand100()) / 100.0F;
        const anim::Vec3 point{m_centre.x + (reach * std::cos(angle)), m_centre.y + (reach * std::sin(angle)),
                               m_centre.z};
        brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = point,
                                                                   .radius = kRoamRadius,
                                                                   .gait = kRoamGait,
                                                                   .option = false,
                                                                   .delayMs = 0,
                                                                   .flagKind = false}));
    }
    return GoalStatus::Stop;
}

// ---- StationaryThrowerGoal ----

StationaryThrowerGoal::StationaryThrowerGoal(int delaySeconds, const ThrowerObjects& objects, ScriptServices& services)
    : Goal(GoalType::StationaryThrower), m_delaySeconds(delaySeconds & 0xff), m_objects(objects),
      m_services(&services) {}

void StationaryThrowerGoal::start(Brain& brain) { m_spot = brain.human().position(); }

GoalStatus StationaryThrowerGoal::process(Brain& brain) {
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    switch (m_state) {
    case ThrowerState::Return:
        if (flatDistance(m_spot, brain.human().position()) > kThrowerSpotRadius) {
            brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = m_spot,
                                                                       .radius = kThrowerSpotRadius,
                                                                       .gait = kThrowerGait,
                                                                       .option = false,
                                                                       .delayMs = 0,
                                                                       .flagKind = false}));
            return GoalStatus::Stop;
        }
        m_state = ThrowerState::Throw;
        [[fallthrough]];
    case ThrowerState::Throw: {
        brain.stopMove();
        const std::vector<Brain*> candidates = knownBrains(brain);
        Brain* enemy = nearestHostileWithin(brain, candidates, brain.sightRange() * kThrowerSightShare);
        if (enemy == nullptr) {
            return GoalStatus::Stop;
        }
        const anim::Vec3 position = brain.human().position();
        const anim::Vec3 target = enemy->human().position();
        if (std::fabs(human::wrapAngle(headingTo(position, target) - brain.human().heading())) > kFaceAngle) {
            brain.queueAction(TurnAction::toPoint(target));
            return GoalStatus::Stop;
        }
        brain.queueAction(std::make_unique<PlayAnimAction>(*m_services, kThrowAnim, false));
        ++m_throws;
        m_waitUntilMs = brain.nowMs() + static_cast<std::uint64_t>(kMsPerSecond * m_delaySeconds) +
                        static_cast<std::uint64_t>(rollRange(brain.random(), 0, kMsPerSecond));
        m_state = ThrowerState::Wait;
        return GoalStatus::Stop;
    }
    case ThrowerState::Wait:
        if (brain.nowMs() >= m_waitUntilMs) {
            m_state = ThrowerState::Return;
        }
        return GoalStatus::Stop;
    }
    return GoalStatus::Stop;
}

} // namespace coney::ai
