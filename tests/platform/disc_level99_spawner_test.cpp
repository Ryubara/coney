// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menus' Spawner works in a level as `coney --play-level level99`
// plays it (support/disc_session.h): a wall spawned through the Spawner page in front of player 1 while he jogs
// (the left stick at 70 per cent) stops him short of it, as a level's own wall would; cleared, he jogs on through
// where it stood; and a jump to the level again starts with nothing spawned and the level's collision as the disc
// has it (docs/guides/debug-menu.md, docs/guides/sandbox.md). It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise; it prints counts and distances only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The frames of play: the jog starts (300), the wall is spawned once he is under way, he is checked against it, it is
// cleared, and he is checked past it.
constexpr std::uint64_t kSpawnAt = 330;
constexpr std::uint64_t kStoppedAt = 420;
constexpr std::uint64_t kStillAt = 450;
constexpr std::uint64_t kPastAt = 540;

// How far ahead of his feet the wall is spawned, metres, and the Spawner's wall's depth (debug::spawnables()).
constexpr float kAhead = 5.0F;
constexpr float kWallDepth = 0.3F;

// The pad: the intro scene skipped, then the left stick held at 70 per cent away from the camera, a jog.
constexpr std::string_view kPad = "100 tap cross\n"
                                  "300 stick left 0 70\n";

// How far `feet` lies along the heading `headingDegrees` from `from` (0 faces +y, counter-clockwise from above).
float along(coney::anim::Vec3 from, float headingDegrees, coney::anim::Vec3 feet) {
    const float h = headingDegrees * std::numbers::pi_v<float> / 180.0F;
    return (feet.x - from.x) * -std::sin(h) + (feet.y - from.y) * std::cos(h);
}

} // namespace

TEST_CASE("the disc's level99: a wall from the Spawner stops player 1, clears, and is gone after a reload",
          "[disc][story][debug][spawner]") {
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
    coney::platform::PlayLevelControls controls;
    services.play = [&run, &controls] { return controls.controlsOf(run.session().play()); };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, nullptr);

    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const std::size_t diskTriangles = play->collision().triangles().size();

    // Under way, the wall is spawned through the page as a person spawns it: the distance set, then the action.
    REQUIRE(run.runToPlayFrame(kSpawnAt));
    REQUIRE(play->canSpawn());
    const auto page = debug.model().openPage("Spawner");
    REQUIRE(page != nullptr);
    const coney::debug::MenuItem* distance = page->find("Distance ahead");
    const coney::debug::MenuItem* wall = page->find("Wall (2.5 m)");
    const coney::debug::MenuItem* clear = page->find("Clear spawned");
    REQUIRE(distance != nullptr);
    REQUIRE(wall != nullptr);
    REQUIRE(clear != nullptr);
    distance->setNumber(kAhead);
    const coney::human::Human& human = play->player().human();
    const coney::anim::Vec3 from = human.position();
    const float heading = play->playerHeadingDegrees();
    REQUIRE(human.speed() > 1.0F);
    wall->run();
    REQUIRE(play->spawnedCount() == 1);
    const std::size_t withWall = play->collision().triangles().size();
    CHECK(withWall > diskTriangles);

    // He stops short of its near face: the walking sphere's radius and the sweep's margin before it.
    REQUIRE(run.runToPlayFrame(kStoppedAt));
    const float stopped = along(from, heading, human.position());
    REQUIRE(run.runToPlayFrame(kStillAt));
    const float still = along(from, heading, human.position());
    std::printf("spawner: wall face %.2f m ahead; player at %.2f m after %llu frames, %.2f m after %llu\n",
                static_cast<double>(kAhead - kWallDepth / 2.0F), static_cast<double>(stopped),
                static_cast<unsigned long long>(kStoppedAt - kSpawnAt), static_cast<double>(still),
                static_cast<unsigned long long>(kStillAt - kSpawnAt));
    CHECK(stopped > kAhead - kWallDepth / 2.0F - 1.0F);
    CHECK(still < kAhead - kWallDepth / 2.0F);
    CHECK(std::abs(still - stopped) < 0.1F);

    // Cleared, the level's collision is the disc's again and he jogs on through where the wall stood.
    clear->run();
    CHECK(play->spawnedCount() == 0);
    CHECK(play->collision().triangles().size() == diskTriangles);
    REQUIRE(run.runToPlayFrame(kPastAt));
    const float past = along(from, heading, human.position());
    std::printf("spawner: cleared, player at %.2f m after %llu more frames\n", static_cast<double>(past),
                static_cast<unsigned long long>(kPastAt - kStillAt));
    CHECK(past > kAhead + 1.0F);

    // Spawned again, then the level is loaded again: the new level starts with nothing spawned.
    wall->run();
    REQUIRE(play->spawnedCount() == 1);
    // A new level's scenery logs its preload, so the reload is done once one more preload line is in and it plays.
    const auto preloads = [&run] {
        return std::ranges::count_if(run.log(), [](const std::string& line) { return line.starts_with("preload:"); });
    };
    const auto before = preloads();
    REQUIRE(run.session().jumpToLevel("level99", 1));
    REQUIRE(run.runUntil([&run, &preloads, before] { return preloads() > before && run.session().inPlay(); },
                         coney::test::DiscSession::kPlayLimit));
    const coney::platform::PlayLevelMode* reloaded = run.session().play();
    REQUIRE(reloaded != nullptr);
    CHECK(reloaded->spawnedCount() == 0);
    CHECK(reloaded->collision().triangles().size() == diskTriangles);
}

namespace {

// The climbs: a fence spawned ahead once he walks (from frame 300), triangle at its face (400), then a crate the same
// way (triangle at 570).
constexpr std::uint64_t kFenceAt = 330;
constexpr std::uint64_t kCrateAt = 500;
constexpr std::uint64_t kClimbsEnd = 690;

// How far ahead the fence and the crate are spawned, metres, the fence's depth and the crate's height
// (debug::spawnables()).
constexpr float kClimbAhead = 3.0F;
constexpr float kFenceDepth = 0.1F;
constexpr float kCrateHeight = 1.0F;

// The pad: the intro scene skipped, then the left stick held at 50 per cent away from the camera, a walk, with
// triangle tapped at each obstacle's face (the standing climbs of tests/support/parkour_climb_stand.txt).
constexpr std::string_view kClimbPad = "100 tap cross\n"
                                       "300 stick left 0 50\n"
                                       "400 tap triangle\n"
                                       "570 tap triangle\n";

// The Spawner page's item `label`, which must exist.
const coney::debug::MenuItem& itemOn(const coney::debug::MenuPage& page, std::string_view label) {
    const coney::debug::MenuItem* item = page.find(label);
    INFO(label);
    REQUIRE(item != nullptr);
    return *item;
}

} // namespace

TEST_CASE("the disc's level99: player 1 climbs a spawned fence and onto a spawned crate",
          "[disc][story][debug][spawner]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kClimbPad);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run.session(); });
    coney::platform::PlayLevelControls controls;
    services.play = [&run, &controls] { return controls.controlsOf(run.session().play()); };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, nullptr);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::human::Human& human = play->player().human();
    const auto page = debug.model().openPage("Spawner");
    REQUIRE(page != nullptr);
    itemOn(*page, "Distance ahead").setNumber(kClimbAhead);

    // The fence: a standing short-fence climb through it, the feet back on the street behind it.
    REQUIRE(run.runToPlayFrame(kFenceAt));
    REQUIRE(human.speed() > 0.5F);
    coney::anim::Vec3 from = human.position();
    const float heading = play->playerHeadingDegrees();
    itemOn(*page, "Fence (1.2 m)").run();
    REQUIRE(play->spawnedCount() == 1);
    int climbing = 0;
    REQUIRE(run.runUntil(
        [&] {
            climbing += human.traversal() == coney::human::Traversal::Climbing ? 1 : 0;
            return run.playFrames() >= kCrateAt;
        },
        coney::test::DiscSession::kPlayLimit));
    const float pastFence = along(from, heading, human.position());
    const float fenceDrop = human.position().z - from.z;
    std::printf("spawner: fence back %.2f m ahead; %d frames climbing, player %.2f m ahead, %.2f m up\n",
                static_cast<double>(kClimbAhead + kFenceDepth / 2.0F), climbing, static_cast<double>(pastFence),
                static_cast<double>(fenceDrop));
    CHECK(climbing > 10);
    CHECK(pastFence > kClimbAhead + kFenceDepth / 2.0F + 0.3F);
    CHECK(std::abs(fenceDrop) < 0.3F);

    // The crate: a standing short-wall climb onto its top, where he stands before walking on.
    itemOn(*page, "Clear spawned").run();
    from = human.position();
    itemOn(*page, "Crate (1 m)").run();
    REQUIRE(play->spawnedCount() == 1);
    int onTop = 0;
    climbing = 0;
    float highest = from.z;
    REQUIRE(run.runUntil(
        [&] {
            climbing += human.traversal() == coney::human::Traversal::Climbing ? 1 : 0;
            const float z = human.position().z;
            highest = std::max(highest, z);
            onTop += !human.airborne() && std::abs(z - (from.z + kCrateHeight)) < 0.15F ? 1 : 0;
            return run.playFrames() >= kClimbsEnd;
        },
        coney::test::DiscSession::kPlayLimit));
    std::printf("spawner: crate %.2f m tall; %d frames climbing, highest %.2f m up, %d frames standing on top\n",
                static_cast<double>(kCrateHeight), climbing, static_cast<double>(highest - from.z), onTop);
    CHECK(climbing > 10);
    CHECK(onTop > 3);
    CHECK(play->player().respawns() == 0);
}
