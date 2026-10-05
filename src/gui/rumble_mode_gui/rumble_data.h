// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "warriors/game_state.h"
#include "warriors/unlockables.h"

namespace coney::gui {

/// The chunks the Rumble menu's screens run when they open, each building its screen's list through the `CfgRumble*`
/// bindings (docs/research/frontend.md#rumble-data).
inline constexpr std::string_view kRumbleModeChunk = "rumble_data.lua";
inline constexpr std::string_view kRumbleGangChunk = "rumble_gang.lua";
inline constexpr std::string_view kRumbleArenaChunk = "rumble_arena.lua";

/// One entry of the Game Mode screen's list (`0x0063ee84`), a `CfgRumbleGame` call. The original's record is 0x1b4
/// bytes, most of it the entry's text widget; Coney keeps the fields the menu reads.
struct RumbleModeEntry {
    /// The Game Type screen's entries, in the order of the record's three flags.
    static constexpr std::size_t kPlayerOptions = 3;

    std::string title;                                ///< `+0x08`: the mode's title, from the chunk's string table.
    std::uint16_t mode = 0;                           ///< `+0x0c`: the `RM_*` id, copied to the set-up's `gameType`.
    std::array<bool, kPlayerOptions> playerOptions{}; ///< `+0x10` / `+0x14` / `+0x18`: one player, co-op, versus.
    std::uint16_t gangSize = 0;                       ///< `+0x1c`: fighters per side, copied to `gangSize`.
    /// `+0x20` / `+0x22`: the preset character type of side 1 and side 2; 0 when the player chooses the gangs.
    std::array<std::uint16_t, 2> presets{};
    std::string description; ///< The text the record's widget (`+0x24`) shows under the title.
};

/// One entry of the Choose Gangs screen's list (`0x0063ee4c`), a `CfgRumbleGang` call: the original's 0x54-byte record.
struct RumbleGangEntry {
    /// Character types in a roster.
    static constexpr std::size_t kMembers = RumbleSetup::kGangMembers;
    /// One side's roster: nine character types, the first the warchief.
    using Roster = std::array<std::uint32_t, kMembers>;

    std::int32_t id = 0; ///< `+0x00`: the gang id; its pack is id - 1.
    std::string name;    ///< `+0x04`: the gang's display name.
    /// `+0x08` and `+0x2c`: side 1's roster and side 2's, the same types at first, each rotated on its own to choose
    /// that side's warchief.
    std::array<Roster, 2> rosters{};
    std::size_t index = 0; ///< `+0x50`: the entry's position in the list.
};

/// One entry of the Choose Area screen's list (`0x0063ee40`), a `CfgRumbleArena` call.
struct RumbleArenaEntry {
    int levelNumber = 0;        ///< The arena's level number (102 for `level102`).
    std::uint32_t value = 0;    ///< The second argument, stored but not researched (9 in every call on the disc).
    std::size_t levelIndex = 0; ///< `+0x60`: the arena's record in the level table, which the launch reads.
};

/// One `CfgRumbleChar` record (`RM_CharData`): a character type's Rumble description.
struct RumbleCharEntry {
    std::string name;      ///< The display name, upper-cased.
    std::string gangName;  ///< The gang's name, as given.
    std::uint8_t gang = 0; ///< `+0x0c`: the gang id.
    std::uint8_t rank = 0; ///< `+0x0d`: `RM_WARCHIEF`, `RM_LT`, `RM_SOLDIER`, ...
    std::uint16_t n6 = 0;  ///< `+0x0e`: not researched (0 in the scripts).
    std::uint8_t n7 = 0;   ///< `+0x10`: not researched (20-80, a strength rating, inferred).
    std::string bio;       ///< The description, upper-cased.
};

/// The lists the Rumble menu's chunks build, which the `CfgRumble*` bindings fill: the modes, gangs and arenas the
/// player may choose (each added only when unlocked) and the characters' Rumble descriptions. A screen clears its list
/// and runs its chunk each time it opens.
///
/// Research: docs/research/frontend.md#rumble-data, docs/references/bindings/config.md#cfgrumblegame
struct RumbleData {
    std::vector<RumbleModeEntry> modes;             ///< `0x0063ee84`.
    std::vector<RumbleGangEntry> gangs;             ///< `0x0063ee4c`.
    std::vector<RumbleArenaEntry> arenas;           ///< `0x0063ee40`.
    std::map<std::uint32_t, RumbleCharEntry> chars; ///< By character type.
};

/// `CfgRumbleGame`: appends `entry` to `data.modes` when its mode is unlocked in `unlocks`; returns whether it did.
/// @orig 0x001f8110 RM_GameMode_AddGame (RM_GameMode.cpp)
bool addRumbleMode(RumbleData& data, const Unlockables& unlocks, RumbleModeEntry entry);

/// `CfgRumbleGang`: appends gang `id` named `name` with the nine character types `members` (each passed through
/// rumbleStandIn()) to `data.gangs`, when the id is negative or the gang is unlocked; returns whether it did.
/// @orig 0x001ec980 RM_ChooseGangs_AddGang (RM_ChooseGangs.cpp)
bool addRumbleGang(RumbleData& data, const Unlockables& unlocks, std::int32_t id, std::string name,
                   const RumbleGangEntry::Roster& members);

/// `CfgRumbleArena`: appends the arena with level number `levelNumber` to `data.arenas` when it is unlocked, the level
/// table `state.levels` has it, and `modes` allows the mode chosen in `state.rumble` (a first value of 0 allows every
/// mode; the list ends at a 0 or -1). Returns whether it did.
/// @orig 0x001eaa30 RM_ChooseArea_AddArena (RM_ChooseArea.cpp)
bool addRumbleArena(RumbleData& data, const GameState& state, int levelNumber, std::uint32_t value,
                    std::span<const std::int64_t> modes);

/// `CfgRumbleChar`: keeps `entry` for character type `type`, replacing an earlier one; the name and the description are
/// upper-cased.
/// @orig 0x001f0e60 RM_CharData_Add (unknown)
void addRumbleChar(RumbleData& data, std::uint32_t type, RumbleCharEntry entry);

/// The character type a gang's roster uses for `type`: its stand-in from the table at `0x001ec490` when `type` is in
/// the table and not unlocked (UnlockKind::CharacterType), else `type`. One lookup: a stand-in is not looked up again
/// (Coney's reading of the page's pairs).
/// @orig 0x001ec490 RM_ChooseGangs_StandInType (RM_ChooseGangs.cpp)
[[nodiscard]] std::uint32_t rumbleStandIn(const Unlockables& unlocks, std::uint32_t type);

} // namespace coney::gui
