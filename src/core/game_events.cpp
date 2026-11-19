// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the tests' first run (the test commit): the next commit implements these.
#include "core/game_events.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coney {

bool validGameEventKind(std::uint8_t /*kind*/) { return false; }

std::uint8_t gameEventFields(GameEventKind /*kind*/) { return 0; }

bool reliableGameEvent(GameEventKind /*kind*/) { return false; }

std::string_view describe(GameEventKind /*kind*/) { return "unknown"; }

std::string describe(const GameEvent& /*event*/) { return {}; }

void raiseSceneChanges(GameEvents* /*events*/, std::vector<std::uint32_t>& before, std::vector<std::uint32_t> now) {
    before = std::move(now);
}

void GameEvents::raise(GameEvent event) { m_raised.push_back(std::move(event)); }

std::vector<GameEvent> GameEvents::take() { return {}; }

} // namespace coney
