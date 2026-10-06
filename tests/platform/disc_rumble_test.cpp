// SPDX-License-Identifier: GPL-3.0-or-later

// Rumble matches played on the player's own disc, from boot through the menus, headless, with the game's own scripts
// (docs/research/rumble.md): a QUICK RUMBLE Brawl the player loses ends on the result screen with the other gang's
// win. They run only when CONEY_DISC names the disc and skip otherwise; they print counts only (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/rumble_result_mode.h"
#include "support/disc_play_fixtures.h"

TEST_CASE("the disc's QUICK RUMBLE Brawl the player loses ends on the result screen with the Orphans' win",
          "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Through the menus and the intro, then the pad left alone: the Orphan confronts the Fury and beats him.
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript);
    game.run(1000);
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    CHECK_FALSE(game.flow().rumbleIntro().showing());

    // The Fury's side has nobody standing: the win sequence (3 s) and then the result screen, mode 0x14, naming the
    // Orphans. Within five minutes of play.
    constexpr std::uint64_t kLimit = 9000;
    const bool ended = game.runUntilTop(coney::RumbleResultMode::kId, kLimit);
    for (const std::string& line : game.log()) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(ended);
    const coney::RumbleResultMode& result = game.flow().rumbleResult();
    CHECK(result.winner().find("ORPHANS") != std::string::npos);
    CHECK_FALSE(result.reason().empty());
    CHECK(game.flow().scripts().errors() == 0);
    std::printf("  rumble lost: result screen at frame %llu, %zu log lines\n",
                static_cast<unsigned long long>(game.frames()), game.log().size());
}
