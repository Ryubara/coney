// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/story_graph.h"
#include "warriors/profile_record.h"
#include "warriors/save_game.h"

namespace coney {

/// Mod missions' progress during a session: the save's story section (MissionProgress, keyed by mission id) and the
/// session's level numbers for mod missions, so that `UM_Unlock` and `UM_IsLevelComplete` on a mod level's number keep
/// the original's record fields for that mission under its id instead of in the 640 unlock bits and the 30 best
/// records, which stay the original missions'.
///
/// The original keeps a mission's progress as unlock records of its level number (`Unlockables_Unlock` `0x00423f70`):
/// `(level, 0, 0)` is its completion, which `UM_IsLevelComplete` asks (`Unlocks_IsLevelComplete` `0x004241d8`), and
/// `(level, 0, k)` its rank records, which `MissionCompleteUnlocks` (`global.lua`) unlocks from k = score /
/// PointsPerRank down to 1. A mod level's number is one no level record uses (the pool), so the original's search would
/// match no record and keep nothing; here the same calls fill the mission's story entry. A mod level has no other
/// records (no group 1 objectives, no rank past 32), so those calls keep nothing, as the original's search finds
/// nothing.
///
/// **Ownership (networking checklist):** simulation state of the story session, held in GameState. The entries are
/// the loaded profile's, put in by the save system when a profile loads and taken back when it saves; entries of mods
/// the session lacks are kept as they are. The numbers are the session's, the same on every machine (given in chain
/// order from the pool, #1300) and never saved: a save, a replay and a message name the id.
///
/// Research: docs/guides/studio/mission-mods.md#graph-progress, docs/research/save.md#save-mods
/// @coney-only the original's save has no mod missions (docs/research/save.md#save-mods)
class ModStoryProgress {
  public:
    /// The rank records a story entry keeps: `(level, 0, k)` for k 1-32 (MissionProgress::ranks).
    static constexpr std::uint8_t kRanks = 32;

    /// The story entries, in the order they were made or loaded.
    [[nodiscard]] const std::vector<MissionProgress>& entries() const { return m_entries; }
    /// Replaces the story entries with a loaded profile's (none for a new profile). The numbers stay.
    void setEntries(std::vector<MissionProgress> entries) { m_entries = std::move(entries); }
    /// The entry of mission `id`; null when it has none.
    [[nodiscard]] const MissionProgress* find(std::string_view id) const;
    /// Whether mission `id` is complete; false when it has no entry.
    [[nodiscard]] bool isComplete(std::string_view id) const;

    /// Gives mod mission `id` the session's level number `level` (a pool number, #1300), replacing what the number
    /// named before.
    void assign(std::uint8_t level, std::string id);
    /// Forgets every number: no level is a mod's.
    void clearNumbers() { m_numbers.clear(); }
    /// The mission whose number `level` is; null when it is no mod mission's.
    [[nodiscard]] const std::string* missionAt(std::uint8_t level) const;

    /// `UM_Unlock(level, group, item)` on a mod level's number: `(level, 0, 0)` completes its mission and
    /// `(level, 0, k)` for k 1-32 keeps rank record k; anything else keeps nothing. Returns false, keeping nothing,
    /// when `level` is no mod mission's (the caller then unlocks the original's records).
    /// @coney-only the route of `UM_Unlock` (`0x004237e8`) for a mod level (mission-mods.md#graph-progress)
    bool unlock(std::uint8_t level, std::uint8_t group, std::uint8_t item);
    /// `UM_IsLevelComplete(level)` on a mod level's number: whether its mission is complete. Nothing when `level` is no
    /// mod mission's (the caller then asks the original's records).
    /// @coney-only the route of `UM_IsLevelComplete` (`0x004238a8`) for a mod level (mission-mods.md#graph-progress)
    [[nodiscard]] std::optional<bool> isLevelComplete(std::uint8_t level) const;
    /// Keeps `best` as the best record of the mission numbered `level`, its four described fields (MissionBest's
    /// `rest` bytes have no place in the story section). Returns false, keeping nothing, when `level` is no mod
    /// mission's (the caller then keeps it in the 30 best records by the level's mission byte).
    /// @coney-only the best record of a mod mission (mission-mods.md#graph-progress)
    bool setBest(std::uint8_t level, const MissionBest& best);

  private:
    // The entry of mission `id`, made (incomplete, no ranks, no best) when it has none.
    MissionProgress& entry(const std::string& id);

    std::vector<MissionProgress> m_entries;
    std::map<std::uint8_t, std::string> m_numbers; // the session's level numbers of mod missions
};

/// The latest mission of `graph` that is complete, the step `findLastMission` goes on from: an original mission by
/// `isOriginalComplete(level)` (its `(level, 0, 0)` record), a mod mission by its story entry. Story entries of
/// missions the chain lacks (a mod the session does not run) are skipped, so the story goes on from the latest mission
/// present. Null when none is complete.
/// @coney-only the story graph's walk over both kinds of progress (mission-mods.md#graph-progress)
[[nodiscard]] const story::StoryStep* latestMission(const story::StoryGraph& graph,
                                                    const std::function<bool(int level)>& isOriginalComplete,
                                                    const ModStoryProgress& progress);
/// No walk over a temporary graph: the step returned points into it.
const story::StoryStep* latestMission(story::StoryGraph&& graph,
                                      const std::function<bool(int level)>& isOriginalComplete,
                                      const ModStoryProgress& progress) = delete;

/// The mods whose story entries name a mission `graph` lacks, each once, sorted: the mods a loaded save had that the
/// session does not run. Writes one warning naming each to the run log; their entries stay and are saved again.
/// @coney-only the warning of a save loaded without one of its story mods (mission-mods.md#graph-progress)
std::vector<std::string> warnMissingStoryMods(const ModStoryProgress& progress, const story::StoryGraph& graph);

} // namespace coney
