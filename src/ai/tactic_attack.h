// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "ai/goal.h"
#include "ai/tactic.h"

// TacticAttack: the gang goes for its enemies. Each AI member gets threat response 2 and a melee goal; members left
// idle join the fight; the callback hears when no member has an enemy any more (9) and when a member dies (13). And
// the melee goal it gives, which takes the member to the nearest enemy and fights him there.
// Research: docs/research/ai.md#tactic-kinds, docs/references/bindings/ai.md#tacticattack

namespace coney::ai {

class Gangs;

/// The attack tactic's type id.
inline constexpr int kAttackTactic = 0x00;
/// The melee goal's type id (`Goal_Melee`).
inline constexpr GoalType kMeleeGoal = static_cast<GoalType>(0x08);
/// The tactic's codes: no member has an enemy, a member died (`TacticGetString`).
inline constexpr int kTacNoEnemies = 9;
inline constexpr int kTacMemberDied = 13;
/// How often the tactic checks for enemies, and sends idle members to the fight.
inline constexpr std::uint64_t kAttackEnemyCheckMs = 1000;
inline constexpr std::uint64_t kAttackIdleCheckMs = 3000;

/// The nearest living human, other than `brain`'s, whose gang is an enemy of `brain`'s (Gangs::enemies() either
/// way); null when none. **Coney choice**: how `Goal_Melee` picks its target is not traced.
[[nodiscard]] Brain* nearestGangEnemy(Brain& brain, Gangs& gangs);

/// The melee goal (type 8): **Coney's stand-in**, its code not being traced (docs/research/ai.md#goals). Each update it
/// takes the nearest gang enemy (nearestGangEnemy()) as an enemy and the target; done when there is none; beyond
/// kInReachShare of the far melee range it runs to him (MoveToHumanAction, 2 s at a time); within, it pushes the fight
/// goal, and takes over again when that ends.
class MeleeGoal final : public Goal {
  public:
    /// A melee goal over the gangs of `gangs` (which must outlive it).
    explicit MeleeGoal(Gangs& gangs) : Goal(kMeleeGoal), m_gangs(&gangs) {}

    [[nodiscard]] GoalStatus process(Brain& brain) override;

  private:
    Gangs* m_gangs;
};

/// Gives `brain` the melee goal unless it has one: what the attack tactic and its events do for a member.
/// @orig 0x002b75b8 Brain_Melee (unknown)
void giveMelee(Brain& brain, Gangs& gangs);

/// The attack tactic (type 0, vtable `0x00543320`).
/// @orig 0x003075c8 TacticAttack_Init (unknown)
class TacticAttack final : public Tactic {
  public:
    /// An attack calling `callback` (empty for none).
    explicit TacticAttack(std::string callback) : Tactic(kAttackTactic, std::move(callback)) {}

    /// Each AI member gets threat response 2 and the melee goal. **Coney choices**: the `PedReaction` exception and the
    /// gang's spot line are not built.
    /// @orig 0x00307fe0 TacticAttack_Start (unknown)
    void start(Gang& gang) override;
    /// Every kAttackIdleCheckMs, AI members with no goal get the melee goal; every kAttackEnemyCheckMs, 9 when no
    /// member has an enemy. **Coney choice**: the coordinated sub-tactics (`0x00307a10`, not traced) are not built.
    /// @orig 0x003081a8 TacticAttack_Process (unknown)
    [[nodiscard]] int update(Gang& gang) override;
    /// Damage (1) or an enemy added (`0xb`): an idle AI member melees; the gang's message 2 (a member died) fires 13.
    /// @orig 0x00308478 TacticAttack_Event (unknown)
    bool event(Gang& gang, Brain& member, const BrainEvent& event) override;

  private:
    std::uint64_t m_nextEnemyCheckMs = 0;
    std::uint64_t m_nextIdleCheckMs = 0;
};

} // namespace coney::ai
