// SPDX-License-Identifier: GPL-3.0-or-later
// The Movies page (docs/guides/debug-menu.md#movies): the disc's movies with what plays each, a captions switch, and
// a choice that asks the game to play the movie and closes the menu so the movie's skip reaches it. Fake services;
// no game, no disc.
#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/menu_navigator.h"
#include "debug/pad_menu_input.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::ItemKind;
using coney::debug::TunableRegistry;

namespace {

// What the page asked the game to play.
struct Played {
    std::string name;
    bool captions = false;
};

// Services listing three movies whose play records each request in `played` and answers `accept`, a reference the
// test changes between requests.
DebugServices movieServices(std::vector<Played>& played, bool& accept) {
    DebugServices services;
    services.movies = [] { return std::vector<std::string>{"LOGO", "TRAILER", "L99_IN"}; };
    services.playMovie = [&played, &accept](std::string_view name, bool captions) {
        played.push_back(Played{.name = std::string(name), .captions = captions});
        return accept;
    };
    return services;
}

} // namespace

TEST_CASE("the Movies page lists the disc's movies with what plays each, after Missions", "[debug][movies]") {
    TunableRegistry tunables;
    std::vector<Played> played;
    bool accept = true;
    DebugSession session(tunables, movieServices(played, accept), nullptr);
    const auto titles = session.model().pageTitles();
    const auto missions = std::ranges::find(titles, "Missions");
    REQUIRE(missions != titles.end());
    REQUIRE(std::next(missions) != titles.end());
    CHECK(*std::next(missions) == "Movies");

    const auto page = session.model().openPage("Movies");
    REQUIRE(page != nullptr);
    const coney::debug::MenuItem* captions = page->find("Captions always");
    REQUIRE(captions != nullptr);
    CHECK(captions->kind == ItemKind::Toggle);
    CHECK(coney::debug::valueText(*captions) == "on");
    const coney::debug::MenuItem* logo = page->find("LOGO");
    REQUIRE(logo != nullptr);
    CHECK(logo->kind == ItemKind::Action);
    CHECK(coney::debug::valueText(*logo) == "start-up");
    REQUIRE(page->find("TRAILER") != nullptr);
    CHECK(page->find("TRAILER")->detail.empty());
    REQUIRE(page->find("L99_IN") != nullptr);
    CHECK(coney::debug::valueText(*page->find("L99_IN")) == "level99 intro");
    CHECK(played.empty());
}

TEST_CASE("choosing a movie asks the game to play it with the captions switch and closes the menu", "[debug][movies]") {
    TunableRegistry tunables;
    std::vector<Played> played;
    bool accept = true;
    DebugSession session(tunables, movieServices(played, accept), nullptr);
    coney::debug::MenuNavigator& navigator = session.navigator();
    navigator.open();
    // The root page: Favourites, then the pages in order; walk down to Movies and open it.
    const auto titles = session.model().pageTitles();
    const auto at = static_cast<int>(std::ranges::find(titles, "Movies") - titles.begin()) + 1;
    for (int i = 0; i < at; ++i) {
        navigator.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Down});
    }
    navigator.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Accept});
    REQUIRE(navigator.page() != nullptr);
    CHECK(navigator.page()->title() == "Movies");

    // The page's first item is the captions switch, then the movies: L99_IN is the fourth.
    for (int i = 0; i < 3; ++i) {
        navigator.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Down});
    }
    navigator.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Accept});
    REQUIRE(played.size() == 1);
    CHECK(played.front().name == "L99_IN");
    CHECK(played.front().captions);
    CHECK_FALSE(navigator.isOpen());
    CHECK(session.log().last(1).front() == "movies: playing L99_IN");

    // Reopened, the menu is on the Movies page where it was; captions off, then a movie the game refuses.
    navigator.open();
    REQUIRE(navigator.page() != nullptr);
    CHECK(navigator.page()->title() == "Movies");
    session.model().openPage("Movies")->find("Captions always")->setBool(false);
    accept = false;
    session.model().openPage("Movies")->find("LOGO")->run();
    REQUIRE(played.size() == 2);
    CHECK_FALSE(played.back().captions);
    CHECK(navigator.isOpen());
    CHECK(session.log().last(1).front() == "movies: LOGO could not be played (no such movie, or one is playing)");
}

TEST_CASE("the Movies page says so in a run with no movies", "[debug][movies]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Movies");
    REQUIRE(page != nullptr);
    const coney::debug::MenuItem* none = page->find("No movies");
    REQUIRE(none != nullptr);
    CHECK(none->kind == ItemKind::Watch);
}
