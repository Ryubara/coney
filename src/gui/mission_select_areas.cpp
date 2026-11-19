// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mission_select_areas.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <string>
#include <utility>

#include "core/name_hash.h"
#include "gui/game_stats.h"

namespace coney::gui {

namespace {

// The map tables' shape: a row per mission byte 1-23, a column per video mode.
constexpr std::size_t kMapRows = 23;
constexpr std::size_t kMapColumns = 5;

// `0x005148e0`: one row per mission byte (1-23), the five video-mode columns, each (x, y).
constexpr std::array<std::array<MapPoint, kMapColumns>, kMapRows> kMapPoints{{
    {{{0.36F, 0.947F}, {0.333F, 1.01F}, {0.369F, 0.948F}, {0.369F, 0.848F}, {0.338F, 0.875F}}},
    {{{0.36F, 0.947F}, {0.333F, 1.01F}, {0.369F, 0.948F}, {0.369F, 0.848F}, {0.338F, 0.875F}}},
    {{{0.385F, 0.916F}, {0.36F, 0.975F}, {0.391F, 0.92F}, {0.391F, 0.825F}, {0.365F, 0.847F}}},
    {{{0.0285F, 0.411F}, {-0.0585F, 0.399F}, {0.062F, 0.382F}, {0.058F, 0.43F}, {-0.045F, 0.426F}}},
    {{{0.108F, 0.198F}, {0.035F, 0.156F}, {0.136F, 0.187F}, {0.133F, 0.265F}, {0.045F, 0.25F}}},
    {{{0.07F, 0.622F}, {-0.009F, 0.642F}, {0.101F, 0.649F}, {0.097F, 0.597F}, {0.002F, 0.605F}}},
    {{{0.036F, 0.325F}, {-0.05F, 0.3F}, {0.069F, 0.303F}, {0.065F, 0.364F}, {-0.036F, 0.353F}}},
    {{{0.36F, 0.947F}, {0.333F, 1.01F}, {0.369F, 0.948F}, {0.369F, 0.848F}, {0.338F, 0.875F}}},
    {{{0.36F, 0.947F}, {0.333F, 1.01F}, {0.369F, 0.948F}, {0.369F, 0.848F}, {0.338F, 0.875F}}},
    {{{0.385F, 0.916F}, {0.36F, 0.975F}, {0.391F, 0.916F}, {0.391F, 0.916F}, {0.365F, 0.847F}}},
    {{{0.184F, 0.134F}, {0.1245F, 0.0825F}, {0.205F, 0.127F}, {0.204F, 0.215F}, {0.134F, 0.195F}}},
    {{{0.31F, 0.91F}, {0.273F, 0.969F}, {0.323F, 0.915F}, {0.323F, 0.82F}, {0.28F, 0.846F}}},
    {{{0.227F, 0.167F}, {0.176F, 0.119F}, {0.246F, 0.156F}, {0.244F, 0.24F}, {0.185F, 0.22F}}},
    {{{0.151F, 0.102F}, {0.086F, 0.045F}, {0.175F, 0.096F}, {0.173F, 0.189F}, {0.095F, 0.167F}}},
    {{{0.108F, 0.198F}, {0.035F, 0.156F}, {0.136F, 0.187F}, {0.133F, 0.265F}, {0.045F, 0.25F}}},
    {{{0.0285F, 0.411F}, {0.058F, 0.411F}, {0.062F, 0.382F}, {0.058F, 0.43F}, {-0.045F, 0.426F}}},
    {{{0.094F, 0.57F}, {0.018F, 0.58F}, {0.123F, 0.6F}, {0.12F, 0.556F}, {0.03F, 0.559F}}},
    {{{0.36F, 0.947F}, {0.333F, 1.01F}, {0.369F, 0.948F}, {0.369F, 0.848F}, {0.338F, 0.875F}}},
    {{{0.385F, 0.916F}, {0.36F, 0.975F}, {0.391F, 0.916F}, {0.391F, 0.916F}, {0.365F, 0.847F}}},
    {{{0.36F, 0.947F}, {0.333F, 1.01F}, {0.369F, 0.948F}, {0.369F, 0.848F}, {0.338F, 0.875F}}},
    {{{0.385F, 0.916F}, {0.36F, 0.975F}, {0.391F, 0.916F}, {0.391F, 0.916F}, {0.365F, 0.847F}}},
    {{{0.072F, 0.658F}, {-0.006F, 0.681F}, {0.103F, 0.652F}, {0.099F, 0.625F}, {0.004F, 0.634F}}},
    {{{0.016F, 0.255F}, {-0.0735F, 0.221F}, {0.051F, 0.238F}, {0.048F, 0.309F}, {-0.06F, 0.295F}}},
}};

// `0x00514c78`: one row per mission byte (1-23), the five video-mode columns, each (x, y).
constexpr std::array<std::array<MapPoint, kMapColumns>, kMapRows> kLogoPoints{{
    {{{0.36F, 0.744F}, {0.333F, 0.8F}, {0.369F, 0.744F}, {0.368F, 0.69F}, {0.338F, 0.71F}}},
    {{{0.36F, 0.744F}, {0.333F, 0.8F}, {0.369F, 0.744F}, {0.368F, 0.69F}, {0.338F, 0.71F}}},
    {{{0.38F, 0.838F}, {0.35F, 0.885F}, {0.391F, 0.838F}, {0.391F, 0.75F}, {0.36F, 0.78F}}},
    {{{0.13F, 0.41F}, {0.045F, 0.395F}, {0.169F, 0.41F}, {0.169F, 0.43F}, {0.06F, 0.426F}}},
    {{{0.203F, 0.268F}, {0.13F, 0.225F}, {0.203F, 0.268F}, {0.23F, 0.315F}, {0.14F, 0.3F}}},
    {{{0.18F, 0.622F}, {0.1F, 0.67F}, {0.197F, 0.622F}, {0.197F, 0.622F}, {0.12F, 0.622F}}},
    {{{0.165F, 0.325F}, {0.08F, 0.3F}, {0.194F, 0.325F}, {0.194F, 0.365F}, {0.1F, 0.35F}}},
    {{{0.36F, 0.744F}, {0.333F, 0.8F}, {0.369F, 0.744F}, {0.368F, 0.69F}, {0.338F, 0.71F}}},
    {{{0.36F, 0.744F}, {0.333F, 0.8F}, {0.369F, 0.744F}, {0.368F, 0.69F}, {0.338F, 0.71F}}},
    {{{0.38F, 0.838F}, {0.35F, 0.885F}, {0.391F, 0.838F}, {0.391F, 0.75F}, {0.36F, 0.78F}}},
    {{{0.184F, 0.324F}, {0.1245F, 0.27F}, {0.201F, 0.324F}, {0.204F, 0.35F}, {0.134F, 0.335F}}},
    {{{0.321F, 0.84F}, {0.28F, 0.89F}, {0.321F, 0.84F}, {0.321F, 0.75F}, {0.29F, 0.77F}}},
    {{{0.34F, 0.215F}, {0.285F, 0.2F}, {0.364F, 0.215F}, {0.364F, 0.29F}, {0.3F, 0.307F}}},
    {{{0.151F, 0.29F}, {0.086F, 0.23F}, {0.172F, 0.29F}, {0.173F, 0.33F}, {0.095F, 0.307F}}},
    {{{0.203F, 0.268F}, {0.13F, 0.225F}, {0.203F, 0.268F}, {0.23F, 0.315F}, {0.14F, 0.3F}}},
    {{{0.169F, 0.41F}, {0.045F, 0.395F}, {0.169F, 0.41F}, {0.169F, 0.43F}, {0.06F, 0.426F}}},
    {{{0.2F, 0.4895F}, {0.125F, 0.496F}, {0.2F, 0.4895F}, {0.2F, 0.4895F}, {0.14F, 0.493F}}},
    {{{0.36F, 0.744F}, {0.333F, 0.8F}, {0.369F, 0.744F}, {0.368F, 0.69F}, {0.338F, 0.71F}}},
    {{{0.38F, 0.838F}, {0.35F, 0.885F}, {0.391F, 0.838F}, {0.391F, 0.75F}, {0.36F, 0.78F}}},
    {{{0.36F, 0.744F}, {0.333F, 0.8F}, {0.369F, 0.744F}, {0.368F, 0.69F}, {0.338F, 0.71F}}},
    {{{0.38F, 0.838F}, {0.35F, 0.885F}, {0.391F, 0.838F}, {0.391F, 0.75F}, {0.36F, 0.78F}}},
    {{{0.16F, 0.756F}, {0.08F, 0.77F}, {0.18F, 0.756F}, {0.19F, 0.7F}, {0.1F, 0.71F}}},
    {{{0.19F, 0.254F}, {0.105F, 0.225F}, {0.227F, 0.254F}, {0.235F, 0.31F}, {0.125F, 0.295F}}},
}};

// The row of `record`'s mission byte (`+0x0c`, 1-based), or nothing for none or one past the tables.
// @orig 0x0041f540 LevelRecord_GetMapPos (unknown)
// @orig 0x0041f5f8 LevelRecord_GetMapPos2 (unknown)
std::optional<std::size_t> mapRow(const LevelRecord& record) {
    const double mission = record.values.at(LevelRecord::kMissionValue);
    if (mission < 1.0 || mission > static_cast<double>(kMapRows)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(mission) - 1;
}

// A table's point for `record` and `column`; (0, 0) without a row (Coney's stand-in).
// @orig 0x0041f540 LevelRecord_GetMapPos (unknown)
// @orig 0x0041f5f8 LevelRecord_GetMapPos2 (unknown)
MapPoint tablePoint(const std::array<std::array<MapPoint, kMapColumns>, kMapRows>& table, const LevelRecord& record,
                    std::size_t column) {
    const std::optional<std::size_t> row = mapRow(record);
    if (!row || column >= kMapColumns) {
        return MapPoint{};
    }
    return table.at(*row).at(column);
}

} // namespace

std::size_t mapColumn(bool flag02, bool widescreen, bool flag20) {
    // Flag 0x02 first (3, or 4 with 16:9), then progressive (2), then 16:9 (1), else 0.
    if (flag02) {
        return widescreen ? 4 : 3;
    }
    if (flag20) {
        return 2;
    }
    return widescreen ? 1 : 0;
}

std::optional<std::string_view> missionAreaName(const LevelRecord& record) {
    const std::string_view second = record.secondName;
    const std::size_t colon = second.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    return second.substr(colon + 1);
}

std::string missionLabel(const LevelRecord& record) {
    // The characters before the first ':', three at most; none when the name is empty or starts with ':'.
    std::string out;
    for (const char c : record.secondName) {
        if (c == ':' || out.size() == 3) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

int labelNumber(std::string_view label) {
    // strtol in base 10: leading white space, an optional sign, then the digits.
    std::size_t i = 0;
    while (i < label.size() && std::isspace(static_cast<unsigned char>(label[i])) != 0) {
        ++i;
    }
    bool negative = false;
    if (i < label.size() && (label[i] == '+' || label[i] == '-')) {
        negative = label[i] == '-';
        ++i;
    }
    int value = 0;
    while (i < label.size() && label[i] >= '0' && label[i] <= '9') {
        value = (value * 10) + (label[i] - '0');
        ++i;
    }
    return negative ? -value : value;
}

MapPoint missionMapPoint(const LevelRecord& record, std::size_t column) {
    return tablePoint(kMapPoints, record, column);
}

MapPoint missionLogoPoint(const LevelRecord& record, std::size_t column) {
    return tablePoint(kLogoPoints, record, column);
}

graphics::Rgba areaHaloColour(double area) {
    // By the area byte; any other value leaves the original's output unwritten: Coney's black.
    switch (static_cast<int>(area)) {
    case 1:
        return {186, 139, 53, 255};
    case 2:
        return {48, 137, 81, 255};
    case 3:
        return {64, 95, 142, 255};
    case 4:
        return {76, 53, 44, 255};
    default:
        // Coney's choice: black, where the original leaves its output unwritten.
        return {0, 0, 0, 255};
    }
}

bool isMissionSelectListed(const LevelRecord& record, const UnlockRecords& unlocks, const SavedProgress& progress) {
    // Levels 2-99, not the hub (95), not the Armies of the Night (60-65); then the level must be complete.
    const int number = static_cast<int>(record.number);
    if (number < 2 || number > 99 || number == 95 || (number >= 60 && number <= 65)) {
        return false;
    }
    return unlocks.isLevelComplete(progress, static_cast<std::uint8_t>(number));
}

std::vector<MissionSelectArea> buildMissionSelectAreas(const LevelTable& levels, const UnlockRecords& unlocks,
                                                       const SavedProgress& progress, std::size_t column) {
    std::vector<MissionSelectArea> areas;
    for (std::size_t index = 0; index < LevelTable::kCapacity; ++index) {
        const LevelRecord* record = levels.at(index);
        if (record == nullptr || !isMissionSelectListed(*record, unlocks, progress)) {
            continue;
        }
        // The area: found by the CRC-32 of the mission's area name (Coney's stand-in hashes an empty name for a
        // record without a ':', which the original would hash from a null pointer; no disc record has none).
        const std::string_view name = missionAreaName(*record).value_or(std::string_view{});
        const std::uint32_t hash = crc32(name);
        auto area = std::ranges::find(areas, hash, &MissionSelectArea::hash);
        if (area == areas.end()) {
            // A new area for a new hash: its marker and logo at this, its first, mission's points.
            MissionSelectArea made;
            made.firstRecord = index;
            made.hash = hash;
            made.name = std::string(name);
            made.marker = missionMapPoint(*record, column);
            made.logo = missionLogoPoint(*record, column);
            made.logoWord = static_cast<std::uint32_t>(record->values.at(LevelRecord::kLogoValue));
            made.halo = areaHaloColour(record->values.at(LevelRecord::kSubwayValue));
            areas.push_back(std::move(made));
            area = std::prev(areas.end());
        }
        // The mission, appended to its area, with what its entry shows (`MissionSelect_FillArea`: the circles in the
        // level's colour, purple when the label reads as 0) and its checkpoints.
        MissionSelectMission mission;
        mission.record = index;
        mission.level = static_cast<int>(record->number);
        mission.checkpoints = static_cast<std::size_t>(record->values.at(LevelRecord::kSectionsValue));
        mission.name = record->fourthName;
        mission.label = missionLabel(*record);
        mission.circleColour = labelNumber(mission.label) == 0
                                   ? kMissionSelectPurple
                                   : areaColour(record->values.at(LevelRecord::kSubwayValue));
        area->missions.push_back(std::move(mission));
    }
    // The areas by their marker's y, the larger first; each area's missions by their label's number, the smaller
    // first.
    std::ranges::stable_sort(
        areas, [](const MissionSelectArea& a, const MissionSelectArea& b) { return a.marker.y > b.marker.y; });
    for (MissionSelectArea& area : areas) {
        std::ranges::stable_sort(area.missions, [](const MissionSelectMission& a, const MissionSelectMission& b) {
            return labelNumber(a.label) < labelNumber(b.label);
        });
    }
    return areas;
}

} // namespace coney::gui
