// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "ai/goal.h"
#include "ai/tactic.h"

// TacticAttack: the gang goes for its enemies. Start gives every standing member the Melee goal and threat response
// 2; every second the tactic reports 9 to its callback once no member lists an enemy; every 3 s a member with nothing
// to do melees and goes over to the farthest busy mate; violence nearby sends the idle members at the side the leader
// picks; and a gang beaten below its flee share sends its fleeing role running. The team-up orders (Unit 3 on) are
// not built: no order ever runs.
// Research: docs/research/ai-reimpl.md#unit-2, docs/research/ai-code.md#t1-attack

namespace coney::ai {

class Gangs;

/// The attack tactic's type id.
inline constexpr int kAttackTactic = 0x00;
/// The melee goal's type id (`Goal_Melee`).
inline constexpr GoalType kMeleeGoal = GoalType::Melee;
/// The tactic's codes: no member has an enemy, a member died (`TacticGetString`).
inline constexpr int kTacNoEnemies = 9;
inline constexpr int kTacMemberDied = 13;
/// How often the tactic checks for enemies, and passes over its idle members, ms.
inline constexpr std::uint64_t kAttackEnemyCheckMs = 1000;
inline constexpr std::uint64_t kAttackIdleCheckMs = 3000;

/// The nearest living human, other than `brain`'s, whose gang is an enemy of `brain`'s (Gangs::enemies() either
/// way); null when none. **Coney's helper** for the story tactics' range tests.
[[nodiscard]] Brain* nearestGangEnemy(Brain& brain, Gangs& gangs);

/// Gives `brain` the Melee goal with no time limit unless one is on its stack: what Coney's story and scout tactics
/// give a member they send to fight (**Coney's helper**: their own pushers are not traced).
void giveMelee(Brain& brain);

/// The attack tactic (type 0, vtable `0x00543320`), without the team-ups.
class TacticAttack final : public Tactic {
  public:
    /// An attack calling `callback` (empty for none) with no time limit, made at game time `nowMs`: the enemy check is
    /// due kAttackEnemyCheckMs after, the idle pass kAttackIdleCheckMs after.
    /// @orig 0x003075c8 AttackTactic_Construct (unknown)
    explicit TacticAttack(std::string callback, std::uint64_t nowMs = 0);

    /// The start: the spacing (computeSpacing()), every standing member's Melee (giveMelee()), the cower clips (group
    /// kCowerClips: two for kind 6 with team-ups, else `gen_cower_plead` when the gang can flee and its kind's flee
    /// share is not 0) and, when the gang can flee and has none yet, the living count for the flight. **Coney
    /// stand-in**: the gang's spot line (`Gang_SaySpotLine(gang, 0x16)`) is not built.
    /// @orig 0x00307fe0 AttackTactic_Start (unknown)
    void start(Gang& gang) override;
    /// The end: the cower clips released under the condition start() installed them.
    /// @orig 0x003080c8 AttackTactic_End (unknown)
    void end(Gang& gang) override;
    /// One update: past the enemy check's time, a gang that has not fled reports 0 at the first member listing an
    /// enemy (ending the update there) or 9 with none; past the idle pass's time, each member with an empty stack gets
    /// the Melee goal and a HelpRespond goal toward the farthest mate whose stack is not empty. **Coney stand-in**: the
    /// team-up timer (`AttackTactic_StartSubTactic`, Unit 3) is not built. Returns 0 to carry on.
    /// @orig 0x003081a8 AttackTactic_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// An event to `member` (the human it concerns): a hit (1) or a new enemy (`0xb`) melees an own member with no
    /// fight goal (Brain::hasFightGoal()); a death (2) fires 13 unless the gang fled; a member down (18) checks the
    /// flight (checkFlee()); `0x13` and `0x16` give the Melee again (giveMelee()); violence (`0x14`) goes to
    /// onViolence(). Never uses the event (false).
    /// @orig 0x00308478 AttackTactic_OnEvent (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

    /// `+0x2c`, `+0x30`: the spacing interval and its base, which nothing in the executable reads.
    /// @orig 0x00307668 AttackTactic_ComputeSpacing (unknown)
    void computeSpacing(Gang& gang);
    [[nodiscard]] std::uint16_t spacing() const { return m_spacing; }
    [[nodiscard]] std::uint16_t spacingBase() const { return m_spacingBase; }
    /// Every standing member with no PedReaction goal (`0x6b`): threat response 2, popped to his goal base, the base
    /// marked and the Melee goal with no time limit on top. Players are included.
    /// @orig 0x00307788 AttackTactic_GiveMelee (unknown)
    static void giveMelee(Gang& gang);
    /// Violence nearby (event `0x14`): the leader picks a side (ai::pickSideInFight()); with one, each standing member
    /// with an empty stack melees, lists him as an enemy and takes him as the target (no slot forced), and goes over to
    /// help (ai::sendHelper()) unless a HelpRespond goal is above his goal base.
    /// @orig 0x00307878 AttackTactic_OnViolence (unknown)
    void onViolence(Gang& gang, const BrainEvent& event);
    /// A member down (event 18): when the gang can flee, noted a count (not kind 6 with team-ups), has a flee share,
    /// the threshold round(flee share × that count / 100) is not 0 and its standing, uncuffed members number at most
    /// that, every standing, uncuffed member of the fleeing role (kFleeRole) without a PedReaction already gets threat
    /// response 0, the Melee goal and a PedReaction away from his nearest enemy; the gang has fled once one was sent.
    /// It reruns on every event 18, fled or not. **Coney stand-ins**: the PedReaction's speech line 9 is not said;
    /// "standing" is ai::downOrDead(); "cuffed" is the script's arrested state.
    /// @orig 0x00307d40 GangTactic_CheckFlee (unknown)
    void checkFlee(Gang& gang);

    /// The game times the enemy check and the idle pass are next due after (`+0x20`, `+0x24`), ms.
    [[nodiscard]] std::uint64_t nextEnemyCheckMs() const { return m_nextEnemyCheckMs; }
    [[nodiscard]] std::uint64_t nextIdlePassMs() const { return m_nextIdlePassMs; }
    /// Whether the gang fled (`+0x32`).
    [[nodiscard]] bool fled() const { return m_fled; }

  private:
    std::uint64_t m_nextEnemyCheckMs; // +0x20
    std::uint64_t m_nextIdlePassMs;   // +0x24
    std::uint16_t m_spacing = 0;      // +0x2c
    std::uint16_t m_spacingBase = 0;  // +0x30
    bool m_fled = false;              // +0x32
};

/// `TacticAttack(gang, callback)`: gang `gang` takes a new attack tactic calling `callback`, made at the gangs' time.
/// Nothing for an id not in use. **Coney's**: the tactic pool (30) is not modelled, so it is never full.
/// @orig 0x00307548 Tactic_Attack (unknown)
void tacticAttack(Gangs& gangs, int gang, std::string callback);

} // namespace coney::ai
