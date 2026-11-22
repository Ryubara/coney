// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

// A story save made in a level's fresh script state: the levels a test or the `--complete-levels` option names marked
// complete, as their missions' completion marks them. Coney's own: the original's story save comes from playing.
//
// Research: docs/research/story-progress.md#hub-doors

namespace coney::script {

class ScriptSystem;

/// Runs `global.lua`'s main chunk in the scripts' current state, which fills the unlock records (`CfgUnlockables`),
/// then calls the binding `UM_Unlock(level, 0, 0)` for each of `levels`, as a mission's completion does
/// (`MissionCompleteUnlocks`, `global.lua:f60`): each level is complete and what its completion unlocks is open (a
/// story mission's flashback trophy, `level84`'s Armies of the Night cabinet). Call it as the level's Lua state is
/// fresh, before its scripts (GameSession::startAtLevel()'s `ready`); does nothing without a state.
/// @coney-only debug and test set-up of a story save (`--complete-levels`)
void completeLevels(ScriptSystem& scripts, std::span<const int> levels);

} // namespace coney::script
