// SPDX-License-Identifier: GPL-3.0-or-later
// The level intro movie InitLevel plays (docs/research/level-loading.md#initlevel, step 12) and the movie player hook
// the front-end services pass movies to. Synthetic records; nothing from the disc.
#include "gamemodes/movie_player.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gamemodes/front_end_services.h"
#include "warriors/level_table.h"

namespace {

// A movie player that only records what it is asked to play.
class RecordingPlayer final : public coney::MoviePlayer {
  public:
    void playMovie(std::string_view name) override { played.emplace_back(name); }
    std::vector<std::string> played;
};

} // namespace

TEST_CASE("level intro movie: L<number>_IN for a record with the intro switch, first section only", "[movies]") {
    coney::LevelRecord level99;
    level99.name = "level99";
    level99.number = 99;
    level99.values.at(coney::LevelRecord::kIntroValue) = 1;
    CHECK(coney::levelIntroMovie(level99, 1) == std::optional<std::string>("L99_IN"));
    CHECK_FALSE(coney::levelIntroMovie(level99, 2).has_value());

    coney::LevelRecord level80;
    level80.name = "level80";
    level80.number = 80;
    CHECK_FALSE(coney::levelIntroMovie(level80, 1).has_value());
}

TEST_CASE("front-end services: movies go to the attached player, or are skipped", "[movies]") {
    std::vector<std::string> log;
    coney::FrontEndServices services([&log](std::string_view line) { log.emplace_back(line); });
    services.playMovie("LOGO");
    CHECK(log.back() == "movie: LOGO skipped (no movie player)\n");

    RecordingPlayer player;
    services.attachMoviePlayer(&player);
    services.playMovie("L99_IN");
    CHECK(player.played == std::vector<std::string>{"L99_IN"});
    CHECK(services.movies() == std::vector<std::string>{"LOGO", "L99_IN"});
}
