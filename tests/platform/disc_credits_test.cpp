// SPDX-License-Identifier: GPL-3.0-or-later
// A check against the player's own disc that the end credits run as level84_end runs them
// (docs/research/frontend.md#fn-credits): in a level started as `coney --play-level` starts it, `PreloadCredits` runs
// the disc's `credits.lua` into the list, `ShowCredits` starts the scroll, the HUD steps it in play (it ignores the
// pad: the scroll takes no input), and once the last line has passed the top the game calls the Lua `CreditEnd`. It
// runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only
// (LEGAL.md).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "hud/credits.h"
#include "hud/hud.h"
#include "platform/game_session.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

TEST_CASE("the disc's credits.lua fills the credits, which scroll to their end and call CreditEnd",
          "[disc][hud][credits]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    coney::script::ScriptSystem& scripts = run.session().flow().scripts();
    coney::hud::Hud& hud = run.session().flow().hud();

    // level84_end's calls, with a CreditEnd that counts.
    std::size_t ends = 0;
    scripts.vm().registerFunction("CreditEnd", [&ends](std::span<const coney::script::Value> /*args*/) {
        ++ends;
        return std::vector<coney::script::Value>{};
    });
    REQUIRE(scripts.call("PreloadCredits"));
    const coney::hud::Credits& credits = hud.credits();
    REQUIRE(credits.loaded());
    const std::size_t lines = credits.lines().size();
    REQUIRE(scripts.call("ShowCredits"));
    REQUIRE(credits.running());
    const std::uint64_t shownAt = run.frames();

    // Ten seconds of play: lines start at the bottom and move up.
    run.run(300);
    std::size_t started = 0;
    for (const coney::hud::CreditLine& line : credits.lines()) {
        started += line.started ? 1 : 0;
    }
    CHECK(started > 0);
    CHECK(credits.freeWidgets() < coney::hud::kCreditWidgets);

    // The rest of the scroll: up to an hour of play, then the next frame of play calls CreditEnd.
    REQUIRE(run.runUntil([&credits] { return !credits.running(); }, 30ULL * 60 * 60));
    run.run(2);
    std::printf("  credits: %zu lines from credits.lua, %zu started in the first 10 s, %llu frames to the end, %zu "
                "CreditEnd calls\n",
                lines, started, static_cast<unsigned long long>(run.frames() - shownAt), ends);
    CHECK(lines == 578);
    CHECK(ends == 1);
}
