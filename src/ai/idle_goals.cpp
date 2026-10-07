// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/idle_goals.h"

#include <cmath>
#include <memory>
#include <numbers>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/engage_goals.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/turn_action.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The Melee goal's spectate: 1-3 s between looks, a 2 s spell.
constexpr int kMeleeShortestPauseMs = 1000;
constexpr int kMeleeLongestPauseMs = 3000;
constexpr int kMeleeTimeLimitMs = 2000;
// The dealer's wary spectate: 8 m, 2-4 s between looks, 8 s.
constexpr float kWaryKeep = 8.0F;
constexpr int kWaryShortestPauseMs = 2000;
constexpr int kWaryLongestPauseMs = 4000;
constexpr int kWaryTimeLimitMs = 8000;
// The watched man is picked again this often, ms.
constexpr std::uint64_t kRepickMs = 3000;
// A run is checked for one update in this many.
constexpr std::uint64_t kRunCheckUpdates = 5;
// Beyond this he moves toward the watched man, m; the move stops this far within the far range, m.
constexpr float kFarWatch = 10.0F;
constexpr float kFarWatchRadiusExtra = 2.0F;
// The join's watching band beyond the keep distance, m, and how often he only turns to face, percent.
constexpr float kWatchBand = 2.0F;
constexpr int kWatchChance = 51;
// The keep-distance move holds him between this share of the keep distance and all of it.
constexpr float kKeepInner = 0.95F;
// The back-out move arrives within this of the band's edge, m.
constexpr float kBackOutRadius = 0.25F;
// The moves' gaits: walk, run.
constexpr int kWalkGait = 2;
constexpr int kRunGait = 4;

// Whether `other` can be watched: health left and in the world (the valid-target test `0x0028d4b0`).
bool valid(const Brain& other) { return Brain::fightable(other) && !other.human().outOfWorld(); }

// Whether `other`'s human runs (gait run or sprint).
bool runs(const Brain& other) { return other.human().gait() >= human::Gait::Run; }

// SpectateGoal_PickTarget: with `join` the nearest valid man of the brain's enemy list, else the nearest valid member
// of a gang other than its own (**Coney reading** of `0x0016c808`'s "nearest member of the nearest other gang").
Brain* pickWatched(Brain& brain, bool join) {
    Brain* nearest = nullptr;
    float best = 0.0F;
    const auto consider = [&](Brain* other) {
        if (other == nullptr || other == &brain || !valid(*other)) {
            return;
        }
        if (const float d = brain.distanceTo(*other); nearest == nullptr || d < best) {
            nearest = other;
            best = d;
        }
    };
    if (join) {
        for (Brain* enemy : brain.enemies()) {
            consider(enemy);
        }
        return nearest;
    }
    const Gang* own = brain.gang();
    if (own == nullptr) {
        return nullptr;
    }
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        const Gang* other = own->owner().find(id);
        if (other == nullptr || other == own) {
            continue;
        }
        for (Brain* member : other->members()) {
            consider(member);
        }
    }
    return nearest;
}

} // namespace

SpectateArgs SpectateArgs::melee(float keepDistance) {
    return SpectateArgs{.keepDistance = keepDistance,
                        .join = true,
                        .mayEngage = true,
                        .shortestPauseMs = kMeleeShortestPauseMs,
                        .longestPauseMs = kMeleeLongestPauseMs,
                        .timeLimitMs = kMeleeTimeLimitMs,
                        .taunt = true};
}

SpectateArgs SpectateArgs::dealerWary() {
    return SpectateArgs{.keepDistance = kWaryKeep,
                        .shortestPauseMs = kWaryShortestPauseMs,
                        .longestPauseMs = kWaryLongestPauseMs,
                        .timeLimitMs = kWaryTimeLimitMs};
}

GoalStatus IdleGoal::process(Brain& brain) {
    if (brain.actionCount() == 0) {
        brain.stopMove();
    }
    return GoalStatus::Stop;
}

SpectateGoal::SpectateGoal(int minMs, int maxMs)
    : Goal(GoalType::Spectate), m_still(true), m_stillMinMs(minMs), m_stillMaxMs(maxMs) {}

void SpectateGoal::start(Brain& brain) {
    const std::uint64_t now = brain.nowMs();
    if (m_still) {
        m_untilMs = now + static_cast<std::uint64_t>(rollRange(brain.random(), m_stillMinMs, m_stillMaxMs));
        return;
    }
    m_watched = pickWatched(brain, m_args.join);
    m_nextPickMs = now + kRepickMs;
    m_nextLookMs = now;
    m_untilMs = now + static_cast<std::uint64_t>(m_args.timeLimitMs);
    m_chase = brain.random().coin();
}

GoalStatus SpectateGoal::process(Brain& brain) {
    const std::uint64_t now = brain.nowMs();
    if (now >= m_untilMs) {
        return GoalStatus::Done;
    }
    if (m_still) {
        if (brain.actionCount() == 0) {
            brain.stopMove();
        }
        return GoalStatus::Stop;
    }
    // A failed move stops the chase; every 3 s the watched man is picked again.
    const bool moveFailed = brain.moveFailure() != MoveFailure::None;
    if (moveFailed) {
        m_chase = false;
    }
    if (now >= m_nextPickMs) {
        m_nextPickMs = now + kRepickMs;
        m_watched = pickWatched(brain, m_args.join);
    }
    // 1. No one to watch.
    if (m_watched == nullptr || !valid(*m_watched)) {
        m_watched = nullptr;
        return m_args.join ? GoalStatus::Done : GoalStatus::Stop;
    }
    Brain& watched = *m_watched;
    // 2. With join he is an enemy. 3. Out of sight: done.
    if (m_args.join) {
        brain.addEnemy(watched);
    }
    if (!brain.canSee(watched, brain.sightRange())) {
        return GoalStatus::Done;
    }
    // 4. Run in at him when he runs, or when he is beyond the far range and the chase flag is set.
    const float distance = brain.distanceTo(watched);
    const float far = brain.meleeFar();
    if (m_args.join && m_args.mayEngage && !moveFailed) {
        const bool running = brain.updates() % kRunCheckUpdates == 0 && runs(watched);
        if (running || (m_chase && distance > far)) {
            brain.setTarget(&watched);
            brain.pushGoal(std::make_unique<EngageEnemyGoal>());
            return GoalStatus::Again;
        }
    }
    // 5. Watch, with the actions free and the pause over.
    if (brain.actionCount() > 0 || now < m_nextLookMs) {
        return GoalStatus::Stop;
    }
    m_nextLookMs =
        now + static_cast<std::uint64_t>(rollRange(brain.random(), m_args.shortestPauseMs, m_args.longestPauseMs));
    const anim::Vec3 at = watched.human().position();
    if (distance > kFarWatch) {
        const int gait = runs(watched) || m_args.join ? kRunGait : kWalkGait;
        brain.queueAction(
            std::make_unique<MoveAction>(MoveRequest{.point = at, .radius = far + kFarWatchRadiusExtra, .gait = gait}));
        return GoalStatus::Stop;
    }
    const float keep = m_args.keepDistance > 0.0F ? m_args.keepDistance : far;
    if (m_args.join && distance >= keep && distance <= keep + kWatchBand && brain.rand100() < kWatchChance) {
        brain.queueAction(TurnAction::toPoint(at));
        return GoalStatus::Stop;
    }
    // Hold him between 0.95 × and 1 × the keep distance: in toward him, or back out along the line from him.
    if (distance > keep) {
        brain.queueAction(std::make_unique<MoveAction>(MoveRequest{.point = at, .radius = keep, .gait = kWalkGait}));
    } else if (distance < kKeepInner * keep) {
        const anim::Vec3 from = brain.human().position();
        anim::Vec3 away = anim::subtract(from, at);
        const float length = std::hypot(away.x, away.y);
        away = length > 1e-4F ? anim::Vec3{away.x / length, away.y / length, 0.0F}
                              : human::facing(brain.human().heading() + std::numbers::pi_v<float>);
        const anim::Vec3 out{at.x + (away.x * keep), at.y + (away.y * keep), from.z};
        brain.queueAction(
            std::make_unique<MoveAction>(MoveRequest{.point = out, .radius = kBackOutRadius, .gait = kWalkGait}));
    } else {
        brain.queueAction(TurnAction::toPoint(at));
    }
    return GoalStatus::Stop;
}

void SpectateGoal::end(Brain& brain) {
    if (m_still) {
        return;
    }
    brain.setTarget(nullptr);
    brain.clearActions();
    brain.setMoveFailure(MoveFailure::None);
}

} // namespace coney::ai
