// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>
#include <vector>

#include "core/story_merge.h"

// The session's level numbers for mod missions: the game's scripts pass levels as numbers, so each mission a story mod
// places gets one, from the numbers that pass the scripts' range checks as a story mission's does. Coney's own, with no
// counterpart in the original, which has no mods.
//
// Research: docs/guides/studio/mission-mods.md#graph-natives, docs/research/story-progress.md#readers

namespace coney::story {

/// The lowest number a mod mission may get: `GetLevel` (`global.lua:f64`) turns 0 into 1, and 1 is the intro level's.
inline constexpr int kFirstPoolNumber = 2;
/// The highest: 60-69 is an Armies stage to the engine, and the mission menu lists only 2-99 but 95 and 60-65.
inline constexpr int kLastPoolNumber = 59;

/// The pool: the numbers kFirstPoolNumber to kLastPoolNumber that no level record uses (`usedNumbers`, the records'
/// level numbers in any order), ascending.
[[nodiscard]] std::vector<int> levelNumberPool(std::span<const int> usedNumbers);

/// A mod mission and the number the session gave it.
struct NumberedMission {
    std::string id; ///< Its story id, `<mod>.<id>`.
    int level = 0;  ///< Its level number for the session: `level<n>` is its level record's name.

    friend bool operator==(const NumberedMission&, const NumberedMission&) = default;
};

/// Gives each mod mission of `merge.graph` (a mission step with a StoryStep::modId), in chain order, the next number of
/// `pool`, and returns them in that order. A mission past the pool is refused: it leaves the chain with the new hub
/// chapter it opened, leaves `merge.placed`, and a refusal with its message joins `merge.refusals`; a refused
/// replacement of an original mission gives that mission its place back. Every mission anchored on a refused one comes
/// after it in the chain, so it is past the pool too and refused the same way. Without mod missions nothing changes.
/// @coney-only the session's numbers of mod missions (docs/guides/studio/mission-mods.md#graph-natives)
std::vector<NumberedMission> numberModMissions(StoryMerge& merge, std::span<const int> pool);

} // namespace coney::story
