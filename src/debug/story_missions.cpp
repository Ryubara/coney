// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/story_missions.h"

namespace coney::debug {

const std::array<StoryMission, 18>& storyMissions() {
    // Kept in step with research/missions.yaml and research/references/levels.yaml by
    // python/tests/test_story_missions.py.
    static constexpr std::array<StoryMission, 18> kMissions{{
        {1, "New Blood", "level99", 3},
        {2, "Real Live Bunch", "level80", 4},
        {3, "Payback", "level87", 5},
        {4, "Blackout", "level34", 5},
        {5, "Real Heavy Rep", "level2", 4},
        {6, "Writer's Block", "level3", 5},
        {7, "Adios Amigo", "level5", 4},
        {8, "Encore", "level81", 5},
        {9, "Payin' The Cost", "level86", 4},
        {10, "Destroyed", "level93", 6},
        {11, "Boys In Blue", "level31", 6},
        {12, "Set Up", "level14", 3},
        {13, "All-City", "level9", 3},
        {14, "Desperate Dudes", "level51", 7},
        {15, "No Permits, No Parley", "level52", 4},
        {16, "Home Run", "level54", 5},
        {17, "Friendly Faces", "level55", 3},
        {18, "Come Out To Play", "level84", 3},
    }};
    return kMissions;
}

} // namespace coney::debug
