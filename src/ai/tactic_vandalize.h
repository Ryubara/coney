// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/story_tactics.h"

// TacticVandalize: the gang wrecks the objects of an object zone (each member a DestroyGoal, ai/destroy_goal.h) and
// calls back 1 once the zone has nothing left, or earlier when the members are hurt, see the player, spot an enemy or
// are attacked. Research: docs/research/ai-code.md#t2-vandalize, docs/research/ai-gangs.md#tactic-kinds

namespace coney::ai {

/// The wreck tactic checks its zone every this long, ms.
inline constexpr std::uint64_t kVandalizeCheckMs = 3000;
/// A vandal hit by a stranger while not fighting melees for this long (`Goal_Melee`), ms, behind a fight goal of
/// kVandalizeFightMs.
inline constexpr int kVandalizeMeleeMs = 8000;
inline constexpr int kVandalizeFightMs = 4000;

/// `TacticVandalize(gang, zone, delay, callback, leaderRange)` (type `0x1c`, vtable `0x00544040`): at its start every
/// AI member who is up gets a DestroyGoal on top of his own goals (zone, delay, leaderRange). Every
/// kVandalizeCheckMs it returns 1 once the zone has nothing left to wreck (ItemServices::zoneHasObjects()). Events:
/// damage (1) from a stranger makes that member fight back (onDamage()) and fires 5; seeing the player (10) fires 4,
/// an enemy spotted (11) 3 and an attack warning (16) 6; 19 and 22 give the goals again; a fight seen (20) is taken.
/// **Coney stand-ins**: the cars of `TacticVandalizeCars` (issue #716), the vandal brain byte `+0x28d` (not
/// identified) and event 23 (an object broken: the waiting members' cheer, `VandalizeTactic_OnObjectBroken`, and code
/// 16 for an object of the zone) are not built, because Coney sends no event 23 yet.
/// @orig 0x0031e3b8 VandalizeTactic_Construct (unknown)
class VandalizeTactic final : public StoryTactic {
  public:
    /// The wreck of `call` (its zone, delay, callback and range) through `services` (its items: none, and the members
    /// get no goals).
    VandalizeTactic(const script::TacticCall& call, const TacticServices& services) : StoryTactic(call, services) {}

    /// Gives the goals (giveGoals()) and arms the first check.
    /// @orig 0x0031e5f8 VandalizeTactic_Start (unknown)
    void start(Gang& gang) override;
    /// The zone check (above).
    /// @orig 0x0031ea00 VandalizeTactic_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// The events (above).
    /// @orig 0x0031eaf0 VandalizeTactic_OnEvent (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

  private:
    // Each AI member who is up: popped to his goal base, the base marked again and a DestroyGoal pushed.
    // @orig 0x0031e4f0 VandalizeTactic_GiveGoals (unknown)
    void giveGoals(Gang& gang);
    // Damage to `member` from `attacker`, not a friend: fighting already, he takes the attacker as enemy and target;
    // otherwise he pops down to his DestroyGoal, takes the attacker as enemy and pushes a Melee goal of
    // kVandalizeMeleeMs and a fight goal of kVandalizeFightMs.
    // @orig 0x0031e620 VandalizeTactic_OnDamage (unknown)
    static void onDamage(Brain& member, Brain& attacker);

    std::uint64_t m_nextCheckMs = 0; // +0x38
};

} // namespace coney::ai
