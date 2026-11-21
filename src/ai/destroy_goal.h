// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/goal.h"
#include "ai/item_services.h"

// The Destroy goal the wreck tactic (TacticVandalize, ai/tactic_vandalize.h) gives each member: it waits a while, picks
// the next object of the tactic's object zone (or a glass pane), then fetches it to throw or walks up and smashes it
// (DestroyItemGoal, GetItemGoal), and with a weapon in hand swings it. Research: docs/research/ai-goals.md#goal-destroy

namespace coney::ai {

class Brain;

/// The Destroy goal swings at most this many times at one target before it gives it up (`+0x28` above 2).
inline constexpr int kDestroyTries = 3;
/// The wait it draws between targets runs from the tactic's delay to the delay + this, ms (`Random_IntRange`).
inline constexpr std::uint32_t kDestroyWaitSpreadMs = 100;
/// An object it picks within the leader's radius is claimed for his gang this long, ms.
inline constexpr std::uint64_t kDestroyClaimMs = 5000;
/// It looks for a glass pane within this distance, metres (`Glass_ClaimPane(15, ...)`).
inline constexpr float kDestroyPaneRadius = 15.0F;
/// With something to swing in hand it first turns to the nearest pane within this distance, metres (900 squared).
inline constexpr float kDestroyTurnRadius = 30.0F;
/// Its swing: attack kind 0 on himself after this many ms.
inline constexpr std::int16_t kDestroySwingDelayMs = 0x7d;

/// `DestroyGoal_Init(range, goal, brain, zone, wait, cars)` (type `0x5b`, vtable `0x00541710`). Each update, unless
/// actions are queued or he is winding up a swing of what he holds:
/// 1. After a swing, or a fetch he came back from empty-handed, the target is let go (its claim dropped) and a new
///    wait drawn (`wait` to `wait` + kDestroyWaitSpreadMs).
/// 2. With no target he waits that long, then picks one (pickTarget()) and clears its tries; with no wait armed he
///    arms one.
/// 3. A target he has tried kDestroyTries times is let go.
/// 4. With something in hand: one he swings (HeldUse::Swings) turns him once to the nearest pane within
///    kDestroyTurnRadius, then swings it (attack kind 0 on himself after kDestroySwingDelayMs); a breaking one goes on
///    to step 5; anything else stops him.
/// 5. A pane, or an object a held breaking object strikes where it stands, or any object not carried, gets
///    DestroyItemGoal; a carried one (anim set 4 or 5) GetItemGoal (a run, gait 4). Each push counts a try.
///
/// **Coney stand-ins**: cars (`TacticVandalizeCars`, `DestroyCarGoal`, issue #716), the fight stance (issue #959),
/// the pick-up of a breaking object within 8 m while it waits (`DestroyGoal_TryPickUp`: Coney's objects carry no
/// breaking flag `0x10000`, as LevelItems::heldUse() says), dropping what he holds (ItemServices has no drop), the
/// turn to the crowd ahead before a swing (`Gang_CountAhead`), the 30/70 % taunt `0x8e` / `0x8f` every 8 s, and
/// the brain byte `+0x265` saved at the start and put back at the end (not identified) are not built. Nor is
/// `DestroyGoal_IsWaiting` (`0x002cda28`), whose one caller is the event-23 cheer (issue #1571), which ports it.
class DestroyGoal final : public Goal {
  public:
    /// Wrecks zone `zone` through `items` (which must outlive it), waiting `waitMs` between targets; with
    /// `leaderRange` 0 or more it leaves objects farther than that from the gang's leader.
    /// @orig 0x002cd4f8 DestroyGoal_Init (unknown)
    DestroyGoal(int zone, std::uint32_t waitMs, float leaderRange, ItemServices& items)
        : Goal(GoalType::Destroy), m_leaderRange(leaderRange), m_waitMs(waitMs), m_zone(zone), m_items(&items) {}

    /// Drops his target.
    /// @orig 0x002cda68 DestroyGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Back from a fetch with nothing in hand, it lets that target go at its next update.
    /// @orig 0x002cdae0 DestroyGoal_Resume (unknown)
    void resume(Brain& brain) override;
    /// Drops the claim on its target, and his target.
    /// @orig 0x002cdb30 DestroyGoal_End (unknown)
    void end(Brain& brain) override;
    /// The update (above).
    /// @orig 0x002cdbc0 DestroyGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The object or pane it goes for; nothing between targets.
    [[nodiscard]] std::optional<double> target() const { return m_target; }

  private:
    // The next target: the zone's nearest object, or every other pick a pane (above all near that object).
    // @orig 0x002cd698 DestroyGoal_PickTarget (unknown)
    void pickTarget(Brain& brain);
    // Lets the target go: its claim dropped and the target forgotten.
    // @orig 0x002cd640 DestroyGoal_ReleaseTarget (unknown)
    void releaseTarget();
    // Draws the wait before the next target.
    // @coney-only helper: the wait DestroyGoal_Process (0x002cdbc0) draws, compared there
    void armWait(Brain& brain);

    float m_leaderRange;             // +0x2c
    std::uint32_t m_waitMs;          // +0x30
    int m_zone;                      // +0x32
    ItemServices* m_items;           //
    std::optional<double> m_target;  // +0x20
    bool m_targetPane = false;       // whether +0x20 is a glass pane
    std::uint64_t m_waitUntilMs = 0; // +0x24
    int m_tries = 0;                 // +0x28
    bool m_letGo = false;            // +0x36
    bool m_fetching = false;         // +0x37
    bool m_turnedToGlass = false;    // +0x38
    std::uint8_t m_picks = 0;        // +0x39
};

} // namespace coney::ai
