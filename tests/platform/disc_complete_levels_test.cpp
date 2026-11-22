// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that script::completeLevels(), behind `coney --complete-levels`, makes the
// story save a level starts from (docs/guides/running-play-level.md#complete-levels): `level95` started as
// `--play-level` starts it, with the levels marked complete as its Lua state is fresh, has them complete and offers
// what their completion unlocks (level84's opens the arcade cabinet, docs/research/story-progress.md#hub-doors). It
// runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only
// (LEGAL.md).

#include <array>
#include <cstdio>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/start_up_flow.h"
#include "platform/game_session.h"
#include "scripting/message_handlers.h"
#include "scripting/story_save.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

TEST_CASE("the disc's level95 from a story save the complete-levels set-up made: the levels complete, the cabinet "
          "offered",
          "[disc][story][hub]") {
    // Expected: story-progress.md#hub-doors, confirmed (code) at level95.lua:f73 and global.lua's Unlockables:
    // level84's completion opens level60's type-9 record, so flashback.Setup gives the cabinet (fMenu[4]) a prompt.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    coney::StartUpFlow& flow = run.session().flow();
    constexpr std::array<int, 2> kComplete{99, 84};
    run.session().startAtLevel("level95", 11,
                               [&flow, &kComplete] { coney::script::completeLevels(flow.scripts(), kComplete); });
    REQUIRE(run.runUntilPlay());
    const coney::GameState& state = flow.state();
    const bool complete99 = state.player.unlocks.isLevelComplete(state.saved, 99);
    const bool complete84 = state.player.unlocks.isLevelComplete(state.saved, 84);
    const bool complete82 = state.player.unlocks.isLevelComplete(state.saved, 82);
    const std::optional<double> cabinet = flow.flags().findByName("fMenu_4");
    REQUIRE(cabinet.has_value());
    const coney::script::MessageHandlers* messages = flow.context().messages;
    REQUIRE(messages != nullptr);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const bool offered = messages->prompts().contains(*cabinet);
    std::printf("  complete-levels: 99 complete %d, 84 complete %d, 82 complete %d, cabinet offered %d\n",
                complete99 ? 1 : 0, complete84 ? 1 : 0, complete82 ? 1 : 0, offered ? 1 : 0);
    CHECK(complete99);
    CHECK(complete84);
    CHECK_FALSE(complete82);
    CHECK(offered);
}
