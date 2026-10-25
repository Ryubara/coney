// SPDX-License-Identifier: GPL-3.0-or-later
// Scripted noclip input uses the normal game session and the player's own disc, never copied game assets.
#include <cmath>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "fileio/wad.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

TEST_CASE("noclip holds its camera offset while R1 accelerates vertical flight", "[disc][story][debug][noclip]") {
    // Expected: unverified, #243 (Coney-only debug flight: R1 speeds flight, camera offset stays locked)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "100 tap cross\n301 press r1 r2\n331 release r1 r2\n");
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(300));
    auto* play = run.session().play();
    REQUIRE(play != nullptr);
    play->setNoclip(true);
    run.run(1);
    const auto initial = play->player().current();
    const auto offset = coney::anim::subtract(initial.cameraEye, initial.feet);
    for (int frame = 0; frame < 30; ++frame) {
        run.run(1);
        const auto current = play->player().current();
        const auto now = coney::anim::subtract(current.cameraEye, current.feet);
        CHECK(std::abs(now.x - offset.x) < 0.001F);
        CHECK(std::abs(now.y - offset.y) < 0.001F);
        CHECK(std::abs(now.z - offset.z) < 0.001F);
        CHECK(std::abs(current.heading - initial.heading) < 0.001F);
    }
    CHECK(play->player().human().position().z > initial.feet.z + 23.0F);
}
