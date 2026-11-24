// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

namespace coney::ai {
class Brain;
class Brains;
} // namespace coney::ai

// The input script's `drop-ai` (docs/guides/running-input.md#input-scripts): a debug aid that puts an AI human far
// below the world, so the characters' step takes him away as it takes a human who fell out of it
// (docs/research/characters-ground.md#out-of-world). A build with the debug tools only; the release build's game has no
// sink for the line.

namespace coney::debug {

/// How far below his feet `drop-ai` puts an AI human, metres: past human::Human::kOutOfWorldDepth under the lowest
/// point of any level he stood on.
inline constexpr float kDropDepth = 200.0F;

/// The AI human `drop-ai` drops among `brains`: the one whose handle is `handle` when given, else the nearest to
/// `player` who is not his friend (ai::Gangs::friends(), so a crew stays); never the player, nor a human already out
/// of the world. Null when there is none.
/// @coney-only a debug aid: the original has no way to drop a human
[[nodiscard]] ai::Brain* dropTarget(ai::Brains& brains, const ai::Brain& player, std::optional<double> handle);

/// Places `brain`'s human kDropDepth below his feet, on no ground and facing as he faced, with the brain's part of a
/// placement (ai::Brain::placed()). The next characters' step finds him out of the world.
/// @coney-only a debug aid: the original has no way to drop a human
void dropBelowWorld(ai::Brain& brain);

} // namespace coney::debug
