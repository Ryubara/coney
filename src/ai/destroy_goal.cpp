// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/destroy_goal.h"

#include <memory>
#include <vector>

#include "ai/attack_action.h"
#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/smash_goals.h"
#include "ai/steal_goals.h"
#include "ai/swap_prompt.h"
#include "ai/turn_action.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// The gait of the run to a carried object (`Goal_GetItem(human, 4, object)`).
constexpr int kFetchGait = 4;
// The anim sets of carried and thrown objects, which are fetched rather than struck where they stand.
constexpr int kCarriedSet = 4;
constexpr int kThrownSet = 5;

// The squared 3D distance. @coney-only vector helper
float distanceSquared(anim::Vec3 a, anim::Vec3 b) {
    const anim::Vec3 d = anim::subtract(a, b);
    return anim::dot(d, d);
}

// Drops `brain`'s target. @coney-only helper: Human_DropTarget through Brain::setTarget
void dropTarget(Brain& brain) {
    const TargetCauseScope cause(brain, TargetCause::Order);
    brain.setTarget(nullptr);
}

} // namespace

void DestroyGoal::start(Brain& brain) {
    dropTarget(brain);
    // Coney stand-in: the wreck tactic flag (+0x35) and the brain byte +0x265 saved and cleared are not built.
}

void DestroyGoal::resume(Brain& brain) {
    if (m_fetching && brain.human().script().heldObject == 0.0) {
        m_letGo = true;
    }
    m_fetching = false;
}

void DestroyGoal::end(Brain& brain) {
    // Coney stand-in: the Suspend the original runs first, the drop of what he holds and the restore of the brain
    // byte +0x265 are not built.
    releaseTarget();
    dropTarget(brain);
}

void DestroyGoal::releaseTarget() {
    if (m_target) {
        m_items->unclaim(*m_target);
    }
    m_target.reset();
    m_targetPane = false;
}

void DestroyGoal::armWait(Brain& brain) {
    const auto low = static_cast<int>(m_waitMs);
    const auto high = low + static_cast<int>(kDestroyWaitSpreadMs);
    m_waitUntilMs = brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), low, high));
}

void DestroyGoal::pickTarget(Brain& brain) {
    ++m_picks;
    m_target.reset();
    m_targetPane = false;
    const bool oddPick = (m_picks & 1U) != 0;
    const std::optional<double> object = m_items->nearestZoneTarget(m_zone, brain.human().position(), brain.nowMs());
    const std::optional<anim::Vec3> where = object ? m_items->objectPosition(*object) : std::nullopt;
    // No object left: every other pick a pane near him.
    if (!object || !where) {
        if (oddPick) {
            return;
        }
        if (const std::optional<PaneView> pane = claimPane(brain, *m_items, kDestroyPaneRadius)) {
            m_target = pane->handle;
            m_targetPane = true;
        }
        return;
    }
    // An object beyond the leader's radius is left.
    const Gang* gang = brain.gang();
    if (m_leaderRange >= 0.0F && gang != nullptr) {
        if (const Brain* leader = gang->leader();
            leader != nullptr && distanceSquared(leader->human().position(), *where) > m_leaderRange * m_leaderRange) {
            return;
        }
    }
    // Every other pick a pane near the object goes first; else the object, claimed for his gang.
    if (!oddPick) {
        if (const std::optional<PaneView> pane = claimPane(brain, *m_items, kDestroyPaneRadius, where)) {
            m_target = pane->handle;
            m_targetPane = true;
            return;
        }
    }
    m_items->claim(*object, gang != nullptr ? gang->id() : -1, brain.nowMs() + kDestroyClaimMs);
    m_target = object;
}

GoalStatus DestroyGoal::process(Brain& brain) {
    // 1. Busy: queued actions, or winding up a swing of what he holds.
    if (brain.actionCount() > 0 || swingingHeld(brain.human())) {
        return GoalStatus::Stop;
    }
    // Coney stand-in: the fight stance is not entered (issue #959).
    // 2. After a swing, or empty-handed back from a fetch: the target goes and a new wait starts.
    const bool targetThere = m_target && m_items->objectPosition(*m_target).has_value();
    if (m_letGo) {
        m_letGo = false;
        m_fetching = false;
        releaseTarget();
        armWait(brain);
        return GoalStatus::Stop;
    }
    // 3. No target: wait, then pick one.
    if (!targetThere) {
        if (m_waitUntilMs == 0) {
            armWait(brain);
            return GoalStatus::Stop;
        }
        if (brain.nowMs() < m_waitUntilMs) {
            // Coney stand-in: the pick-up of a breaking object while waiting (DestroyGoal_TryPickUp) is not built.
            return GoalStatus::Stop;
        }
        pickTarget(brain);
        m_tries = 0;
        m_waitUntilMs = 0;
        return GoalStatus::Stop;
    }
    // 4. Tried enough: let it go.
    if (m_tries >= kDestroyTries) {
        releaseTarget();
        return GoalStatus::Stop;
    }
    // 5. Something in hand: one he swings turns him once to the glass, then is swung; a breaking one goes on.
    const double held = brain.human().script().heldObject;
    const HeldUse use = held != 0.0 ? m_items->heldUse(held) : HeldUse::None;
    if (use == HeldUse::Other) {
        // Coney stand-in: the original drops it (Human_DropHeld); ItemServices has no drop.
        return GoalStatus::Stop;
    }
    if (use == HeldUse::Swings) {
        if (brain.human().heldFlags() != 0) {
            return GoalStatus::Stop;
        }
        if (!m_turnedToGlass) {
            // Coney stand-in: the nearest car within 30 m is not weighed against the pane (no cars yet).
            const std::vector<PaneView> near = m_items->panes(brain.human().position(), kDestroyTurnRadius);
            if (!near.empty()) {
                brain.queueAction(TurnAction::toPoint(near.front().centre));
                m_turnedToGlass = true;
                return GoalStatus::Stop;
            }
        }
        // Coney stand-in: the turn to the crowd ahead (Gang_CountAhead) and the taunt every 8 s are not built.
        brain.queueAction(AttackAction::onSelf(0, kDestroySwingDelayMs));
        m_turnedToGlass = false;
        m_letGo = true;
        return GoalStatus::Stop;
    }
    // 6. The target: a carried object is fetched (unless a held breaking object strikes it), anything else smashed.
    if (!m_targetPane) {
        const std::optional<SmashTarget> view = m_items->wreckView(*m_target);
        const bool strikeHeld = use == HeldUse::Breaks && view && view->heldBreaks;
        if (!strikeHeld && view && (view->animSet == kCarriedSet || view->animSet == kThrownSet)) {
            // Coney stand-in: the original first drops what he holds (Human_DropHeld); ItemServices has no drop.
            static_cast<void>(brain.pushGoal(std::make_unique<GetItemGoal>(*m_target, kFetchGait, *m_items)));
            m_fetching = true;
            ++m_tries;
            return GoalStatus::Stop;
        }
    }
    static_cast<void>(brain.pushGoal(std::make_unique<DestroyItemGoal>(*m_target, m_targetPane, *m_items)));
    m_fetching = false;
    ++m_tries;
    return GoalStatus::Stop;
}

} // namespace coney::ai
