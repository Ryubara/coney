// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>
#include <vector>

#include "core/story_graph.h"
#include "core/story_merge.h"
#include "core/story_numbers.h"

// The story graph in the game's scripts: Coney's `findLastMission` and `runNextMission` natives, set over the two
// `global.lua` functions of those names at every level entry, the hub's offer written into `fRunMission` and its
// chapter set at the hub load, and the session's level numbers and level records for mod missions. Coney's own: the
// original keeps the story's order in its scripts' tables, and has no mods.
//
// Research: docs/guides/studio/mission-mods.md#graph-natives, docs/research/story-progress.md

namespace coney {
struct GameState;
class ModStoryProgress;
} // namespace coney

namespace coney::script {

class ScriptSystem;

/// Sets two natives in the scripts' current state, over the `global.lua` functions of the same names:
///
/// - `findLastMission()` returns the level number of the latest mission of `graph` that is complete, else 0: an
///   original mission by the scripts' own `UM_IsLevelComplete(level)`, which the Lua asks too, a mod mission by its
///   story entry in `progress` (latestMission()).
/// - `runNextMission(arg)` sets `LastLevel` from `findLastMission()` when it is 0 or nil, then, when `graph` has a step
///   after it (StoryGraph::nextLoad()), calls `SetCheckPoint(checkpoint)`, `SoundStopMusicTrack()` and
///   `MenuLoadLevel("level" .. level)`, and `HUDLaunchMissionComplete(4)` when `arg` is 1, each by its name, as the
///   Lua does. With no step after it nothing loads, as after the Lua's call of a name no binding has.
///
/// `graph` and `progress` must outlive the state. Does nothing without a state.
/// @coney-only replaces `findLastMission` and `runNextMission` (`global.lua:f62`, `f61`) with the story graph's
void installStoryNatives(ScriptSystem& scripts, const story::StoryGraph& graph, const ModStoryProgress& progress);

/// The hub load, once `level95.lua`'s main chunk has run at `checkpoint`: plans the chapter (story::planHubChapter())
/// from the latest mission complete (latestMission(), an original mission by `UM_IsLevelComplete` as above) and
/// applies it (applyHubChapter()): the chapter's offer goes into `fRunMission[checkpoint].level`, and a new chapter's
/// actions replace chapter n's. Does nothing without a state or when `merge.graph` has no chapter at `checkpoint`.
/// @coney-only replaces `fRunMission`'s rows (`level95.lua`'s main chunk) with the story graph's offers
void enterHub(ScriptSystem& scripts, const story::StoryMerge& merge, const ModStoryProgress& progress, int checkpoint);

/// The story graph in a session's scripts: the session's merged story, numbered once the level table is filled, and
/// what runs at each level entry (attach()).
///
/// **Ownership (networking checklist):** simulation state of the story session. The merge is fixed from the session's
/// start; numberMissions() gives the same numbers on every machine that has the same mods and the same level records,
/// and the numbers are never saved. Holds references to the session's script system and game state, which must outlive
/// it.
///
/// Research: docs/guides/studio/mission-mods.md#graph-natives
class StoryHooks {
  public:
    /// The level name of the front end, after which a story session starts.
    static constexpr std::string_view kFrontEnd = "level100";

    /// The story `merge` of the session over `scripts` and `state`; nothing is numbered or attached yet.
    StoryHooks(ScriptSystem& scripts, GameState& state, story::StoryMerge merge);

    /// The session's story: the merge, with its mod missions numbered once numberMissions() has run.
    [[nodiscard]] const story::StoryMerge& merge() const { return m_merge; }
    /// The numbers numberMissions() gave, in chain order.
    [[nodiscard]] const std::vector<story::NumberedMission>& numbers() const { return m_numbers; }

    /// Makes the script system call afterGlobal() and afterLevel() at every level entry.
    /// @coney-only Coney's natives over the story graph (docs/guides/studio/mission-mods.md#graph-natives)
    void attach();

    /// Numbers the mod missions once, from the pool of the numbers 2-59 the level table's records leave free
    /// (story::numberModMissions()), as many as the table has free records for: each numbered mission gets a level
    /// record `level<n>` at a free index, with level number n, and its number in the game state's ModStoryProgress
    /// (ModStoryProgress::assign()); a mission past the pool is refused with its message. Returns false, numbering
    /// nothing, while the level table is empty (the preloads have not filled it); true once numbered.
    /// @coney-only the session's numbers of mod missions (docs/guides/studio/mission-mods.md#graph-natives)
    bool numberMissions();

    /// After `global.lua`'s main chunk at the entry of `level`: numbers the mod missions if not done yet, sets the two
    /// natives (installStoryNatives()), and, at the first level after the front end (a story session's start), names
    /// in the run log each story mod a loaded save has that the session lacks (warnMissingStoryMods()).
    /// @coney-only Coney's natives over the story graph (docs/guides/studio/mission-mods.md#graph-natives)
    void afterGlobal(std::string_view level);
    /// After the level's own script: at the hub (`level95`), the hub load (enterHub()) at the game state's checkpoint.
    /// @coney-only the hub's offer from the story graph (docs/guides/studio/mission-mods.md#graph-natives)
    void afterLevel(std::string_view level);

  private:
    ScriptSystem& m_scripts;
    GameState& m_state;
    story::StoryMerge m_merge;
    std::vector<story::NumberedMission> m_numbers;
    bool m_numbered = false;
    bool m_fromFrontEnd = true; // the level entered before was the front end (or none): a story session starts
};

} // namespace coney::script
