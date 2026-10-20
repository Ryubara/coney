// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/steal_goals.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "ai/brain.h"
#include "ai/crew.h"
#include "ai/formations.h"
#include "ai/gangs.h"
#include "ai/move_action.h"
#include "ai/route_planner.h"
#include "ai/script_services.h"
#include "ai/turn_action.h"
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
    // 2. His pick-up under way: wait for its take.
    if (brain.human().pickingUp()) {
        m_picked = true;
        return GoalStatus::Stop;
    }
    // 3. Someone holds it (he himself among them): done.
    if (m_items->holder(m_object)) {
        return GoalStatus::Done;
    }
    // 4. Nobody holds it: done once picked; wait for queued actions; give up without a point, after a failed walk or
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
    // 5. Within reach: the pick-up, a try.
    if (planDistance(at, *object) <= kGetItemReach) {
        ++m_tries;
        if (m_items->startPickUp(brain, m_object)) {
            m_picked = true;
        }
        return GoalStatus::Stop;
    }
    // 6. The walk to the point beside it.
    const int gait = planDistance(at, *m_point) <= kGetItemJogWithin ? kJogGait : m_gait;
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
    if (leader == nullptr || leader->human().outOfWorld()) {
        return GoalStatus::Done;
    }
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
    // Too far from the leader for a while: back to his slot.
    const bool attacked = brain.gang() != nullptr && std::ranges::any_of(brain.gang()->members(), [](const Brain* m) {
                              return !m->attackSlots().empty();
                          });
    const float reach = attacked ? kVandalStealAttackedReach : kVandalStealLeaderReach;
    if (brain.distanceTo(*leader) > reach) {
        if (!m_awayMs) {
            m_awayMs = brain.nowMs();
        }
        if (brain.nowMs() - *m_awayMs >= kVandalStealFollowDelayMs) {
            m_awayMs.reset();
            if (brain.pushGoal(std::make_unique<FollowFormationGoal>(kVandalStealSlotReach))) {
                return GoalStatus::Again;
            }
        }
        return GoalStatus::Stop;
    }
    m_awayMs.reset();
    // Near him: his heading every 4 s.
    if (brain.nowMs() >= m_nextTurnMs) {
        m_nextTurnMs = brain.nowMs() + kVandalStealTurnMs;
        const float heading = leader->human().heading();
        if (std::fabs(human::wrapAngle(heading - brain.human().heading())) > kCrewHeadingTurn) {
            brain.queueAction(TurnAction::toHeading(heading));
        }
    }
    return GoalStatus::Stop;
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
