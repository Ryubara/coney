// SPDX-License-Identifier: GPL-3.0-or-later
// The mission select's areas: what the level records say about each mission (its area, label, map and logo points,
// halo colour), which missions the screen lists, and how they group into sorted areas. The screen itself is in
// gui/mission_select.h.
//
// Research: docs/research/frontend.md#fn-missionselecthud
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"
#include "warriors/level_table.h"
#include "warriors/profile_record.h"
#include "warriors/unlock_records.h"

namespace coney::gui {

/// A point on the mission select's map, in GUI coordinates.
struct MapPoint {
    float x = 0.0F; ///< GUI x.
    float y = 0.0F; ///< GUI y.

    friend bool operator==(const MapPoint&, const MapPoint&) = default;
};

/// Which column of the map tables a video mode reads (`LevelRecord_GetMapPos`): 0 the default, 1 16:9 (flag `0x04`),
/// 2 progressive (flag `0x20`), 3 flag `0x02`, 4 flag `0x02` with 16:9.
/// @orig 0x0041f540 LevelRecord_GetMapPos (unknown)
[[nodiscard]] std::size_t mapColumn(bool flag02, bool widescreen, bool flag20);

/// The text after the first `:` of a level record's second name (`<label>:<area>`): the mission's area, which names
/// the area's title and whose CRC-32 groups the missions; nothing when the name has no `:`.
/// @orig 0x0041f3e8 LevelRecord_GetAreaName (unknown)
[[nodiscard]] std::optional<std::string_view> missionAreaName(const LevelRecord& record);

/// The text before the first `:` of a level record's second name, at most three characters: the mission's number,
/// circled on its entry; empty when the name starts with `:` or is empty.
/// @orig 0x0041f428 LevelRecord_GetLabel (unknown)
[[nodiscard]] std::string missionLabel(const LevelRecord& record);

/// A label read as a base-10 number as `strtol` reads it (leading spaces, a sign, the digits; 0 when none).
/// @coney-only the C library's strtol (`0x00433a68`), middleware rather than game code
[[nodiscard]] int labelNumber(std::string_view label);

/// A mission's marker on the map: the record's mission byte (`+0x0c`, 1-23) row of the table at `0x005148e0`, column
/// `column` (mapColumn()). A record without a mission byte, or past the table, gets (0, 0) (Coney's stand-in: the
/// original returns its static result unchanged, never seen for a listed mission).
/// @orig 0x0041f540 LevelRecord_GetMapPos (unknown)
[[nodiscard]] MapPoint missionMapPoint(const LevelRecord& record, std::size_t column);

/// Where an area's gang logo sits over the map: the same row and column of the table at `0x00514c78`.
/// @orig 0x0041f5f8 LevelRecord_GetMapPos2 (unknown)
[[nodiscard]] MapPoint missionLogoPoint(const LevelRecord& record, std::size_t column);

/// The halo's colour for a level of area byte `area` (record `+0x7c`): 1 (186, 139, 53), 2 (48, 137, 81),
/// 3 (64, 95, 142), 4 (76, 53, 44). For any other value the original leaves its output unwritten; Coney gives black.
/// @orig 0x0041f340 LevelRecord_GetAreaColour2 (unknown)
[[nodiscard]] graphics::Rgba areaHaloColour(double area);

/// Whether the mission select lists level record `record`: its number is 2-99 but not 95 (the hub) nor 60-65 (the
/// Armies of the Night), and the level is complete.
/// @orig 0x001bc100 MissionSelect_IsMissionListed (MissionSelectHUD.cpp)
[[nodiscard]] bool isMissionSelectListed(const LevelRecord& record, const UnlockRecords& unlocks,
                                         const SavedProgress& progress);

/// The entries' circle colour when the label reads as 0 (`MissionSelect_FillArea`).
inline constexpr graphics::Rgba kMissionSelectPurple{106, 65, 131, 255};

/// One mission of the mission select (an `MS_Mission`): its record and the entry and tile it shows.
struct MissionSelectMission {
    std::size_t record = 0;        ///< The level record's index (`+0x00`).
    int level = 0;                 ///< The level number (record `+0x04`): what onChoose receives.
    std::size_t checkpoints = 0;   ///< The record's section count (`+0x08`): how many letters it offers.
    std::string name;              ///< The entry's text: the level's menu name (record `+0x49`).
    std::string label;             ///< The circled characters: missionLabel().
    graphics::Rgba circleColour{}; ///< The level's colour, purple when the label reads as 0.
    std::size_t letter = 0;        ///< The chosen checkpoint, from 0 (`'A'`; the tile's `+0x60`).
    bool open = false;             ///< The tile is highlighted: the mission is open.
};

/// One area of the mission select (an `MS_Area`): the missions whose area names hash alike.
struct MissionSelectArea {
    std::size_t firstRecord = 0;                ///< The first mission's record (`+0x00`): the marker's and logo's.
    std::uint32_t hash = 0;                     ///< The CRC-32 of the area's name (`+0x04`).
    std::string name;                           ///< The area's name: the title shows it.
    MapPoint marker;                            ///< The marker's place (the first mission's map point).
    MapPoint logo;                              ///< The gang logo's place (the first mission's logo point).
    std::uint32_t logoWord = 0;                 ///< The logo's sprite word (record `+0x78`).
    graphics::Rgba halo{};                      ///< The halo's colour (areaHaloColour of the first mission).
    std::vector<MissionSelectMission> missions; ///< The missions (`+0x0c`), sorted by their label.
    std::size_t selected = 0;                   ///< The list's selected mission (the area's menu, `+0x20`).
};

/// The areas the mission select lists, as `MissionSelect_BuildAreas` makes them: the level records in index order,
/// each one isMissionSelectListed() passes going into the area of its name's CRC-32 (a new area made for a new hash);
/// the areas then sorted by their marker's y, the larger first, and each area's missions by labelNumber(), the smaller
/// first. The original's sorts are `std::sort`, which keeps the order of equal keys for 16 entries or fewer; so does
/// Coney's (a stable sort).
/// @orig 0x001bcc48 MissionSelect_BuildAreas (MissionSelectHUD.cpp)
/// @orig 0x001bca70 MissionSelect_AddArea (MissionSelectHUD.cpp)
/// @orig 0x001bc930 MissionSelect_AddMission (MissionSelectHUD.cpp)
/// @orig 0x001bca30 MissionSelect_FindArea (MissionSelectHUD.cpp)
[[nodiscard]] std::vector<MissionSelectArea> buildMissionSelectAreas(const LevelTable& levels,
                                                                     const UnlockRecords& unlocks,
                                                                     const SavedProgress& progress, std::size_t column);
} // namespace coney::gui
