// SPDX-License-Identifier: GPL-3.0-or-later
// The Movies page (docs/guides/debug-menu.md#movies): Coney's own debug tooling, no part of the original, which plays
// movies only from its own callers (docs/research/movies.md#callers).
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "gamemodes/movie_player.h"

namespace coney::debug {

void addMoviesPage(DebugSession& session) {
    // The captions switch outlives each refresh of the page: on by default, since an English game's subtitle option
    // is off and would hide every movie's captions.
    auto captions = std::make_shared<bool>(true);
    session.model().addPage(
        "Movies",
        [&session, captions](MenuPage& page) {
            const std::vector<std::string> movies =
                session.services().movies ? session.services().movies() : std::vector<std::string>{};
            if (movies.empty()) {
                page.add(watchItem("No movies", [] { return std::string("run the game from a disc"); }))
                    .withHelp("The movies are read from the disc's PSS folder by the game session.");
                return;
            }
            page.add(toggleItem(
                         "Captions always", [captions] { return *captions; }, [captions](bool on) { *captions = on; }))
                .withHelp("On: a movie's captions show whatever the game's subtitle option says.");
            for (const std::string& name : movies) {
                // Choosing a movie closes the menu: while it is open the game sees no buttons, so the movie's skip
                // (any button) could not reach it.
                // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
                MenuItem item = actionItem(name, [&session, captions, name] {
                    const auto& play = session.services().playMovie;
                    if (!play) {
                        session.print("movies: cannot play movies in this run");
                        return;
                    }
                    if (!play(name, *captions)) {
                        session.print(
                            std::format("movies: {} could not be played (no such movie, or one is playing)", name));
                        return;
                    }
                    session.print(std::format("movies: playing {}", name));
                    session.navigator().close();
                });
                item.detail = movieUse(name);
                item.help = "Plays it full screen; any button but LOGO's skips it, and the game resumes as it was.";
                page.add(std::move(item));
            }
            page.add(logItem("Log", [&session] { return session.log().last(3); }));
        },
        "The disc's movies; play one over the game, which then resumes where it was.");
}

} // namespace coney::debug
