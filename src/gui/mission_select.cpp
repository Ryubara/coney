// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mission_select.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <string>
#include <utility>

#include "core/name_hash.h"
#include "graphics/overlay_camera.h"
#include "gui/colour_table.h"
#include "gui/game_stats.h"
#include "gui/text_layout.h"

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
                                   ? MissionSelectScreen::kPurple
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

MissionSelectLayout MissionSelectLayout::forFlags(bool flag02, bool widescreen, bool flag20) {
    MissionSelectLayout layout;
    layout.column = mapColumn(flag02, widescreen, flag20);
    // Coney's choice: the entries' circles' y offset (`0x0050e868`: -0.002, 0.008 with 16:9) is not kept, as Coney's
    // CircledText places its circles on the text's row.
    if (flag02 && widescreen) {
        layout.x = 0.843F;
        layout.y = 0.153F;
        layout.textX = 0.508F;
        layout.textY = 0.067F;
        layout.tile = 0.25F;
        layout.circleW = 0.04F;
        layout.circleH = 0.056F;
        layout.background = 1.6F;
        layout.backgroundHeight = 0.88F;
        layout.logo = 0.6F;
        layout.logoWidth = 2.0F;
        layout.marker = 0.012F;
        layout.usageX = -0.08F;
        layout.usageY = 0.76F;
        layout.alternative = true;
        layout.stepDivisor = CircledText::kStepDivisor02;
        return layout;
    }
    if (flag02) {
        layout.x = 0.785F;
        layout.y = 0.173F;
        layout.textX = 0.43F;
        layout.textY = 0.057F;
        layout.tile = 0.27F;
        layout.circleW = 0.04F;
        layout.circleH = 0.056F;
        layout.background = 1.05F;
        layout.backgroundHeight = 0.82F;
        layout.logo = 0.6F;
        layout.logoWidth = 2.0F;
        layout.marker = 0.01F;
        layout.usageX = 0.045F;
        layout.usageY = 0.75F;
        layout.stepDivisor = CircledText::kStepDivisor02;
        return layout;
    }
    if (flag20) {
        // Progressive keeps the static values, which differ from the default mode's, with the alternative sheet.
        layout.x = 0.78F;
        layout.y = 0.0772F;
        layout.textX = 0.415F;
        layout.textY = 0.065F;
        layout.tile = 0.415F;
        layout.circleW = 0.05F;
        layout.circleH = 0.07F;
        layout.background = 1.05F;
        layout.usageX = 0.03F;
        layout.usageY = 0.83F;
        layout.alternative = true;
        return layout;
    }
    if (widescreen) {
        layout.x = 0.852F;
        layout.y = 0.022F;
        layout.textX = 0.52F;
        layout.textY = 0.083F;
        layout.circleOffset = 0.01F;
        layout.tile = 0.423F;
        layout.circleW = 0.057F;
        layout.circleH = 0.08F;
        layout.background = 1.64F;
        layout.backgroundHeight = 1.2F;
        layout.usageX = -0.1F;
        layout.usageY = 0.85F;
        layout.alternative = true;
        return layout;
    }
    return layout;
}

void MissionSelectScreen::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

MissionSelectMission& MissionSelectScreen::current() { return m_areas.at(m_cursor).missions.at(m_list.selected()); }

void MissionSelectScreen::open(std::vector<MissionSelectArea> areas, std::uint64_t nowMs) {
    m_areas = std::move(areas);
    m_open = true;
    m_cancelled = false;
    m_chosen = false;
    m_chosenLevel = 0;
    m_chosenCheckpoint = 1;
    m_start = Start::None;
    m_startMs = 0;
    m_shown.reset();
    m_missionOpen = false;
    m_refused = false;
    m_cursor = 0;
    // The map (grey 144), the gang logo (white, with a shadow) and the halo (rectangle 2, 3 in the other sheet), all
    // placed by each update.
    m_map.setBatch(m_batches.frame);
    m_logo.setBatch(m_batches.logos);
    m_halo.setBatch(m_batches.frame);
    m_halo.setRect(m_layout.alternative ? 3 : 2);
    // A marker per area.
    m_markers.clear();
    m_markers.reserve(m_areas.size());
    for (std::size_t i = 0; i < m_areas.size(); ++i) {
        m_markers.push_back(std::make_unique<BaseWidget>(m_batches.frame, kMarkerRect));
    }
    // The title: the text's offset as its size, circles 0.055 square, the back rectangle 1 and circles 3 (2 in the
    // other sheet), Font_Size(1.25), no circled characters.
    // Coney's choice: x is the left edge, as Coney's CircledText takes it (the original's takes the centre).
    m_title.setBack(m_batches.frame, 1);
    m_title.setCircleSprite(m_batches.frame, m_layout.alternative ? 2 : 3);
    m_title.setup(CircledTextSetup{.x = m_layout.x - (m_layout.textX / 2.0F),
                                   .y = m_layout.y,
                                   .width = m_layout.textX,
                                   .height = m_layout.textY,
                                   .circleWidth = kTitleCircle,
                                   .circleHeight = kTitleCircle,
                                   .circleOffset = m_layout.circleOffset,
                                   .textSize = kTitleSize,
                                   .stepDivisor = m_layout.stepDivisor});
    // Coney's choice: the title has no circled characters (the slot +0x94 call after its set-up is not traced).
    m_title.setCircled({});
    // The usage line, left-aligned at the layout's point, showing global string 0x20.
    m_usage.place(m_layout.usageX, m_layout.usageY, true);
    m_usage.setLegend(m_strings != nullptr ? m_strings->get(kUsageString) : std::string_view{});
    m_letter.setup(TextWidgetSetup{.colour = colourAt(0x005fd328), .fontSlot = CircledText::kFontSlot});
    // The first area shown, then the area menu takes the focus.
    showArea();
    m_menuFocused = true;
    m_input.focus(nowMs);
    m_input.setDpadRepeat(true);
}

void MissionSelectScreen::close() {
    m_open = false;
    m_entries.clear();
    m_markers.clear();
    m_areas.clear();
}

void MissionSelectScreen::showArea() {
    m_refused = false;
    fillArea();
}

void MissionSelectScreen::fillArea() {
    m_entries.clear();
    if (m_cursor >= m_areas.size()) {
        m_list.setup(ScrollingMenuSetup{.visible = 0, .moveCue = kMoveCue, .playCue = m_playCue}, 0);
        return;
    }
    const MissionSelectArea& area = m_areas.at(m_cursor);
    // An entry per mission: its name after its circled label, the circles in its colour, on a black back (rectangle
    // 1, circles 3, or 2 in the other sheet), Font_Size(0.92), stacked down from below the title by each update.
    for (const MissionSelectMission& mission : area.missions) {
        auto entry = std::make_unique<CircledText>();
        entry->setBack(m_batches.frame, 1);
        entry->setCircleSprite(m_batches.frame, m_layout.alternative ? 2 : 3);
        entry->setup(CircledTextSetup{.x = m_layout.x - (m_layout.textX / 2.0F),
                                      .y = m_layout.y,
                                      .width = m_layout.textX,
                                      .height = m_layout.textY,
                                      .circleWidth = m_layout.circleW,
                                      .circleHeight = m_layout.circleH,
                                      .circleOffset = m_layout.circleOffset,
                                      .textSize = kEntrySize,
                                      .stepDivisor = m_layout.stepDivisor});
        entry->setText(mission.name);
        entry->setCircled(mission.label);
        entry->setBackColour(graphics::kBlack);
        entry->setCircleColour(mission.circleColour);
        m_entries.push_back(std::move(entry));
    }
    // The area's list keeps its selection (the original keeps a menu per area); every mission shows.
    m_list.setup(ScrollingMenuSetup{.visible = area.missions.size(), .moveCue = kMoveCue, .playCue = m_playCue},
                 area.missions.size());
    m_list.select(area.selected);
}

void MissionSelectScreen::nextArea() {
    if (m_cursor + 1 < m_areas.size()) {
        ++m_cursor;
        showArea();
        playCue(kMoveCue);
        return;
    }
    playCue(kRefusedCue);
    m_refused = true;
}

void MissionSelectScreen::prevArea() {
    if (m_cursor == 0) {
        playCue(kRefusedCue);
        m_refused = true;
        return;
    }
    --m_cursor;
    showArea();
    playCue(kMoveCue);
}

void MissionSelectScreen::onMenuCommand(MenuCommand command, std::uint64_t nowMs) {
    switch (command) {
    case MenuCommand::Up:
        nextArea();
        return;
    case MenuCommand::Down:
        prevArea();
        return;
    case MenuCommand::Left:
    case MenuCommand::Right:
        return;
    case MenuCommand::Accept:
        // The focus moves to the area's list. Coney's choice: nothing without a mission to list (the original never
        // opens with none).
        if (m_cursor >= m_areas.size() || m_areas.at(m_cursor).missions.empty()) {
            return;
        }
        m_menuFocused = false;
        m_list.takeFocus(m_input, nowMs);
        playCue(kAcceptCue);
        return;
    case MenuCommand::Back:
        m_cancelled = true;
        return;
    }
}

bool MissionSelectScreen::onListCommand(MenuCommand command, std::uint64_t nowMs) {
    switch (command) {
    case MenuCommand::Down: {
        // With a mission open, the next checkpoint letter within its sections; else the list's own move.
        if (!m_missionOpen) {
            return false;
        }
        MissionSelectMission& mission = current();
        if (mission.letter + 1 >= mission.checkpoints) {
            playCue(kRefusedCue);
            m_refused = true;
            return true;
        }
        ++mission.letter;
        playCue(kMoveCue);
        return true;
    }
    case MenuCommand::Up: {
        if (!m_missionOpen) {
            return false;
        }
        MissionSelectMission& mission = current();
        if (mission.letter == 0) {
            playCue(kRefusedCue);
            m_refused = true;
            return true;
        }
        --mission.letter;
        playCue(kMoveCue);
        return true;
    }
    case MenuCommand::Accept:
        // The first accept opens the selected mission (its tile highlighted), the second starts it.
        if (!m_missionOpen) {
            m_missionOpen = true;
            current().open = true;
        } else {
            // The start stamps no time: the choice waits for the clock against Init's 0 (`+0x6a0`).
            m_start = Start::Waiting;
        }
        playCue(kAcceptCue);
        return true;
    case MenuCommand::Back:
        // With none open the focus goes back to the area menu; either way the mission closes.
        if (!m_missionOpen) {
            m_menuFocused = true;
            m_input.focus(nowMs);
            m_input.setDpadRepeat(true);
        }
        m_missionOpen = false;
        current().open = false;
        playCue(kBackCue);
        return true;
    case MenuCommand::Left:
    case MenuCommand::Right:
        return false;
    }
    return false;
}

void MissionSelectScreen::handle(MenuCommand command, std::uint64_t nowMs) {
    // No command once the screen is done or while a start waits (the original reads no pad then).
    if (!m_open || done() || m_start != Start::None) {
        return;
    }
    if (m_menuFocused) {
        onMenuCommand(command, nowMs);
        return;
    }
    // The list's handler first; what it leaves goes to the list's own moves.
    if (!onListCommand(command, nowMs) && (command == MenuCommand::Up || command == MenuCommand::Down)) {
        (void)m_list.handle(command, nowMs);
        m_areas.at(m_cursor).selected = m_list.selected();
    }
}

void MissionSelectScreen::onAccept() {
    const MissionSelectMission& mission = current();
    playCue(kAcceptCue);
    m_chosenLevel = mission.level;
    m_chosenCheckpoint = static_cast<int>(mission.letter) + 1;
    m_chosen = true;
}

void MissionSelectScreen::update(const GuiFrame& frame) {
    if (!m_open || done()) {
        return;
    }
    // While a start waits, only the clock: the choice on the first frame the clock is more than kStartDelayMs past
    // the start stamp, which only open() sets (to 0), so in play on the next frame. Coney's stand-in: the clock is
    // the game time the screen is given, not the real-time clock since start-up.
    if (m_start != Start::None) {
        if (m_start == Start::Waiting && frame.timeMs - m_startMs > kStartDelayMs) {
            m_start = Start::Accepted;
            onAccept();
        }
        return;
    }
    if (m_cursor >= m_areas.size()) {
        return;
    }
    // A newly shown area starts the pulse again and names the title.
    if (m_shown != m_cursor) {
        m_shown = m_cursor;
        m_pulseUp = false;
        m_pulseStartMs = frame.timeMs;
        m_title.setText(m_areas.at(m_cursor).name);
    }
    // The pad: the area menu's d-pad repeats unless the last move was refused.
    if (frame.pad != nullptr) {
        if (m_menuFocused) {
            m_input.setDpadRepeat(!m_refused);
        }
        if (const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, frame.timeMs)) {
            handle(*command, frame.timeMs);
        }
    }
    const MissionSelectArea& area = m_areas.at(m_cursor);
    // The map: its width a GUI width (Coney's reading, as the statistics screen's background) by its height.
    m_map.setup(BaseWidgetSetup{.x = 0.5F,
                                .y = 0.5F,
                                .height = m_layout.backgroundHeight,
                                .width = graphics::OverlayCamera::guiWidthToOverlay(m_layout.background),
                                .colour = kMapGrey});
    // The area's gang logo at its point, sized by its texture rectangle × the logo's size (× the width factor).
    // The sprite word's rectangle: its low half.
    const std::size_t logoRect = area.logoWord & 0xffffU;
    m_logo.setRect(logoRect);
    graphics::UvRect uv{};
    if (m_batches.logos != nullptr && logoRect < m_batches.logos->sheet().page.rects.size()) {
        uv = m_batches.logos->sheet().page.rect(logoRect);
    }
    m_logo.setup(BaseWidgetSetup{.x = area.logo.x,
                                 .y = area.logo.y,
                                 .height = (uv.v1 - uv.v0) * m_layout.logo,
                                 .width = (uv.u1 - uv.u0) * m_layout.logo * m_layout.logoWidth,
                                 .colour = graphics::kWhite,
                                 .shadow = true});
    // The pulse: one way each kPulseMs, from 2 down to 1, then from 1 up to 2.
    std::uint64_t elapsed = frame.timeMs - m_pulseStartMs;
    if (elapsed >= kPulseMs) {
        elapsed = 0;
        m_pulseStartMs = frame.timeMs;
        m_pulseUp = !m_pulseUp;
    }
    const float share = static_cast<float>(elapsed) / static_cast<float>(kPulseMs);
    m_pulse = m_pulseUp ? 1.0F + share : 2.0F - share;
    // Every marker at its area's point in the menus' grey; the shown one pulsing, with the halo around it in the
    // area's halo colour × the map's grey.
    for (std::size_t i = 0; i < m_markers.size(); ++i) {
        const bool shown = i == m_cursor;
        m_markers.at(i)->setup(BaseWidgetSetup{.x = m_areas.at(i).marker.x,
                                               .y = m_areas.at(i).marker.y,
                                               .height = m_layout.marker * (shown ? m_pulse : 1.0F),
                                               .colour = kMenuGrey});
    }
    // A channel of the halo colour × the map's grey / 255, the fraction dropped (`Float_ToUInt`).
    const auto tint = [](std::uint8_t colour, std::uint8_t grey) {
        return static_cast<std::uint8_t>(static_cast<float>(colour) * 0.003921569F * static_cast<float>(grey));
    };
    m_halo.setup(BaseWidgetSetup{.x = area.marker.x,
                                 .y = area.marker.y,
                                 .height = m_layout.marker * m_pulse * kHaloScale,
                                 .colour = graphics::Rgba{tint(area.halo.r, kMapGrey.r), tint(area.halo.g, kMapGrey.g),
                                                          tint(area.halo.b, kMapGrey.b), area.halo.a}});
    m_title.update(frame);
    // The list: the entries stacked from below the title, an item gap apart; the open mission's letter under its
    // entry (the tile's stand-in, #1451).
    float y = m_layout.y + m_layout.textY + m_layout.listGap;
    m_letter.setVisible(false);
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        CircledText& entry = *m_entries.at(i);
        entry.moveTo(y);
        entry.setFocused(!m_menuFocused && i == m_list.selected());
        entry.update(frame);
        const MissionSelectMission& mission = area.missions.at(i);
        if (mission.open) {
            m_letter.setVisible(true);
            m_letter.style().x = m_layout.x - (m_layout.textX / 2.0F) + m_layout.circleW;
            m_letter.style().y = y + m_layout.textY;
            m_letter.setText(std::string(1, static_cast<char>('A' + mission.letter)));
            y += m_layout.textY;
        }
        y += m_layout.textY + m_layout.listGap;
    }
    m_letter.update(frame);
    m_usage.update(frame);
}

void MissionSelectScreen::render(const GuiCanvas& canvas) const {
    if (!m_open || !m_shown) {
        return;
    }
    m_map.render(canvas);
    m_logo.render(canvas);
    m_halo.render(canvas);
    for (const std::unique_ptr<BaseWidget>& marker : m_markers) {
        marker->render(canvas);
    }
    // The title and the area's list. Coney's stand-in for the open mission's tile is its letter (#1451).
    m_title.render(canvas);
    for (const std::unique_ptr<CircledText>& entry : m_entries) {
        entry->render(canvas);
    }
    if (m_letter.visible()) {
        m_letter.render(canvas);
    }
    m_usage.render(canvas);
}

} // namespace coney::gui
