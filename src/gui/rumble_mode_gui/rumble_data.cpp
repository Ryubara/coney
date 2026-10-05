// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/rumble_mode_gui/rumble_data.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace coney::gui {

namespace {

// One pair of the stand-in table: a locked character type and the type used in its place.
struct StandIn {
    std::uint32_t locked;
    std::uint32_t used;
};

// The stand-in table at `0x001ec490`, in the page's order (docs/research/frontend.md#rumble-data).
constexpr std::array kStandIns{
    StandIn{0xb2, 0xc4},  StandIn{0xba, 0xc7},  StandIn{0xb9, 0xc0},   StandIn{0xc4, 0xc5},   StandIn{0xdc, 0xe1},
    StandIn{0xdf, 0xe2},  StandIn{0x64, 0x65},  StandIn{0x63, 0x66},   StandIn{0x59, 0x5b},   StandIn{0x5a, 0x5e},
    StandIn{0xed, 0xf1},  StandIn{0x4d, 0x51},  StandIn{0x87, 0x8d},   StandIn{0x89, 0x8a},   StandIn{0x6a, 0x6e},
    StandIn{0xa1, 0xa4},  StandIn{0xa0, 0xa2},  StandIn{0x80, 0x82},   StandIn{0x7b, 0x7c},   StandIn{0x77, 0x7e},
    StandIn{0x7a, 0x7d},  StandIn{0x9a, 0x9c},  StandIn{0x79, 0x1a1},  StandIn{0x1a5, 0x1a2}, StandIn{0x13b, 0x1a3},
    StandIn{0x103, 0xfd}, StandIn{0x106, 0xff}, StandIn{0x105, 0x102},
};

// The modes list of `CfgRumbleArena` ends at the first of these.
constexpr std::int64_t kModesEnd = 0;
constexpr std::int64_t kModesEndAlt = -1;

// `text` in capitals, as `CfgRumbleChar` stores its name and description.
std::string upperCased(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return text;
}

// Whether the arena's `modes` list allows `mode`: a first value of 0 allows every mode.
bool allowsMode(std::span<const std::int64_t> modes, std::int64_t mode) {
    if (modes.empty() || modes.front() == kModesEnd) {
        return true;
    }
    for (const std::int64_t allowed : modes) {
        if (allowed == kModesEnd || allowed == kModesEndAlt) {
            break;
        }
        if (allowed == mode) {
            return true;
        }
    }
    return false;
}

} // namespace

bool addRumbleMode(RumbleData& data, const Unlockables& unlocks, RumbleModeEntry entry) {
    if (!unlocks.isUnlocked(UnlockKind::RumbleMode, entry.mode)) {
        return false;
    }
    data.modes.push_back(std::move(entry));
    return true;
}

bool addRumbleGang(RumbleData& data, const Unlockables& unlocks, std::int32_t id, std::string name,
                   const RumbleGangEntry::Roster& members) {
    if (id >= 0 && !unlocks.isUnlocked(UnlockKind::RumbleGang, id)) {
        return false;
    }
    RumbleGangEntry gang;
    gang.id = id;
    gang.name = std::move(name);
    for (std::size_t i = 0; i < members.size(); ++i) {
        gang.rosters[0].at(i) = rumbleStandIn(unlocks, members.at(i));
    }
    gang.rosters[1] = gang.rosters[0];
    gang.index = data.gangs.size();
    data.gangs.push_back(std::move(gang));
    return true;
}

bool addRumbleArena(RumbleData& data, const GameState& state, int levelNumber, std::uint32_t value,
                    std::span<const std::int64_t> modes) {
    if (!state.unlockables.isUnlocked(UnlockKind::RumbleArena, levelNumber) ||
        !allowsMode(modes, state.rumble.values.at(RumbleSetup::kGameType))) {
        return false;
    }
    // The entry keeps the level's record index, which the launch reads; an arena the table lacks is left out (Coney's
    // choice: it could not be launched).
    for (std::size_t index = 0; index < LevelTable::kCapacity; ++index) {
        const LevelRecord* record = state.levels.at(index);
        if (record != nullptr && record->number == static_cast<double>(levelNumber)) {
            data.arenas.push_back(RumbleArenaEntry{.levelNumber = levelNumber, .value = value, .levelIndex = index});
            return true;
        }
    }
    return false;
}

void addRumbleChar(RumbleData& data, std::uint32_t type, RumbleCharEntry entry) {
    entry.name = upperCased(std::move(entry.name));
    entry.bio = upperCased(std::move(entry.bio));
    data.chars.insert_or_assign(type, std::move(entry));
}

std::uint32_t rumbleStandIn(const Unlockables& unlocks, std::uint32_t type) {
    const auto pair = std::ranges::find(kStandIns, type, &StandIn::locked);
    if (pair == kStandIns.end() || unlocks.isUnlocked(UnlockKind::CharacterType, type)) {
        return type;
    }
    return pair->used;
}

} // namespace coney::gui
