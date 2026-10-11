// SPDX-License-Identifier: GPL-3.0-or-later
// The debug menus' game services over a game session (docs/guides/debug-menu.md): Coney's own debug tooling, in the
// debug tools' target (docs/guides/building.md#debug-tools).
#include "platform/game_session_debug.h"

#include <string>
#include <string_view>
#include <vector>

namespace coney::platform {

void connectDebugServices(debug::DebugServices& services, const std::function<GameSession*()>& session) {
    services.scripts = [session]() -> script::ScriptSystem* {
        GameSession* game = session();
        return game != nullptr ? &game->flow().scripts() : nullptr;
    };
    services.recorded = [session]() -> const script::RecordedCalls* {
        GameSession* game = session();
        return game != nullptr ? &game->flow().recorded() : nullptr;
    };
    services.gameState = [session]() -> GameState* {
        GameSession* game = session();
        return game != nullptr ? &game->flow().state() : nullptr;
    };
    // The Movies page: the disc's movies, each played over the game, which resumes as it was.
    services.movies = [session] {
        GameSession* game = session();
        return game != nullptr ? game->discMovieNames() : std::vector<std::string>{};
    };
    services.playMovie = [session](std::string_view name, bool captions) {
        GameSession* game = session();
        return game != nullptr && game->playDebugMovie(name, captions);
    };
    // The Play pages list only the levels whose level file is on the disc (with no session, every one).
    services.levelLoads = [session](std::string_view name) {
        const GameSession* game = session();
        return game == nullptr || game->levelLoads(name);
    };
}

} // namespace coney::platform
