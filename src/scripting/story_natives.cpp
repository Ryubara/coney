// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/story_natives.h"

#include <utility>

#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney::script {

// Stub for the test commit: the scripts keep global.lua's own functions.
void installStoryNatives(ScriptSystem& /*scripts*/, const story::StoryGraph& /*graph*/,
                         const ModStoryProgress& /*progress*/) {}

// Stub for the test commit: the hub keeps its own offers.
void enterHub(ScriptSystem& /*scripts*/, const story::StoryMerge& /*merge*/, const ModStoryProgress& /*progress*/,
              int /*checkpoint*/) {}

StoryHooks::StoryHooks(ScriptSystem& scripts, GameState& state, story::StoryMerge merge)
    : m_scripts(scripts), m_state(state), m_merge(std::move(merge)) {}

// Stub for the test commit.
void StoryHooks::attach() {}

// Stub for the test commit.
bool StoryHooks::numberMissions() { return false; }

// Stub for the test commit.
void StoryHooks::afterGlobal(std::string_view /*level*/) {}

// Stub for the test commit.
void StoryHooks::afterLevel(std::string_view /*level*/) {}

} // namespace coney::script
