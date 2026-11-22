// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the disc tests play under the engine limits preset
// (docs/guides/running-options.md#engine-limits): the game session a playthrough test drives (DiscSession) and the game
// from boot (DiscGame) pin every lifted limit at the original's number, so they compare with the PS2 one to one, and a
// session's settings can still ask for Coney's own rule. It runs only when the environment variable CONEY_DISC names
// the disc and skips otherwise; it prints nothing from the disc (LEGAL.md).

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/brains.h"
#include "core/engine_limits.h"
#include "gamemodes/gameplay_mode.h"
#include "platform/game_session.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/test_paths.h"

TEST_CASE("the disc tests' sessions play under the engine limits preset unless their settings ask for Coney's",
          "[disc][engine-limits]") {
    // Expected: unverified, #1591 (Coney's own rule: the runs that compare with the PS2 pin the original's limits)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    {
        // A playthrough's session: level99 entered, its brains and gangs at the original's limits.
        coney::test::DiscSession run(*wad, "");
        CHECK(run.session().flow().gameplay().engineLimits() == coney::EngineLimits::Original);
        run.session().startAtLevel("level99", 1);
        REQUIRE(run.runUntilPlay());
        const coney::ai::Brains* brains = run.session().flow().gameplay().brains();
        REQUIRE(brains != nullptr);
        CHECK(brains->engineLimits() == coney::EngineLimits::Original);
        CHECK(brains->gangs().engineLimits() == coney::EngineLimits::Original);
    }
    {
        // Asked for in the settings, Coney's own rule.
        const coney::platform::GameSessionSettings settings{.skipMovies = true,
                                                            .rumble = std::nullopt,
                                                            .profiles = std::nullopt,
                                                            .engineLimits = coney::EngineLimits::Coney};
        coney::test::DiscSession run(*wad, "", settings);
        CHECK(run.session().flow().gameplay().engineLimits() == coney::EngineLimits::Coney);
    }
    {
        // The game from boot.
        coney::test::DiscGame game(*wad, "");
        CHECK(game.flow().gameplay().engineLimits() == coney::EngineLimits::Original);
    }
}
