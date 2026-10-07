// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "core/game_random.h"

// What a new human carries: the money and the object `Human_Init` rolls from its type's `CfgChar` record and its
// category's `CfgCharClassAttribs` record, drawing from the game's random numbers.
// Research: docs/research/crimes.md#starting-money

namespace coney::characters {

/// One `CfgCharClassAttribs` record (game state + category × 16): the money range, the no-object and bonus chances
/// (percent) and the bonus multiplier.
struct ClassMoney {
    int min = 0;         ///< Byte `+0x08`, dollars.
    int max = 0;         ///< Byte `+0x09`, dollars.
    int noObject = 0;    ///< Byte `+0x10`: a first roll at or below it carries no object.
    int bonusChance = 0; ///< Byte `+0x11`: a second roll at or below it multiplies the money.
    float bonus = 1.0F;  ///< Float `+0x14`.
};

/// The `CfgChar` fields the roll reads.
struct CarryConfig {
    int category = 0;       ///< Byte `+0x11b` (argument 3): which ClassMoney record.
    bool altRecord = false; ///< Byte `+0x14b` (argument 13) is 1: category 4 then uses record 16.
    int dropChance = -1;    ///< `v16`, `+0xb4` (argument 16): -1 for the class range, else a chance and a cap.
    std::string object{};   ///< `str15`, `+0x16c` (argument 15): an object, a `grp_` group, or `none`.
};

/// What a human starts with.
struct StartingCarry {
    int money = 0;        ///< `+0x370`, 0-999.
    std::string object{}; ///< `+0x257`: `none` for nothing.
    int dropChance = 0;   ///< `+0x278`.
};

/// The category whose ClassMoney a type of `config` reads (16 for category 4 with the alternate byte).
[[nodiscard]] int moneyCategory(const CarryConfig& config);

/// Rolls a new human's carry from `config` and its category's `money`, drawing in the original's order from
/// `random`: the object roll, the money, the bonus roll, then (category 4) the brain's `Random_Int(100)`.
/// **Coney's stand-in**: a `grp_` object group gives no object (the groups and their own generator are not built).
/// @orig 0x00218008 Human_Init (unknown)
[[nodiscard]] StartingCarry rollStartingCarry(const CarryConfig& config, const ClassMoney& money, GameRandom& random);

} // namespace coney::characters
