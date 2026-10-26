// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/goal.h"
#include "ai/item_services.h"

// Smashing things: the DestroyItem goal that walks up to a world object or a glass pane and swings at it once, the
// object search that finds a world object worth smashing near the war chief (`Ai_FindObject` with the
// vandalisable-in-range filter) and the pane search that claims a glass pane for a gang (`Glass_ClaimPane`). The
// Warriors' wreck command uses all three (ai/steal_goals.h). Research: docs/research/crew-wreck.md (the smash test, the
// stand point), docs/research/ai.md#get-item

namespace coney::ai {

class Brain;
class ScriptServices;

/// DestroyItem swings once he stands within this distance of its stand point (standPoint()), metres in 3D (1.0
/// squared in the code, inclusive).
inline constexpr float kDestroyItemReach = 1.0F;
/// It turns to the object when more than this far off it, radians (15°).
inline constexpr float kDestroyItemTurn = 0.2617994F;
/// It gives up after this many walks.
inline constexpr int kDestroyItemMoves = 3;
/// Its walk arrives within this distance, metres, at a run (gait 4) from 2 m or more in 3D, else a walk (gait 2).
inline constexpr float kDestroyItemArrive = 0.4F;
inline constexpr float kDestroyItemRunFrom = 2.0F;
/// The swing: attack kind 0 on himself after this many ms.
inline constexpr std::int16_t kDestroyItemSwingDelayMs = 100;

/// An object the object search hands out is claimed for the searcher's gang this long, ms.
inline constexpr std::uint64_t kSmashClaimMs = 5000;
/// It skips an object a player stands nearer than this to, metres in 3D (for an AI searcher).
inline constexpr float kSmashPlayerClear = 1.0F;
/// The ray mask the object search tests its ray with (`Human_HasClearRayTo(..., 8)`).
inline constexpr std::uint32_t kSmashRayMask = 8;

/// A pane Glass_ClaimPane hands out is claimed for the gang this long, ms.
inline constexpr std::uint64_t kPaneClaimMs = 7000;
/// A pane counts as upright when its normal's height is at most this.
inline constexpr float kPaneUpright = 0.65F;
/// Its bottom must be at most this high above the searcher's feet, metres: the height of his bone 3 in his pose
/// (human `+0x548`). **Coney stand-in**: the 1.18 m read at runtime on a standing Cleon (PCSX2 2.9.129, level80's
/// "Let's Go" state), not his live pose.
inline constexpr float kPaneReachHeight = 1.18F;

/// `DestroyItemGoal_Push(human, object)` (type `0x2d`, vtable `0x00541e90`): walks up to a world object or a glass
/// pane and swings at it once. Each update, while no action runs: done when the object is gone or broken; else its
/// stand point (standPoint(): the object's own spot on the ground; the stand-off and direction the original passes
/// `Nav_FindPolygonUnder` are never read); within kDestroyItemReach of it (3D) he turns to the object when more than
/// kDestroyItemTurn off, then swings once (attack kind 0 on himself after kDestroyItemSwingDelayMs) and is done when
/// the swing has run; farther he walks there (gait 4, or 2 within kDestroyItemRunFrom; arriving within
/// kDestroyItemArrive), at most kDestroyItemMoves times. **Coney stand-ins**: the look at the object (500 ms), leaving
/// the fight stance, the object target the swing is aimed with (human `+0x128`; Coney's strike breaks what its shapes
/// touch) and the 30/70 % line `0x8e` / `0x8f` every 8 s are not built.
/// @orig 0x002dd0c8 DestroyItemGoal_Push (unknown)
/// @orig 0x002dd148 DestroyItemGoal_Init (unknown)
class DestroyItemGoal final : public Goal {
  public:
    /// Smashes `object` (a world object, or a pane when `pane`) through `items`, which must outlive it.
    DestroyItemGoal(double object, bool pane, ItemServices& items)
        : Goal(GoalType::DestroyItem), m_object(object), m_pane(pane), m_items(&items) {}

    /// The update (above).
    /// @orig 0x002dd210 DestroyItemGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The object it smashes; whether it has swung.
    [[nodiscard]] double object() const { return m_object; }
    [[nodiscard]] bool swung() const { return m_swung; }

  private:
    double m_object; // +0x10
    bool m_pane;
    ItemServices* m_items;
    int m_moves = 0;      // +0x14
    bool m_swung = false; // +0x18
};

/// `Ai_FindObject(15, searcher, ray, {ObjFilter_VandalisableInRange, leader, range}, claim, reach)`: the nearest
/// world object within `radius` of `searcher` that may be smashed (ItemServices::smashTargets()), lies within `range`
/// of `leader`, is not claimed by the searcher's gang, has no player (any of his peers) nearer than kSmashPlayerClear
/// to it (3D), has a clear ray from the searcher (mask kSmashRayMask) that crosses no intact pane, and has a stand
/// point (standPoint()) the searcher can walk to; it is claimed for his gang for kSmashClaimMs. Nothing when none
/// qualifies. **Coney stand-in**: the reach test (`Nav_CanReach`) is a route the planner finds
/// (RoutePlanner::walkDistance()).
/// @orig 0x0029d5f0 Ai_FindObject (unknown)
/// @orig 0x0029d558 ObjFilter_VandalisableInRange (unknown)
[[nodiscard]] std::optional<SmashTarget> findSmashTarget(Brain& searcher, const Brain& leader, ItemServices& items,
                                                         const ScriptServices& services, float radius, float range);

/// `Glass_ClaimPane(radius, human, ray 1, claim 1, reach 1)`: of the panes within `radius` of `human`
/// (ItemServices::panes(), nearest first), the first not claimed by his gang, not alarmed, upright (|normal z| at most
/// kPaneUpright), low enough (its bottom at most kPaneReachHeight above his feet), with a clear ray from him to its
/// centre that crosses glass (clearRayTo()'s result 2) and a stand point (standPoint()) he can walk to; it is claimed
/// for his gang for kPaneClaimMs. Nothing when none qualifies. **Coney stand-in**: the reach test as
/// findSmashTarget()'s.
/// @orig 0x0029d928 Glass_ClaimPane (unknown)
[[nodiscard]] std::optional<PaneView> claimPane(Brain& human, ItemServices& items, float radius);

/// The stand point of DestroyItem and of the object and pane searches: `Nav_FindPolygonUnder` (`0x00252410`) of the
/// object's own spot, which overwrites the stand-off its callers pass (f12) with its 10 m drop and never reads their
/// direction (a1), so every caller stands at the spot itself. The spot under the object at `human`'s feet height,
/// moved onto the navigation mesh when he has a planner (RoutePlanner::navPoint(); nothing when it finds none).
/// **Coney stand-in**: the drop onto marked ground is his own height, then the planner's point.
/// @orig 0x002dd210 DestroyItemGoal_Process (unknown)
[[nodiscard]] std::optional<anim::Vec3> standPoint(const Brain& human, anim::Vec3 object);

} // namespace coney::ai
