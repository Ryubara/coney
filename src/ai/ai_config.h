// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ai/brain.h"
#include "combat/power_class.h"
#include "scripting/script_bindings.h"

// What Coney's AI fighters take from the game's configuration scripts: the character class's brain type, health,
// attack table and damage table (`CfgChar`), the power class's AI fields (`CfgPowerClass`), the attack delays
// (`CfgAttackDelay`) and the base chance to block (`CfgBaseChanceToBlock`), read from the calls the scripts made
// (script::RecordedCalls). Without the disc's scripts the research's reference values stand in.
// Research: docs/research/ai.md#level99, docs/references/bindings/config.md

namespace coney::ai {

/// The character type Coney's fighters are (**Coney choice**): 58, a sparring Warrior of `level99` (`CombatWarriors`:
/// brain type 2, 1400 health, `DamageNormal` / `Att_Normal`).
inline constexpr int kFighterType = 58;
/// The power class Coney's fighters use: 40, the sparring Warriors' (classes 58-60) at runtime.
inline constexpr int kFighterPowerClass = 40;
/// Power class 40 as read at runtime for what an AI reads (block 0.2, 0.1 while hurt, attack delay factors 20 and 20,
/// counter 0.08): a 4 s wait after a 200 ms attack. **Coney choice**: its other fields are the street civilian's until
/// the disc's `CfgPowerClass` call gives them.
inline constexpr combat::PowerClass kWarriorPowerClass = [] {
    combat::PowerClass power = combat::kCivilianPowerClass;
    power.blockChance = 0.2F;
    power.hurtBlockChance = 0.1F;
    power.attackDelayFactor = 20.0F;
    power.attackDelayDownFactor = 20.0F;
    power.counterChance = 0.08F;
    return power;
}();

/// A fighter's class: what an AI human of one character type is.
struct FighterClass {
    int type = kFighterType;
    BrainType brain = BrainType::Gang; ///< From the class's behaviour byte.
    int health = 1400;                 ///< The class's `+0x116`.
    /// The class's damage table (`CfgChar`'s, × its damage scale), written over the human's Anim Range List; empty
    /// keeps the list's own.
    std::vector<std::int16_t> damage;
};

/// The configuration of Coney's AI fighters.
struct AiConfig {
    FighterClass fighter;                               ///< What a spawned fighter is.
    combat::PowerClass powerClass = kWarriorPowerClass; ///< Its power class.
    FightSettings settings;                             ///< Its attack table, the delays, the base block chance.
    std::size_t callsRead = 0;                          ///< Configuration calls taken from the scripts (0: none).
};

/// The configuration from `recorded` (the calls the configuration and level scripts made): the last `CfgChar` call of
/// `type`, the last `CfgPowerClass` call of `powerClass`, every `CfgAttackDelay` and the last `CfgBaseChanceToBlock`;
/// what the calls do not give keeps the reference values (attNormal(), kWarriorPowerClass, referenceAttackDelays()).
[[nodiscard]] AiConfig aiConfigFrom(const script::RecordedCalls& recorded, int type = kFighterType,
                                    int powerClass = kFighterPowerClass);

} // namespace coney::ai
