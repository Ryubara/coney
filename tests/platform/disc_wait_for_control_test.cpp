// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `--wait-for-control` gives a trace scenario a player who answers the pad
// (issue #1680). `level99` checkpoint 3 opens with the scene `l99_c3`, which holds the pad for several hundred updates,
// puts Rembrandt where its script wants (overriding `--start`) and then leaves the pad off until a script gives it back
// (docs/research/scripting.md#level99-checkpoints). A scenario that played its stick after a fixed 300 updates (the old
// `settle`) pushed it into a player who did not answer, so its compare recorded a standing player. With the run waiting
// for control, the pad script's frame 0, the start place and the trace's step 1 all come on the first update he has it.
// The level plays in the game session as `--play-level level99 --checkpoint 3 --start ... --wait-for-control` plays it
// (support/disc_session.h). It runs only when the environment variable CONEY_DISC names the disc and skips otherwise;
// it prints counts and metres only (LEGAL.md).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/options.h"
#include "fileio/wad.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// Walk 60 % up from the first step of the run, the scenarios' walk (research/traces/scenarios/walk60.txt).
constexpr std::string_view kWalk = "0 stick left 0 60\n";
// `walk60`'s start: where the original's slot 1 stands, in the street past the lesson's fence.
constexpr coney::StartPlace kStreet{.x = 75.155F,
                                    .y = 41.1354F,
                                    .z = 0.2231F,
                                    .headingDegrees = 91.909F,
                                    .cameraDistance = std::nullopt,
                                    .cameraYawDegrees = std::nullopt};
// The opening's length that the old fixed settle stood in for: the scene alone holds him for longer than this.
constexpr std::uint64_t kOldSettle = 300;
// The most the opening may take before the test calls it hung (the level's own script gives the pad back at about 750).
constexpr std::uint64_t kWaitLimit = 1500;

// The feet's distance in the ground plane from the start place.
float fromStreet(const coney::platform::PlayLevelMode& play) {
    const coney::anim::Vec3 feet = play.playerFeet();
    return std::hypot(feet.x - kStreet.x, feet.y - kStreet.y);
}

} // namespace

TEST_CASE("the disc's level99 checkpoint 3: a run that waits for control starts at --start and the stick moves him",
          "[disc][story][trace]") {
    // Expected: Coney's own runs of the level (docs/guides/running-play-level.md#wait-for-control): the l99_c3 scene
    // (docs/research/scripting.md#level99-checkpoints) holds him past 300 updates and the pad stays off until about
    // update 750; the walk's 1.56 m a second is docs/research/feel.md's; the start place is slot 1's first trace row
    // (research/traces/scenarios/walk60.toml).
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kWalk);
    run.waitForControl(kStreet);
    run.session().startAtLevel("level99", 3);
    REQUIRE(run.runUntilPlay());
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);

    // The opening: the scene holds him (and takes his place) for longer than the old fixed count, and the script has
    // not started while it does.
    REQUIRE_FALSE(play->controlReady());
    run.run(kOldSettle);
    CHECK_FALSE(play->controlReady());
    CHECK(run.playFrames() == 0);

    // Control comes within the opening's length, and the run's first update is the one that has it.
    const bool started = run.runUntil([&run] { return run.playFrames() >= 1; }, kWaitLimit);
    REQUIRE(started);
    const std::uint64_t waited = run.frames();
    std::printf("level99 checkpoint 3: control after %llu frames of the run\n",
                static_cast<unsigned long long>(waited));
    CHECK(play->hasControl());
    // `--start` placed him on that update (the stick has moved him one step of a walk at most), not the scene.
    CHECK(fromStreet(*play) < 0.5F);

    // Then the stick walks him: a 60 % walk covers about 1.5 m a second, so 60 updates take him past 1 m.
    run.run(60);
    const float walked = fromStreet(*play);
    std::printf("level99 checkpoint 3: %.2f m walked in 60 updates of the stick\n", static_cast<double>(walked));
    CHECK(walked > 1.0F);
}

TEST_CASE("the disc's level99 checkpoint 3: without waiting, the opening's hold is why a fixed count records a "
          "standing player",
          "[disc][story][trace]") {
    // Expected: the same runs as above (docs/guides/running-play-level.md#wait-for-control): until about update 535 the
    // scene holds him where the level put him, so a stick played at update 300 moves him not at all.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The run as `settle = 300` ran it: the pad script's frame 0 at the level's first frame of play, 300 updates of the
    // opening shifted in front of a walk. He has not moved from the scene's hold when the walk's first 60 updates end.
    coney::test::DiscSession run(*wad, "300 stick left 0 60\n");
    run.session().startAtLevel("level99", 3);
    REQUIRE(run.runUntilPlay());
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    run.run(kOldSettle);
    const coney::anim::Vec3 held = play->playerFeet();
    run.run(60);
    const coney::anim::Vec3 after = play->playerFeet();
    const float moved = std::hypot(after.x - held.x, after.y - held.y);
    std::printf("level99 checkpoint 3, no wait: %.3f m moved in 60 updates of the stick\n", static_cast<double>(moved));
    CHECK(moved < 0.05F);
    CHECK_FALSE(play->hasControl());
}
