// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus' noclip flies player 1 in a level as `coney --play-level
// level99` plays it (support/disc_session.h): switched on through the debug session as the Player page does, the left
// stick flies him across the street relative to the camera and R2 lifts him, he hovers with the pad let go, and
// switched off he drops back onto the ground with the normal physics (docs/guides/debug-menu.md#noclip). It runs only
// when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "debug/debug_session.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The frames of play: the noclip on, a second of flying (stick fully up, R2 held), a second of hovering, then off.
constexpr std::uint64_t kOnAt = 300;
constexpr std::uint64_t kHoverAt = 330;
constexpr std::uint64_t kOffAt = 360;
constexpr std::uint64_t kEnd = 480;

// The pad: the intro scene skipped, then a second of flight up and away from the camera.
constexpr std::string_view kPad = "100 tap cross\n"
                                  "300 stick left 0 100\n300 press r2\n"
                                  "330 stick left 0 0\n330 release r2\n";

} // namespace

TEST_CASE("the disc's level99: the noclip flies player 1 up and across, hovers, and drops him when off",
          "[disc][story][debug][noclip]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    // The debug menus as `coney` wires them, with the play mode as `coney --play-level` gives it to them.
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run.session(); });
    services.play = [&run]() -> coney::debug::PlayControls* { return run.session().play(); };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, nullptr);

    REQUIRE(run.runToPlayFrame(kOnAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    REQUIRE(play->canNoclip());
    const coney::human::Human& human = play->player().human();
    REQUIRE_FALSE(human.airborne());
    const coney::anim::Vec3 ground = human.position();
    debug.setNoclip(true);
    CHECK(play->noclip());
    REQUIRE_FALSE(debug.cornerLines().empty());
    CHECK(debug.cornerLines().back() == "Noclip on");

    // A second at 6 m/s: about 6 m across and 6 m up, through whatever is in the way.
    REQUIRE(run.runToPlayFrame(kHoverAt));
    const coney::anim::Vec3 flown = human.position();
    const float across = std::hypot(flown.x - ground.x, flown.y - ground.y);
    const float up = flown.z - ground.z;
    std::printf("noclip: %.2f m across, %.2f m up in %llu frames\n", static_cast<double>(across),
                static_cast<double>(up), static_cast<unsigned long long>(kHoverAt - kOnAt));
    CHECK(human.flying());
    CHECK(across > 5.0F);
    CHECK(across < 7.0F);
    CHECK(up > 5.0F);
    CHECK(up < 7.0F);

    // Let go, he hovers: no gravity.
    REQUIRE(run.runToPlayFrame(kOffAt));
    CHECK(std::abs(human.position().z - flown.z) < 0.01F);
    CHECK_FALSE(human.airborne());

    // Off, he falls and lands on the street.
    debug.setNoclip(false);
    CHECK_FALSE(play->noclip());
    REQUIRE(run.runToPlayFrame(kEnd));
    CHECK_FALSE(human.flying());
    CHECK_FALSE(human.airborne());
    CHECK(human.position().z < flown.z - 4.0F);
    CHECK(play->player().respawns() == 0);
}

namespace {

// The hotkey's pad: the intro scene skipped, then R3 + d-pad left tapped together twice, a second apart.
constexpr std::uint64_t kChordOnAt = 300;
constexpr std::uint64_t kChordOffAt = 360;
constexpr std::string_view kChordPad = "100 tap cross\n"
                                       "300 tap r3 left\n"
                                       "360 tap r3 left\n";

} // namespace

TEST_CASE("the disc's level99: R3 + d-pad left on pad 1 switches the noclip on and off with no menu opened",
          "[disc][story][debug][noclip]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kChordPad);
    // The debug menus' input gate between the pad and the game, as `coney` puts it: the hotkey is on by default.
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run.session(); });
    services.play = [&run]() -> coney::debug::PlayControls* { return run.session().play(); };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, &run.input());
    run.stack().setInput(&debug.gate());
    REQUIRE(debug.noclipHotkey());
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());

    REQUIRE(run.runToPlayFrame(kChordOnAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    CHECK_FALSE(play->noclip());
    // The chord's frame has been read: the noclip is on, with its notice, and the menu stayed shut.
    REQUIRE(run.runToPlayFrame(kChordOnAt + 2));
    CHECK(play->noclip());
    REQUIRE_FALSE(debug.cornerLines().empty());
    CHECK(debug.cornerLines().back() == "Noclip on");
    CHECK_FALSE(debug.navigator().isOpen());

    REQUIRE(run.runToPlayFrame(kChordOffAt + 2));
    CHECK_FALSE(play->noclip());
    REQUIRE_FALSE(debug.cornerLines().empty());
    CHECK(debug.cornerLines().back() == "Noclip off");
    CHECK_FALSE(debug.navigator().isOpen());
}
