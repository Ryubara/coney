// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/game_random.h"

// What a human leaves on the ground when his health runs out (wounded or knocked out): the pocket's items, a cop's
// power cuffs, the object he carries, a cop's key or his money, as `Human_DropCarried` decides it. The decision is
// here; the play mode makes the objects (LevelPickups::dropCarried()).
// Research: docs/research/crimes.md#down-drop

namespace coney::human {

/// The class record a drop reads (`CharClass_Get(+0xcc)`): the class's `CfgChar` `+0xb4` and its category's
/// `CfgCharClassAttribs` money range and share (record game state + category × 16, the category not remapped).
struct DropClass {
    int moneyMin = 0;        ///< Record byte `+0x08`, dollars (a player's loss).
    int moneyMax = 0;        ///< Record byte `+0x09`.
    float moneyShare = 0.0F; ///< Record float `+0x0c`: the share of his money a civilian drops.
    int carriedChance = -1;  ///< `CfgChar` `+0xb4`: 0 lets a cop drop a key.
};

/// Everything `Human_DropCarried` reads of the human, his brain and the game.
struct DropInput {
    /// The brain's type (`+0x04`, ai::BrainType's numbers: 1 a cop, 4 a civilian, 5 a dealer); nothing without a
    /// brain, which drops nothing.
    std::optional<int> brainType;
    int pedType = 0;            ///< The brain's `+0x26c`: 5 drops the pocket round him as a dealer does.
    bool player = false;        ///< He has a player number (`+0x1b0` is not -1).
    int pocketItem = 0;         ///< `+0x250`: an inventory item, 0 for none.
    int pocketCount = 0;        ///< `+0x254`, a signed byte.
    std::string pocketObject;   ///< The pocket item's object (its inventory entry's name).
    int cuffs = 0;              ///< `Human_GetCuffCount`: a cop always 1, another AI `+0x378`, a player item 5.
    std::string carried;        ///< `+0x257`: the object he carries; `none` (or empty, Coney's default) for none.
    int carriedChance = 0;      ///< `+0x278`: -1 keeps an AI's object.
    int money = 0;              ///< `+0x370`.
    DropClass dropClass{};      ///< His class's record.
    int playerMoney = 0;        ///< A player's inventory money (item 2).
    int carriedPrice = 0;       ///< A player's carried object's type `+0x5a`, a signed byte.
    float moneyScale = 1.0F;    ///< Game state `+0x380`, the money a player's carried object costs per price unit.
    bool gameMode = false;      ///< Game state `+0x158` is not 0.
    bool cuffsUnlocked = false; ///< Unlock (6, 14).
    bool keysUnlocked = false;  ///< Unlock (6, 13).
};

/// One object a drop makes.
struct DroppedObject {
    std::string type;          ///< The object type (`dyn_money`, `dyn_powercuffs`, the carried object, ...).
    std::uint32_t money = 0;   ///< A `dyn_money`'s dollars (`+0x124`); 0 for anything else.
    int removalItem = 0;       ///< The inventory item whose duration is its removal time; 0 for none.
    std::optional<int> sector; ///< The neighbour sector it goes 1 m out into; nothing for at his feet.
};

/// What a drop came to: the objects, in the order made, and what the human is left with.
struct CarriedDrop {
    std::vector<DroppedObject> objects;
    int money = 0;            ///< His money afterwards.
    std::string carried;      ///< His carried object afterwards.
    int playerMoneyTaken = 0; ///< What a player's inventory loses (item 2, notifying).
};

/// Whether neighbour sector `k` round the human is blocked (ai::Sectors::blocked()).
using SectorBlocked = std::function<bool(int k)>;

/// Item ids the drop names.
inline constexpr std::string_view kNoObject = "none";
inline constexpr std::string_view kMoneyObject = "dyn_money";
inline constexpr std::string_view kPowerCuffsObject = "dyn_powercuffs";
inline constexpr std::string_view kKeyObject = "dyn_key";

/// Decides what a human whose health ran out drops, drawing from `random`'s gameplay stream: `Human_OnHealthOut`'s
/// `Human_DropCarried(human, 1, 1)`. In order: the pocket's items (a dealer's round him in the free sectors
/// `blocked` tells, then nothing else unless he is a dealer or of ped type 5); a cuff holder's power cuffs (a 51 in
/// 101 draw, in the game mode or with the unlock); the carried object (an AI's unless its chance is -1; a player's
/// on a 50 in 101 draw, at a price in money); a cop's key (class `+0xb4` 0 and the unlock); else his money: a
/// civilian's share (at least $1), another AI's whole, a player's class range from his inventory.
/// @orig 0x00232c60 Human_DropCarried (unknown)
[[nodiscard]] CarriedDrop dropCarried(const DropInput& input, GameRandom& random, const SectorBlocked& blocked);

} // namespace coney::human
