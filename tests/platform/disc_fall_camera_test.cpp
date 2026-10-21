// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the follow camera stays with a player in a long fall as the original's
// does (issue #111, docs/research/camera.md#long-fall): `level99` checkpoint 3 played in the game session as `coney
// --play-level level99 --checkpoint 3` plays it (support/disc_session.h), player 1 put 40 m above the street's end
// (where the original's `fall_high` trace starts him, the stick at rest) and left to fall. The aim point's lag must
// climb past 0.18 and the aim point stay under 5 m above the look-at point, as the original's trace shows (0.188 and
// 4.7 m at update 72 of the fall); built from the horizontal speed alone the lag stayed 0.06 and the aim point rose
// about 18 m above. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// numbers only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The `fall_high` trace's start (docs/research/camera.md#long-fall): the street's end in checkpoint 3, heading 91.9°,
// the feet 40 m up.
constexpr float kStartX = 75.155F;
constexpr float kStartY = 41.1354F;
constexpr float kStartHeading = 91.909F;
constexpr float kRise = 40.0F;
// Updates of play before the teleport (checkpoint 3 opens with Vermin's fence lesson, which holds the pad for about
// 9 s), and the updates of the fall looked at (the trace's 71 in the air).
constexpr std::uint64_t kSettle = 300;
constexpr int kFallUpdates = 70;

} // namespace

TEST_CASE("the disc's level99: in a 40 m fall the follow camera's aim point stays under 5 m above the player's head",
          "[disc][story][camera]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "");
    run.session().startAtLevel("level99", 3);
    REQUIRE(run.runUntilPlay());
    run.run(kSettle);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    coney::camera::Cameras* cameras = run.session().gameplay().cameras();
    REQUIRE(cameras != nullptr);
    const coney::camera::FollowCamera* follow = cameras->follow();
    REQUIRE(follow != nullptr);

    // Put him on the ground at the trace's spot first, so the camera settles behind him, then 40 m above it.
    const float ground = play->playerFeet().z;
    play->teleport(coney::debug::Place{.name = "street end", .feet = {kStartX, kStartY, ground}, .headingDegrees =
                                                                                                  kStartHeading});
    run.run(60);
    const coney::anim::Vec3 feet = play->playerFeet();
    play->teleport(coney::debug::Place{.name = "40 m up", .feet = {feet.x, feet.y, feet.z + kRise},
                                       .headingDegrees = kStartHeading});
    REQUIRE(play->playerFeet().z > feet.z + kRise - 1.0F);

    // The fall: the lag and the aim point's height above the look-at point each update.
    float lagMost = 0.0F;
    float aboveMost = 0.0F;
    std::array<float, kFallUpdates> lags{};
    std::array<float, kFallUpdates> aboves{};
    for (int i = 0; i < kFallUpdates && play->playerFeet().z > feet.z + 1.0F; ++i) {
        run.run(1);
        lags.at(static_cast<std::size_t>(i)) = follow->aimLag();
        aboves.at(static_cast<std::size_t>(i)) = follow->aimPoint().z - follow->lookAt().z;
        lagMost = std::max(lagMost, follow->aimLag());
        aboveMost = std::max(aboveMost, aboves.at(static_cast<std::size_t>(i)));
    }
    std::printf("  level99 40 m fall: update 12 lag %.3f above %.2f; 24 %.3f %.2f; 36 %.3f %.2f; 48 %.3f %.2f; 60 %.3f "
                "%.2f; 66 %.3f %.2f; highest lag %.3f, highest aim point %.2f m above the look-at point\n",
                static_cast<double>(lags[11]), static_cast<double>(aboves[11]), static_cast<double>(lags[23]),
                static_cast<double>(aboves[23]), static_cast<double>(lags[35]), static_cast<double>(aboves[35]),
                static_cast<double>(lags[47]), static_cast<double>(aboves[47]), static_cast<double>(lags[59]),
                static_cast<double>(aboves[59]), static_cast<double>(lags[65]), static_cast<double>(aboves[65]),
                static_cast<double>(lagMost), static_cast<double>(aboveMost));
    CHECK(lagMost > 0.18F);
    CHECK(aboveMost < 5.0F);
}
