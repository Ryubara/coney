// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/smash_goals.h"

#include <cmath>
#include <memory>

#include "ai/attack_action.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/perception.h"
#include "ai/route_planner.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The walk's gaits: a run from afar, a walk near.
constexpr int kRunGait = 4;
constexpr int kWalkGait = 2;

// Whether `human` can walk from `from` to `to`: always without a planner, else when the planner finds a way
// (the stand-in for `Nav_CanReach`).
// @coney-only stand-in for Nav_CanReach through Coney's route planner
bool canReach(const Brain& human, anim::Vec3 from, anim::Vec3 to) {
    RoutePlanner* planner = human.planner();
    return planner == nullptr || planner->walkDistance(from, to).has_value();
}

// The gang id of `brain`, -1 for none.
// @coney-only lookup helper
int gangOf(const Brain& brain) { return brain.gang() != nullptr ? brain.gang()->id() : -1; }

} // namespace

std::optional<anim::Vec3> standPoint(const Brain& human, anim::Vec3 object) {
    // Coney stand-in for the drop onto marked ground: the spot under the object at his own height.
    const anim::Vec3 point{object.x, object.y, human.human().position().z};
    if (human.planner() == nullptr) {
        return point;
    }
    return human.planner()->navPoint(point);
}

GoalStatus DestroyItemGoal::process(Brain& brain) {
    // 1. Wait for the actions queued (the swing among them).
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // 2. Gone or broken: done; after the swing: done.
    const std::optional<anim::Vec3> object = m_items->objectPosition(m_object);
    if (!object || m_items->broken(m_object)) {
        return GoalStatus::Done;
    }
    // 3. The stand point: the object's own spot on the ground.
    const std::optional<anim::Vec3> point = standPoint(brain, *object);
    if (!point) {
        return GoalStatus::Done;
    }
    const anim::Vec3 at = brain.human().position();
    const float pointDistance = anim::distance(at, *point);
    if (pointDistance <= kDestroyItemReach) {
        // 4. There: face it, then one swing.
        const float heading = human::headingOf(anim::subtract(*object, at));
        if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kDestroyItemTurn) {
            brain.queueAction(TurnAction::toPoint(*object));
            return GoalStatus::Stop;
        }
        if (m_swung) {
            return GoalStatus::Done;
        }
        m_swung = true;
        brain.queueAction(AttackAction::onSelf(0, kDestroyItemSwingDelayMs));
        return GoalStatus::Stop;
    }
    // 5. Walk there, a few times at most.
    if (m_moves >= kDestroyItemMoves) {
        return GoalStatus::Done;
    }
    ++m_moves;
    const int gait = pointDistance < kDestroyItemRunFrom ? kWalkGait : kRunGait;
    brain.queueAction(
        std::make_unique<MoveAction>(MoveRequest{.point = *point, .radius = kDestroyItemArrive, .gait = gait}));
    return GoalStatus::Stop;
}

std::optional<SmashTarget> findSmashTarget(Brain& searcher, const Brain& leader, ItemServices& items,
                                           const ScriptServices& services, float radius, float range) {
    const anim::Vec3 at = searcher.human().position();
    const anim::Vec3 leaderAt = leader.human().position();
    const int gang = gangOf(searcher);
    const Brain* player = services.player();
    for (const SmashTarget& target : items.smashTargets(at, radius)) {
        // The filter: within the range of the leader.
        if (anim::distance(leaderAt, target.position) >= range) {
            continue;
        }
        if (items.claimedBy(target.handle, gang, searcher.nowMs())) {
            continue;
        }
        // No player standing at it (an AI searcher's rule). Coney stand-in: player 1 only, where the original looks at
        // every player.
        if (player != nullptr && player != &searcher &&
            anim::distance(player->human().position(), target.position) < kSmashPlayerClear) {
            continue;
        }
        // A clear ray that crosses no intact pane.
        const ClearRay ray = clearRayTo(searcher.collision(), at, target.position, kSmashRayMask);
        if (!ray.clear || ray.throughGlass) {
            continue;
        }
        // A stand point he can walk to.
        const std::optional<anim::Vec3> point = standPoint(searcher, target.position);
        if (!point || !canReach(searcher, at, *point)) {
            continue;
        }
        items.claim(target.handle, gang, searcher.nowMs() + kSmashClaimMs);
        return target;
    }
    return std::nullopt;
}

std::optional<PaneView> claimPane(Brain& human, ItemServices& items, float radius) {
    const anim::Vec3 at = human.human().position();
    const int gang = gangOf(human);
    for (const PaneView& pane : items.panes(at, radius)) {
        if (items.claimedBy(pane.handle, gang, human.nowMs()) || pane.alarmed) {
            continue;
        }
        // Upright, and its bottom within his reach.
        if (std::fabs(pane.normal.z) > kPaneUpright || pane.centre.z - (pane.height * 0.5F) > at.z + kPaneReachHeight) {
            continue;
        }
        // A clear ray to it that meets the glass.
        const ClearRay ray = clearRayTo(human.collision(), at, pane.centre, 0);
        if (!ray.clear || !ray.throughGlass) {
            continue;
        }
        const std::optional<anim::Vec3> point = standPoint(human, pane.centre);
        if (!point || !canReach(human, at, *point)) {
            continue;
        }
        items.claim(pane.handle, gang, human.nowMs() + kPaneClaimMs);
        return pane;
    }
    return std::nullopt;
}

} // namespace coney::ai
