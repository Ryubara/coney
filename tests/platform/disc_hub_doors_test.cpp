// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the hangout's closed exit doors stop the player from both sides
// (checklist item 30, docs/research/objects.md#door-blocking). `level95` plays at checkpoint 1 in the game session as
// `--play-level level95` plays it (support/disc_session.h). Once the opening's pad lock is over, player 1 is steered at
// 60 % toward the street through exit door 10 (`dyn_door_stlslot`, its plane at x -187.63) and must stop inside; then
// he is put just outside it (the debug teleport, only to reach the far side) and steered back in, and must stop
// outside. The door's two triangles must carry the spawn's two-sided and door bits. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts and positions only (LEGAL.md).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "world_objects/doors.h"
#include "world_objects/level_objects.h"

namespace {

// Exit door 10 of level95.lua's SpawnDoor calls: its plane and the y in the middle of its opening (a reference value
// read from the disc, docs/research/objects.md#door-blocking).
constexpr std::uint16_t kExitDoor = 10;
constexpr float kDoorX = -187.63F;
constexpr float kDoorMidY = 95.1F;
// The stopped sphere's distance from the plane is its radius (about 0.47 m) and a little; a walker that got this close
// or closer met no wall.
constexpr float kStopMargin = 0.3F;

// The frames of play: the opening's pad lock (about 23 s at checkpoint 1) is over by `kOutwardAt`; each walk gets 15 s.
constexpr std::uint64_t kOutwardAt = 900;
constexpr std::uint64_t kTeleportAt = 1350;
constexpr std::uint64_t kInwardAt = 1360;
constexpr std::uint64_t kEnd = 1810;

// The pad: steered at 60 % to a point 4 m past the door in the street, then, from outside, to one 4 m inside.
std::string padScript() {
    return std::format("{} steer 60 {:.2f},{:.2f}\n{} steer 60 {:.2f},{:.2f}\n", kOutwardAt, kDoorX - 4.0F, kDoorMidY,
                       kInwardAt, kDoorX + 4.0F, kDoorMidY);
}

} // namespace

TEST_CASE("the disc's level95: a closed exit door stops the player from inside and from outside",
          "[disc][story][hub][doors]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, padScript());
    run.session().startAtLevel("level95", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kOutwardAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);

    // The door's triangles, as its spawn left them in the bound level mesh.
    const coney::world_objects::LevelObjects& objects = run.session().gameplay().objects();
    const coney::world_objects::Door* door = nullptr;
    for (const coney::world_objects::Door& candidate : objects.doors.doors()) {
        if (candidate.number == kExitDoor) {
            door = &candidate;
        }
    }
    REQUIRE(door != nullptr);
    REQUIRE(objects.world.collision != nullptr);
    int marked = 0;
    for (const std::uint32_t index : door->triangles) {
        REQUIRE(index < objects.world.collision->triangles().size());
        const std::uint16_t flags = objects.world.collision->triangles()[index].flags;
        marked += (flags & coney::raycast::kTriangleTwoSided) != 0 && (flags & 0x400) != 0 ? 1 : 0;
    }
    CHECK(marked == 2);
    CHECK_FALSE(objects.doors.isOpen(door->handle));

    // Inside, steered out: the feet never reach the plane.
    const coney::anim::Vec3 start = play->playerFeet();
    REQUIRE(start.x > kDoorX + 1.0F);
    float outermost = start.x;
    while (run.playFrames() < kTeleportAt && run.runToPlayFrame(run.playFrames() + 1)) {
        outermost = std::min(outermost, play->playerFeet().x);
    }
    const float stoppedInside = play->playerFeet().x;

    // Outside, steered in: the feet never reach the plane from the street side either.
    const coney::anim::Vec3 outside{kDoorX - 2.0F, kDoorMidY, start.z};
    play->teleport(coney::debug::Place{.name = "outside door 10", .feet = outside, .headingDegrees = -90.0F});
    float innermost = outside.x;
    while (run.playFrames() < kEnd && run.runToPlayFrame(run.playFrames() + 1)) {
        innermost = std::max(innermost, play->playerFeet().x);
    }
    const float stoppedOutside = play->playerFeet().x;

    std::printf("  level95 exit door 10: %d of 2 triangles two-sided; inside from x %.2f stopped at %.2f (nearest "
                "%.2f), outside stopped at %.2f (nearest %.2f), plane at %.2f\n",
                marked, start.x, stoppedInside, outermost, stoppedOutside, innermost, kDoorX);
    // He walked up to it each time (more than a metre), and it held.
    CHECK(start.x - stoppedInside > 1.0F);
    CHECK(outermost > kDoorX + kStopMargin);
    CHECK(stoppedOutside - outside.x > 1.0F);
    CHECK(innermost < kDoorX - kStopMargin);
}
