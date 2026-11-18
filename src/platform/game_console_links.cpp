// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_console_links.h"

#include <string>

// @coney-only what the console takes from the running game (docs/guides/console.md#in-the-game); the original has no
// console.

namespace coney::platform {

// Stub: no setting is made yet, so the tests below fail.
GameSetting vsyncSetting(RenderEngine& /*renderer*/) { return {}; }

// Stub: the settings are not made yet, so the tests below fail.
std::vector<GameSetting> playerSettings(const GameConsoleSources& /*sources*/) { return {}; }

SessionFacts sessionFacts(GameSession* session) {
    if (session == nullptr) {
        return {};
    }
    SessionFacts facts{.exists = true, .inPlay = session->inPlay(), .level = {}};
    if (facts.inPlay) {
        facts.level = std::string(session->flow().levelFlow().currentLevel());
    }
    return facts;
}

// Stub: the links are empty, so the tests below fail.
GameConsoleLinks makeGameConsoleLinks(const GameConsoleSources& /*sources*/) { return {}; }

} // namespace coney::platform
