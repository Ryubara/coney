// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/steal_goals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "ai/attack_action.h"
#include "ai/brain.h"
#include "ai/crew.h"
#include "ai/formations.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/perception.h"
#include "ai/route_planner.h"
#include "ai/script_services.h"
#include "ai/smash_goals.h"
#include "ai/turn_action.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// The member events the steal tactic answers: 19 and 22 with argument 1 give that member his goal again; 20 (a
// fight seen, kEventViolence) is taken and does nothing.
constexpr int kEventRegoal = 19;
constexpr int kEventGoalsAgain = 22;
// GetItem's walk jogs near the object.
constexpr int kJogGait = 3;

// The distance from `a` to `b` in plan.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Whether `brain` is an AI human's that can act: not a player's, fightable and not on the ground.
bool canAct(const Brain& brain) {
    return brain.type() != BrainType::Player && Brain::fightable(brain) &&
           brain.human().state() != human::TargetState::Grounded;
}

// The held flag of a swing of what he holds, and the flags a throw clip holds.
constexpr std::uint32_t kSwingHeldFlag = 0x800000;
constexpr std::uint32_t kThrowHeldFlags = 0x1000010;
// The object throw clips: one-handed (467, 471, 472), the knife (491), the bat (502), two-handed (505-507) and the
// ghetto set (551-553).
constexpr std::array<std::uint32_t, 11> kThrowClips{467, 471, 472, 491, 502, 505, 506, 507, 551, 552, 553};

// Whether `human` is winding up a throw of what he holds: the swing flag, or a throw clip holding its flags with an
// object in his hand (docs/research/characters.md, Human_IsSwingingHeld). **Coney's reading**: "an object in his
// hand" is any held object, where the original asks that the held object's anim set be other than 0.
// @orig 0x002286a0 Human_IsSwingingHeld (unknown)
bool swingingHeld(const human::Human& human) {
    const std::uint32_t flags = human.heldFlags();
    if ((flags & kSwingHeldFlag) != 0) {
        return true;
    }
    // Coney's choice: any held object stands for the original's test that its anim set is not 0.
    return (flags & kThrowHeldFlags) != 0 && human.script().heldObject != 0.0 &&
           std::ranges::contains(kThrowClips, human.animator().animId());
}

} // namespace

// ---- StealClusters ----

void StealClusters::build(std::span<const LooseItem> items) {
    m_clusters.clear();
    for (const LooseItem& item : items) {
        const auto joined = std::ranges::find_if(m_clusters, [&item](const StealCluster& cluster) {
            return cluster.objects.size() < kStealClusterObjects &&
                   anim::length(anim::subtract(item.position, cluster.centre)) <= kStealClusterRadius;
        });
        if (joined != m_clusters.end()) {
            joined->objects.push_back(item.handle);
        } else if (m_clusters.size() < kStealClusters) {
            m_clusters.push_back(StealCluster{.centre = item.position, .objects = {item.handle}, .users = 0});
        }
    }
}

bool StealClusters::claimed(double object, int gang, std::uint64_t nowMs) const {
    const auto found = m_claims.find(object);
    return found != m_claims.end() && found->second.gang == gang && nowMs < found->second.untilMs;
}

std::optional<double> StealClusters::claim(const Brain& human, const ItemServices& items, int gang,
                                           std::uint64_t nowMs) {
    const anim::Vec3 at = human.human().position();
    // The clusters in the order they are walked: nearest centre first (the stand-in for the sort).
    std::vector<StealCluster*> order;
    order.reserve(m_clusters.size());
    for (StealCluster& cluster : m_clusters) {
        order.push_back(&cluster);
    }
    std::ranges::stable_sort(order, [at](const StealCluster* a, const StealCluster* b) {
        return planDistance(at, a->centre) < planDistance(at, b->centre);
    });
    for (StealCluster* cluster : order) {
        // The cluster's nearest object still there, in nobody's hand and not claimed by the gang.
        std::optional<double> best;
        anim::Vec3 bestAt{};
        float bestDistance = std::numeric_limits<float>::max();
        for (const double object : cluster->objects) {
            const std::optional<anim::Vec3> where = items.itemPosition(object);
            if (!where || items.holder(object) || claimed(object, gang, nowMs)) {
                continue;
            }
            if (const float distance = planDistance(at, *where); distance < bestDistance) {
                best = object;
                bestAt = *where;
                bestDistance = distance;
            }
        }
        if (!best) {
            continue;
        }
        // Not while an enemy stands by it.
        const bool guarded = std::ranges::any_of(human.enemies(), [bestAt](const Brain* enemy) {
            return Brain::fightable(*enemy) && planDistance(enemy->human().position(), bestAt) <= kStealEnemyClear;
        });
        if (guarded) {
            continue;
        }
        m_claims[*best] = Claim{.gang = gang, .untilMs = nowMs + kStealClaimMs};
        ++cluster->users;
        return best;
    }
    return std::nullopt;
}

// ---- GetItemGoal ----

void GetItemGoal::start(Brain& brain) {
    m_savedTurnBoost = brain.turnBoost();
    brain.setTurnBoost(m_savedTurnBoost + 1);
    {
        // Human_DropTarget from GetItemGoal_Start (docs/research/gang-fights.md#set-target-causes).
        const TargetCauseScope cause(brain, TargetCause::Order);
        brain.setTarget(nullptr);
    }
    resume(brain);
}

void GetItemGoal::resume(Brain& brain) {
    m_point.reset();
    const std::optional<anim::Vec3> object = m_items->itemPosition(m_object);
    if (!object) {
        return;
    }
    // The ground beside the object, at his own height (an object on a shelf stands above the floor he walks).
    const anim::Vec3 below{object->x, object->y, brain.human().position().z};
    m_point = brain.planner() != nullptr ? brain.planner()->navPoint(below) : std::optional<anim::Vec3>(below);
}

void GetItemGoal::end(Brain& brain) { brain.setTurnBoost(m_savedTurnBoost); }

GoalStatus GetItemGoal::process(Brain& brain) {
    // 1. Gone: done.
    const std::optional<anim::Vec3> object = m_items->itemPosition(m_object);
    if (!object) {
        return GoalStatus::Done;
    }
    // 2. In flight (thrown or falling, its flag 0x4000000): wait for it to land.
    if (m_items->inFlight(m_object)) {
        return GoalStatus::Stop;
    }
    // 3. His pick-up under way: wait for its take.
    if (brain.human().pickingUp()) {
        m_picked = true;
        return GoalStatus::Stop;
    }
    // 4. Someone holds it (he himself among them): done.
    if (m_items->holder(m_object)) {
        return GoalStatus::Done;
    }
    // 5. Nobody holds it: done once picked; wait for queued actions; give up without a point, after a failed walk or
    // too many tries.
    if (m_picked) {
        return GoalStatus::Done;
    }
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    const bool failed = brain.moveFailure() != MoveFailure::None;
    brain.setMoveFailure(MoveFailure::None);
    if (!m_point || failed || m_tries >= kGetItemTries) {
        return GoalStatus::Done;
    }
    const anim::Vec3 at = brain.human().position();
    // The distance to his standing point (3D), which every test below reads.
    const float pointDistance = anim::distance(at, *m_point);
    // 6. Within reach of the point: with a clear ray, the pick-up (a try); a clear ray through an intact pane, the
    // swing at the pane from the point (no try), so nothing is taken through glass; no clear ray, a turn to it, or,
    // facing it, a try and the walk to the point below.
    if (pointDistance < kGetItemReach) {
        const ClearRay ray = clearRayTo(brain.collision(), at, *object, 0);
        if (ray.clear && !ray.throughGlass) {
            ++m_tries;
            if (m_items->startPickUp(brain, m_object)) {
                m_picked = true;
            }
            return GoalStatus::Stop;
        }
        if (ray.clear) {
            if (pointDistance > kGetItemGlassStep) {
                brain.queueAction(std::make_unique<MoveAction>(
                    MoveRequest{.point = *m_point, .radius = kGetItemWalkRadius, .gait = kJogGait}));
            }
            brain.queueAction(TurnAction::toPoint(*object));
            brain.queueAction(AttackAction::onSelf(0, kGetItemSwingDelayMs));
            return GoalStatus::Stop;
        }
        const anim::Vec3 to = anim::subtract(*object, at);
        if (std::fabs(human::wrapAngle(human::headingOf(to) - brain.human().heading())) > kGetItemTurn) {
            brain.queueAction(TurnAction::toPoint(*object));
            return GoalStatus::Stop;
        }
        ++m_tries;
    }
    // 7. The walk to the point beside it.
    const int gait = pointDistance < kGetItemJogWithin ? kJogGait : m_gait;
    brain.queueAction(
        std::make_unique<MoveAction>(MoveRequest{.point = *m_point, .radius = kGetItemWalkRadius, .gait = gait}));
    return GoalStatus::Stop;
}

// ---- WarriorVandalStealGoal ----

void WarriorVandalStealGoal::start(Brain& brain) {
    if (Brain* leader = m_services->brain(m_leader); leader != nullptr) {
        if (Formation* formation = m_formations->of(*leader, true); formation != nullptr) {
            formation->join(brain);
        }
    }
    resume(brain);
}

void WarriorVandalStealGoal::resume(Brain& brain) {
    if (m_claimed) {
        if (WarriorStealTactic* tactic = stealTactic(brain); tactic != nullptr) {
            tactic->clusters().release(*m_claimed);
        }
        m_claimed.reset();
    }
    // The smash target or pane he went to strike (+0x10): its claim drops once DestroyItem is over.
    if (m_struck) {
        m_items->unclaim(*m_struck);
        m_struck.reset();
    }
}

void WarriorVandalStealGoal::end(Brain& brain) {
    if (Formation* formation = brain.following(); formation != nullptr) {
        formation->leave(brain);
    }
    brain.stopMove();
}

WarriorStealTactic* WarriorVandalStealGoal::stealTactic(const Brain& brain) {
    const Gang* gang = brain.gang();
    return gang != nullptr ? dynamic_cast<WarriorStealTactic*>(gang->tactic()) : nullptr;
}

GoalStatus WarriorVandalStealGoal::process(Brain& brain) {
    Brain* leader = m_services->brain(m_leader);
    // Coney stand-in (issue #644): with no leader the original keeps the goal (returns 0); Coney ends it.
    if (leader == nullptr || leader->human().outOfWorld()) {
        return GoalStatus::Done;
    }
    // Coney stand-in (issue #644): the original tests queued actions after the fetch, and goes on while a fidget
    // (held flag 0x100) runs.
    if (brain.actionCount() > 0) {
        return GoalStatus::Stop;
    }
    // The fetch: empty-handed under the gang's steal tactic, the object it hands him.
    WarriorStealTactic* tactic = stealTactic(brain);
    if (tactic != nullptr && m_items != nullptr && brain.human().script().heldObject == 0.0 &&
        brain.gang() != nullptr) {
        if (const std::optional<double> object =
                tactic->clusters().claim(brain, *m_items, brain.gang()->id(), brain.nowMs())) {
            m_claimed = object;
            if (brain.pushGoal(std::make_unique<GetItemGoal>(*object, kFetchGait, *m_items))) {
                return GoalStatus::Again;
            }
            tactic->clusters().release(*object);
            m_claimed.reset();
        }
    }
    // The wreck: a swing, a smash target or a pane. A goal it pushed starts in the same update; with only actions
    // queued, or busy winding up a throw, he stops for this one.
    if (m_items != nullptr && tryVandalise(brain, *leader)) {
        m_awayMs.reset();
        return brain.topGoal() == this ? GoalStatus::Stop : GoalStatus::Again;
    }
    // Too far from the leader for a while: back to his slot.
    const bool attacked = brain.gang() != nullptr && std::ranges::any_of(brain.gang()->members(), [](const Brain* m) {
                              return !m->attackSlots().empty();
                          });
    const float reach = attacked ? kVandalStealAttackedReach : kVandalStealLeaderReach;
    // Coney stand-in (issue #644): "the leader runs" (his +0x1a8 above 3) is not read.
    if (brain.distanceTo(*leader) > reach) {
        // Coney stand-in (issue #644): the original arms its 2 s deadline whenever it is 0 and clears it only after
        // a wreck; Coney times each spell away from the leader.
        if (!m_awayMs) {
            m_awayMs = brain.nowMs();
        }
        if (brain.nowMs() - *m_awayMs >= kVandalStealFollowDelayMs) {
            m_awayMs.reset();
            if (brain.pushGoal(std::make_unique<FollowFormationGoal>(kVandalStealSlotReach))) {
                return GoalStatus::Again;
            }
        }
        // Coney stand-in (issue #644): the original falls through to the action check and the turn.
        return GoalStatus::Stop;
    }
    m_awayMs.reset();
    // Near him: his heading every 4 s.
    if (brain.nowMs() >= m_nextTurnMs) {
        m_nextTurnMs = brain.nowMs() + kVandalStealTurnMs;
        const float heading = leader->human().heading();
        // Coney stand-in (issue #644): the original turns to a point ahead in his heading with no threshold.
        if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kCrewHeadingTurn) {
            brain.queueAction(TurnAction::toHeading(heading));
        }
    }
    return GoalStatus::Stop;
}

bool WarriorVandalStealGoal::tryVandalise(Brain& brain, Brain& leader) {
    // 1. Winding up a throw of what he holds: busy, nothing new.
    if (swingingHeld(brain.human())) {
        return true;
    }
    if (stealTactic(brain) == nullptr || brain.gang() == nullptr) {
        return false;
    }
    // 2. Something in hand: one he swings (a throwable, kind 4 or 6) or a breaking one (flag 0x10000); anything else
    //    stops the wreck here. With one he swings he lets go of his target and, unless a clip holds any flag, turns
    //    once to the nearest whole pane, then swings. **Coney stand-in**: the turn to the crowd ahead
    //    (`Gang_CountAhead`) and the lines are not built.
    const double held = brain.human().script().heldObject;
    const HeldUse use = held != 0.0 ? m_items->heldUse(held) : HeldUse::None;
    if (use == HeldUse::Swings) {
        {
            // Human_DropTarget from TryVandalise.
            const TargetCauseScope cause(brain, TargetCause::Order);
            brain.setTarget(nullptr);
        }
        if (brain.human().heldFlags() != 0) {
            return false;
        }
        if (!m_turnedToGlass) {
            const std::vector<PaneView> near = m_items->panes(brain.human().position(), kVandalReach);
            if (!near.empty()) {
                brain.queueAction(TurnAction::toPoint(near.front().centre));
                m_turnedToGlass = true;
                return true;
            }
        }
        brain.queueAction(AttackAction::onSelf(0, kVandalSwingDelayMs));
        m_turnedToGlass = false;
        return true;
    }
    if (use == HeldUse::Other) {
        return false;
    }
    // 3. Only on even updates from here.
    if ((brain.updates() & 1U) != 0) {
        return false;
    }
    // **Coney stand-in**: the original first sends him at a car within 15 m of the leader with a free spot
    // (`DestroyCarGoal_Push`, 0x002ddb40); Coney has no DestroyCar goal yet (issue #716).
    // 4. A smash target near him and the leader: one that is carried is fetched (unless what he holds breaks it
    //    where it stands), any other is struck where it stands, from the smaller of its sizes away.
    if (const std::optional<SmashTarget> target =
            findSmashTarget(brain, leader, *m_items, *m_services, kVandalReach, kVandalReach)) {
        const bool strikeHeld = use == HeldUse::Breaks && target->heldBreaks;
        if (!strikeHeld && (target->animSet == 4 || target->animSet == 5)) {
            // **Coney stand-in**: the original drops what he holds first (`Human_DropHeld`); ItemServices has no
            // drop, and the only object that can be in his hand here is a breaking one (HeldUse::Breaks), which
            // LevelItems::heldUse() never reports yet.
            return brain.pushGoal(std::make_unique<GetItemGoal>(target->handle, kFetchGait, *m_items));
        }
        m_struck = target->handle;
        return brain.pushGoal(std::make_unique<DestroyItemGoal>(target->handle, false, *m_items));
    }
    // 5. Now and then a pane near the leader.
    if (brain.nowMs() <= m_nextPaneMs) {
        return false;
    }
    m_nextPaneMs =
        brain.nowMs() + static_cast<std::uint64_t>(rollRange(brain.random(), static_cast<int>(kVandalPaneMinMs),
                                                             static_cast<int>(kVandalPaneMaxMs)));
    if (const std::optional<PaneView> pane = claimPane(leader, *m_items, kVandalReach)) {
        m_struck = pane->handle;
        return brain.pushGoal(std::make_unique<DestroyItemGoal>(pane->handle, true, *m_items));
    }
    return false;
}

// ---- WarriorStealTactic ----

WarriorStealTactic::WarriorStealTactic(double chief, ScriptServices& services, Formations& formations,
                                       ItemServices* items)
    : Tactic(kWarriorStealTacticType, {}), m_chief(chief), m_services(&services), m_formations(&formations),
      m_items(items) {}

void WarriorStealTactic::pickStore(const Brain& chief) {
    if (m_items == nullptr) {
        return;
    }
    const anim::Vec3 at = chief.human().position();
    const std::vector<StoreFlag> stores = m_items->stores();
    // The store it has, while the chief stays near it.
    if (m_store) {
        const auto kept =
            std::ranges::find_if(stores, [this](const StoreFlag& flag) { return flag.handle == *m_store; });
        if (kept != stores.end() && anim::length(anim::subtract(kept->position, at)) <= kStealStoreReach) {
            return;
        }
    }
    // Else the nearest within reach.
    const StoreFlag* nearest = nullptr;
    float nearestDistance = kStealStoreReach;
    for (const StoreFlag& flag : stores) {
        if (const float distance = anim::length(anim::subtract(flag.position, at)); distance <= nearestDistance) {
            nearest = &flag;
            nearestDistance = distance;
        }
    }
    if (nearest == nullptr) {
        m_store.reset();
        m_zone = -1;
        m_clusters.clear();
        return;
    }
    m_store = nearest->handle;
    m_zone = nearest->zone;
    const std::vector<LooseItem> owned = m_items->ownedItems(m_zone);
    m_clusters.build(owned);
}

void WarriorStealTactic::giveGoal(Brain& member) {
    static_cast<void>(
        member.pushTacticGoal(std::make_unique<WarriorVandalStealGoal>(*m_services, *m_formations, m_items, m_chief)));
}

void WarriorStealTactic::start(Gang& gang) {
    Brain* chief = m_services->brain(m_chief);
    if (chief == nullptr) {
        return;
    }
    pickStore(*chief);
    m_nextCheckMs = gang.owner().nowMs() + kStealStoreCheckMs;
    for (Brain* member : gang.members()) {
        if (member != chief && canAct(*member)) {
            member->flush();
            giveGoal(*member);
        }
    }
}

int WarriorStealTactic::update(Gang& gang) {
    if (gang.owner().nowMs() < m_nextCheckMs) {
        return 0;
    }
    m_nextCheckMs = gang.owner().nowMs() + kStealStoreCheckMs;
    if (const Brain* chief = m_services->brain(m_chief); chief != nullptr) {
        pickStore(*chief);
    }
    return 0;
}

void WarriorStealTactic::end(Gang& /*gang*/) { m_clusters.clear(); }

bool WarriorStealTactic::event(Gang& /*gang*/, Brain& member, const BrainEvent& event) {
    if (event.id == kEventViolence) {
        return true;
    }
    const bool again = event.id == kEventRegoal || (event.id == kEventGoalsAgain && event.value == 1);
    if (!again || member.handle() == m_chief || !canAct(member)) {
        return false;
    }
    giveGoal(member);
    return true;
}

} // namespace coney::ai
