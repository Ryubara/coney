// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/pause_items.h"

#include <algorithm>
#include <array>

namespace coney::gui {

bool offersHangout(int level) {
    // The story levels played after the hangout opens (inferred), in the order the function tests them.
    static constexpr std::array<int, 15> kLevels{34, 2, 3, 5, 81, 86, 31, 14, 9, 51, 82, 92, 83, 20, 11};
    return std::ranges::find(kLevels, level) != kLevels.end();
}

PauseGridLayout pauseGrid(int level) {
    namespace s = pause_strings;
    const bool rumble = isRumbleLevel(level);
    PauseGridLayout layout;
    // Each item with its separator as the table gives it: none after Options (the end of row 1) and Quit.
    layout.items.push_back({PauseItem::Objectives, rumble ? s::kRules : s::kObjectives, true});
    if (!rumble) {
        layout.items.push_back({PauseItem::Stats, s::kStats, true});
    }
    layout.items.push_back({PauseItem::Options, s::kOptions, false});
    layout.items.push_back({PauseItem::Controls, s::kControls, true});
    if (rumble) {
        layout.items.push_back({PauseItem::Restart, s::kReplay, true});
    } else if (level != kHangoutLevel) {
        layout.items.push_back({PauseItem::Restart, s::kRestart, true});
    }
    layout.items.push_back({PauseItem::Resume, s::kResume, true});
    layout.items.push_back({PauseItem::Quit, s::kQuit, false});
    layout.rows = rumble || level == kHangoutLevel ? std::vector<std::size_t>{3, 3} : std::vector<std::size_t>{3, 4};
    return layout;
}

} // namespace coney::gui
