// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

namespace coney::debug {

/// One of the story's 18 missions, as the debug menu's Missions page lists them: its number in the story, its title,
/// its level and how many checkpoints (sections) the level has. A curated list (names and numbers, no game text beyond
/// the titles the reference lists already carry): research/references/levels.yaml (the titles and sections) and
/// research/missions.yaml (the order), which a Python test checks it against.
struct StoryMission {
    int number; ///< 1 to 18, the order the story plays them in (docs/research/scripting.md#run-next-mission).
    std::string_view title; ///< The mission's name on the disc (GSTRING.MISSIONNAME).
    std::string_view level; ///< The level it plays in (`level99`).
    int checkpoints;        ///< The level's sections: checkpoints 1 to this are valid.
};

/// The story missions in play order.
[[nodiscard]] const std::array<StoryMission, 18>& storyMissions();

} // namespace coney::debug
