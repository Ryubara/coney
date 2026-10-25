// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise Coney's debug entry through the player's own disc. No game bytes or script source are fixtures.
#include <cstdio>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "platform/game_session.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

// A level-owned scene test must supply its cast and replay without a direct scene-player shortcut.
// Expected: docs/research/scenes.md#scene-test (level-owned cast and replay loop; normal global reset).
TEST_CASE("scene-test disc entry runs the level's cast and loop then reloads normally", "[disc][scene-test][session]") {
    auto wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    run.session().startSceneTest("level99", "l99_c1");
    REQUIRE(run.runUntilPlay());
    auto& flow = run.session().flow();
    REQUIRE(flow.gameplay().scenes() != nullptr);
    CHECK(flow.scripts().vm().global("SCENETEST").string().value_or("") == "l99_c1");
    CHECK_FALSE(flow.humans().all().empty());
    auto* scenes = flow.gameplay().scenes();
    const auto id = scenes->idOf("l99_c1");
    REQUIRE(id.has_value());
    std::optional<float> previous;
    bool looped = false;
    // A looping task restarts its tracks, rather than ending and starting another task.
    const bool observedLoop = run.runUntil(
        [&] {
            const auto frame = scenes->frame(*id);
            if (frame && previous && *frame + 1.0F < *previous) {
                looped = true;
            }
            previous = frame;
            return looped;
        },
        6000);
    const auto stats = scenes->stats();
    REQUIRE(observedLoop);
    CHECK(scenes->playing());
    CHECK(stats.started == 1);
    CHECK(stats.ended == 0);
    CHECK(stats.aborted == 0);
    CHECK(flow.scripts().errors() == 0);
    std::printf("  scene-test: %u track loops, %llu starts, %llu ends, %llu aborts\n", looped ? 1U : 0U,
                static_cast<unsigned long long>(stats.started), static_cast<unsigned long long>(stats.ended),
                static_cast<unsigned long long>(stats.aborted));

    // A later ordinary request replaces a queued scene test, with no debug flag leaking across the reload.
    REQUIRE(run.session().jumpToSceneTest("level99", "l99_c1"));
    REQUIRE(run.session().jumpToLevel("level99", 2));
    run.run(1);
    REQUIRE(run.runUntilPlay());
    CHECK(flow.scripts().vm().global("SCENETEST").isNil());
    CHECK(flow.state().checkPoint == 2);
    CHECK(flow.scripts().errors() == 0);
}
