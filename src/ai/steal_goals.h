// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "ai/goal.h"
#include "ai/item_services.h"
#include "ai/tactic.h"
#include "animation/anim_math.h"

// The Warriors' steal / wreck command (command 5): its tactic, which near a store front clusters the store's owned
// objects and hands them out, the WarriorVandalSteal goal each Warrior runs under it, and the GetItem goal that walks
// to an object and picks it up. Taking an owned object is a theft the level reports (Item_TakeOwned, message 7 to the
// volume boxes round it: gamemodes/level_items.h). Research: docs/research/ai-code.md#t2-warrior-steal,
// docs/research/ai.md#warrior-steal, docs/research/ai-goals.md#goal-warrior-vandal-steal,
// docs/research/ai.md#get-item

namespace coney::ai {

class Formations;
class ScriptServices;

/// The steal tactic's type id.
inline constexpr int kWarriorStealTacticType = 0x26;
/// The store front must stand within this distance of the chief, metres.
inline constexpr float kStealStoreReach = 12.0F;
/// The tactic picks its store again every this long, ms.
inline constexpr std::uint64_t kStealStoreCheckMs = 2000;
/// The clusters' radius (`Points_Cluster` given 1.0), metres.
inline constexpr float kStealClusterRadius = 1.0F;
/// The cluster records: 20 entries of ten handles (`0x006eb318`).
inline constexpr std::size_t kStealClusters = 20;
inline constexpr std::size_t kStealClusterObjects = 10;
/// An object handed out is claimed for the gang this long, ms.
inline constexpr std::uint64_t kStealClaimMs = 5000;
/// It is handed out only with no enemy within this distance of it, metres.
inline constexpr float kStealEnemyClear = 1.5F;

/// GetItem's reach: within this distance in plan of the object it picks it up, metres.
inline constexpr float kGetItemReach = 1.5F;
/// It gives up after this many tries.
inline constexpr int kGetItemTries = 11;
/// Its walk ends this near its point, metres; within kGetItemJogWithin of it the walk jogs (gait 3).
inline constexpr float kGetItemWalkRadius = 0.36F;
inline constexpr float kGetItemJogWithin = 3.0F;

/// WarriorVandalSteal keeps within this distance of the leader (kVandalStealAttackedReach while his gang is attacked),
/// metres, walking back to its slot (FollowFormation, kVandalStealSlotReach) after kVandalStealFollowDelayMs away.
inline constexpr float kVandalStealLeaderReach = 3.0F;
inline constexpr float kVandalStealAttackedReach = 15.0F;
inline constexpr float kVandalStealSlotReach = 0.75F;
inline constexpr std::uint64_t kVandalStealFollowDelayMs = 2000;
/// It turns to the leader's heading every this long, ms.
inline constexpr std::uint64_t kVandalStealTurnMs = 4000;

/// One cluster record: the objects within kStealClusterRadius of each other, and how many Warriors were sent to it.
struct StealCluster {
    anim::Vec3 centre{};
    std::vector<double> objects;
    int users = 0;
};

/// The steal tactic's clusters of a store's owned objects and the gang's claims on them.
class StealClusters {
  public:
    /// Clusters `items` (`Points_Cluster`, 1.0): **Coney stand-in**, as the clustering itself is not on the page:
    /// each object joins the first cluster whose centre (its first object) lies within kStealClusterRadius, else
    /// starts one, at most kStealClusters of kStealClusterObjects objects. The claims are kept.
    /// @orig 0x00320c68 WarriorStealTactic_ClusterZone (unknown)
    void build(std::span<const LooseItem> items);
    /// Forgets the clusters (the tactic's end, or no store).
    /// @orig 0x003217a0 StealClusters_Reset (unknown)
    void clear() { m_clusters.clear(); }
    /// The clusters.
    [[nodiscard]] const std::vector<StealCluster>& clusters() const { return m_clusters; }

    /// An object for `human` to steal: walks the clusters (**Coney stand-in**: nearest centre first, for the sort
    /// `0x00433fd8`) and takes each one's nearest object still there, held by nobody and not claimed by `gang`; when
    /// none of `human`'s enemies stands within kStealEnemyClear of it, it is claimed for `gang` until `nowMs` +
    /// kStealClaimMs, the cluster's users go up and it is returned. Nothing when no object qualifies.
    /// @orig 0x00321540 WarriorSteal_ClaimObject (unknown)
    [[nodiscard]] std::optional<double> claim(const Brain& human, const ItemServices& items, int gang,
                                              std::uint64_t nowMs);
    /// Drops the claim on `object` (the goal's Resume releasing what it was fetching).
    void release(double object) { m_claims.erase(object); }
    /// Whether `object` is claimed by `gang` at `nowMs`.
    [[nodiscard]] bool claimed(double object, int gang, std::uint64_t nowMs) const;

  private:
    // A claim: the gang and when it runs out (object `+0xec`, `+0xf0`).
    struct Claim {
        int gang = 0;
        std::uint64_t untilMs = 0;
    };
    std::vector<StealCluster> m_clusters;
    std::map<double, Claim> m_claims;
};

/// `Goal_GetItem(human, kind, object)` (type `0x2c`): walks to the object and picks it up. Done when the object is
/// gone, when he (or anyone) holds it, after the pick-up, after a failed walk or after kGetItemTries tries. Within
/// kGetItemReach in plan of it he starts the pick-up (ItemServices::startPickUp(), a try); farther he walks to the
/// standing point beside it (gait 3 within kGetItemJogWithin, else the goal's), arriving within kGetItemWalkRadius.
/// **Coney stand-ins**: dropping a held weapon at Start, the look at the object, an enemy holding it (the fight or the
/// engage), clearing the way (kind 17), the ray test and its cross press, and the wait on held flags are not built;
/// the standing point is the planner's navigation point under the object at his height (RoutePlanner::navPoint()),
/// or the object's own place without a planner.
/// @orig 0x002dc7a0 GetItemGoal_Init (unknown)
class GetItemGoal final : public Goal {
  public:
    /// Fetches `object` through `items` (which must outlive it), walking at `gait`.
    GetItemGoal(double object, int gait, ItemServices& items)
        : Goal(GoalType::GetItem), m_object(object), m_gait(gait), m_items(&items) {}

    /// Raises the turn boost by 1, drops the target, then resume().
    /// @orig 0x002dc7f8 GetItemGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Finds the standing point beside the object; none when it is gone.
    /// @orig 0x002dc8b0 GetItemGoal_Resume (unknown)
    void resume(Brain& brain) override;
    /// Puts the turn boost back.
    /// @orig 0x002dc9d8 GetItemGoal_End (unknown)
    void end(Brain& brain) override;
    /// The update (above).
    /// @orig 0x002dc9f8 GetItemGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The object it fetches, and the tries so far.
    [[nodiscard]] double object() const { return m_object; }
    [[nodiscard]] int tries() const { return m_tries; }

  private:
    double m_object;
    int m_gait;
    ItemServices* m_items;
    std::optional<anim::Vec3> m_point; // +0x10, valid +0x32
    int m_tries = 0;                   // +0x2c
    int m_savedTurnBoost = 0;          // +0x30
    bool m_picked = false;             // +0x31
};

class WarriorStealTactic;

/// `WarriorVandalSteal(leader)` (type `0x83`, vtable `0x005404b0`): what each Warrior runs under the steal / wreck
/// command. Empty-handed under the gang's steal tactic, he takes the object the tactic hands him
/// (StealClusters::claim()) and fetches it (GetItemGoal). Otherwise, beyond kVandalStealLeaderReach of the leader
/// (kVandalStealAttackedReach while his gang is attacked) for kVandalStealFollowDelayMs, he walks back to his slot in
/// the leader's formation (FollowFormationGoal, kVandalStealSlotReach); near him he turns to the leader's heading
/// every kVandalStealTurnMs. **Coney stand-ins**: the wreck with no store (smash targets, glass, cars, the swing and
/// its lines), the wait of 500 ms × his place in the gang, the fight stance and the turn to an enemy while his gang
/// is attacked, the fidgets and "the leader runs" are not built; the fetch's gait (4) is not on the page.
/// @orig 0x002bec58 WarriorVandalStealGoal_Init (unknown)
/// @orig 0x002bee78 WarriorVandalStealGoal_TryFetch (unknown)
class WarriorVandalStealGoal final : public Goal {
  public:
    /// The gait of its fetch (**Coney stand-in**).
    static constexpr int kFetchGait = 4;

    /// Steals for the human with handle `leader`, found through `services`, in `formations`, with `items` (all must
    /// outlive it).
    WarriorVandalStealGoal(ScriptServices& services, Formations& formations, ItemServices* items, double leader)
        : Goal(GoalType::WarriorVandalSteal), m_services(&services), m_formations(&formations), m_items(items),
          m_leader(leader) {}

    /// Joins the leader's formation, then resume().
    /// @orig 0x002becb8 WarriorVandalStealGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Releases the object it was sent for and forgets it.
    /// @orig 0x002bed30 WarriorVandalStealGoal_Resume (unknown)
    void resume(Brain& brain) override;
    /// Leaves the formation.
    /// @orig 0x002bee08 WarriorVandalStealGoal_End (unknown)
    void end(Brain& brain) override;
    /// The update (above).
    /// @orig 0x002bf748 WarriorVandalStealGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The leader's handle.
    [[nodiscard]] double leader() const { return m_leader; }

  private:
    // The gang's steal tactic, when his gang runs one; null otherwise.
    [[nodiscard]] static WarriorStealTactic* stealTactic(const Brain& brain);

    ScriptServices* m_services;
    Formations* m_formations;
    ItemServices* m_items;
    double m_leader;
    std::optional<double> m_claimed;       // +0x10
    std::optional<std::uint64_t> m_awayMs; // since when he is too far from the leader
    std::uint64_t m_nextTurnMs = 0;
};

/// The steal / wreck command's tactic (command 5, type `0x26`, vtable `0x005441c0`): every 2 s it picks the store
/// front (a flag of activity 14) within kStealStoreReach of the chief, keeping the one it has while the chief stays
/// that near; the store's zone's owned objects are clustered (StealClusters). Each living member that is not the chief
/// and not a player is given WarriorVandalStealGoal. It never ends by itself; events 19 and 22 (with 1) give that
/// member his goal again and 20 is taken. **Coney stand-ins**: the reach test of the store (`Nav_CanReach`), dogs'
/// AvoidEnemies (class 221), the two-player rule, the idle fidget clips and the answering line (`steal_resp` 141,
/// `vandal_resp` 140, said only after an unforced order's line) are not built.
/// @orig 0x00320b60 WarriorStealTactic_Create (unknown)
/// @orig 0x00320be0 WarriorStealTactic_Init (unknown)
/// @orig 0x00320d30 WarriorStealTactic_GiveGoals (unknown)
/// @orig 0x00320df8 WarriorStealTactic_GiveMemberGoal (unknown)
class WarriorStealTactic final : public Tactic {
  public:
    /// The tactic for `chief`'s crew, with the level's brains, formations and objects (`items` null: no store is
    /// ever found); all must outlive it.
    WarriorStealTactic(double chief, ScriptServices& services, Formations& formations, ItemServices* items);
    /// Picks the store and gives the members their goals.
    /// @orig 0x003211a8 WarriorStealTactic_Start (unknown)
    void start(Gang& gang) override;
    /// Every kStealStoreCheckMs picks the store again; always 0.
    /// @orig 0x00321268 WarriorStealTactic_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// Clears the clusters.
    /// @orig 0x00321240 WarriorStealTactic_End (unknown)
    void end(Gang& gang) override;
    /// Events 19, 22 and 20 (above).
    /// @orig 0x00321328 WarriorStealTactic_OnEvent (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

    /// The store front's flag it steals from; nothing for none.
    [[nodiscard]] std::optional<double> store() const { return m_store; }
    /// Its store's object zone, −1 for none.
    [[nodiscard]] int zone() const { return m_zone; }
    /// The clusters and the gang's claims.
    [[nodiscard]] StealClusters& clusters() { return m_clusters; }
    [[nodiscard]] const StealClusters& clusters() const { return m_clusters; }

  private:
    // Keeps the store while the chief is within kStealStoreReach of it; else the nearest within that reach, whose
    // zone's owned objects are clustered; none: zone −1.
    // @orig 0x00321028 WarriorStealTactic_PickStore (unknown)
    void pickStore(const Brain& chief);
    // Gives `member` his goal above his goal base.
    void giveGoal(Brain& member);

    double m_chief;
    ScriptServices* m_services;
    Formations* m_formations;
    ItemServices* m_items;
    std::optional<double> m_store; // +0x20
    int m_zone = -1;               // +0x28
    std::uint64_t m_nextCheckMs = 0;
    StealClusters m_clusters;
};

} // namespace coney::ai
