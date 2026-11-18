// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the game's humans' tests: they compile and fail until the capture is written.
#include "net/game_humans.h"

namespace coney::net {

HumanState replicatedState(const human::Human& /*human*/) { return {}; }

void GameHumans::capture(const ai::Brains& /*brains*/) {}

void GameHumans::clear() {}

SnapshotHumans GameHumans::holding() const { return {}; }

} // namespace coney::net
