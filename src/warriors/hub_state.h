// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

// The game-state fields and globals the hub's scripts (`level95` and the configuration it reaches) set: the workout's
// tuning and callbacks, the context actions' reach, the turf-invasion and combat-walk switches, the loot's value, the
// script flags beyond the saved 128, the sound matrix and the sprite batches. Each field cites where the original keeps
// it. GameState holds one (warriors/game_state.h); none of them is reset with a level, as in the original. Research:
// docs/references/bindings/story.md#level95, docs/references/bindings/config.md

namespace coney {

/// The context action kinds `CfgActionDistance` sets (0 a handcuffed human ... 5 objects with their own text).
inline constexpr std::size_t kActionKinds = 6;

/// The workout's game-wide tuning (`CfgWorkoutParams`) and callbacks (`HuSetWorkoutCallbacks`).
struct WorkoutSettings {
    /// `0x00510690`, `0x005106a0`, `0x005106b0`: three 3-float tables; the first is the pump's step per phase.
    std::array<std::array<float, 3>, 3> rates{};
    /// `0x005106ac`: the fourth argument, read as a boolean, so 0 or 1.
    float factor = 0.0F;
    /// `0x0051023c`, `0x00510234`, `0x00510238`: the Lua functions called with the human's handle when a workout
    /// begins, at each repetition and when it ends; empty for none.
    std::string onStart;
    std::string onRep;
    std::string onEnd;
};

/// What the hub's scripts set.
struct HubState {
    WorkoutSettings workout;
    /// `CfgActionDistance` (`0x00514878`): each action kind's reach, stored squared. **Coney choice** before any call
    /// (the static values are not on the page): `global.lua`'s 2, 1.1, 2, 2, 1.75 and 1.5 m. **Coney stand-in**: Coney
    /// has no context actions yet, so the reaches are only kept.
    std::array<float, kActionKinds> actionDistanceSquared{4.0F, 1.21F, 4.0F, 4.0F, 3.0625F, 2.25F};
    /// `CfgEnableTurfInvasion` (`+0x56e1`). **Coney stand-in**: the turf invasion is not built, so it is only kept.
    bool turfInvasion = true;
    /// `CfgPowerupPickup` (`+0x56e5`): with it off, a player at full health walks past a flash
    /// (docs/research/player-state.md#walk-over). The level set-up sets it on.
    bool powerupPickup = true;
    /// `CfgPlayerCombatWalkOnly` (`0x0051031c`). **Coney stand-in**: its five readers are not on the page, so it is
    /// only kept.
    bool playerCombatWalkOnly = false;
    /// `CfgObjectValueMod` (`+0x380`): the stolen loot's value factor. **Coney stand-in**: Coney's players pick up no
    /// loot yet, so it is only kept. **Coney choice** before any call: 1.
    float objectValueFactor = 1.0F;
    /// `SetLUASaveDataBool` past flag 128: the original writes them beyond the saved words (`W_GameState + 0x572c`),
    /// into fields the page does not name; Coney keeps them here, unsaved.
    std::map<int, bool> unsavedFlags;
    /// `SndLoadMatrix`: the sound matrix loaded (audio manager `+0x1e0 + 0x23d64`, 64 bytes); empty before the first.
    std::string soundMatrix;
    /// `GetPTank` / `ReleasePTank`: the sprite batches in use, by slot (the handle is the slot << 16). **Coney
    /// stand-in**: the batches are not drawn (the resource manager's sheets are not loaded for them).
    std::vector<bool> ptanks;
};

} // namespace coney
