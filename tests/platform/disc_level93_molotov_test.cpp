// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 can pick up the Molotovs lying on the liquor store's shelves in
// `level93` (mission 10) checkpoint 1: the `dyn_molotv` of object zone 3, which the checkpoint's script switches on.
// The level plays in the game session as `--play-level level93` plays it (support/disc_session.h), its opening scene
// skipped with the pad. Player 1 drops the knife he starts with, then is put in front of each Molotov in turn, facing
// it, from each side until one works, and presses triangle as a player does; with the Molotov in hand he is put back
// outside, where a second press drops it (docs/research/objects.md#pickable, docs/research/combat.md#breakables). It
// runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only
// (LEGAL.md).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "world_objects/spawn_records.h"

namespace {

// The frame of play at which the opening scene is skipped (cross on its skip prompt, after its first seconds).
constexpr std::uint64_t kSkipAt = 100;
// The frames the scene's end and the player's hand-over take before the first press.
constexpr std::uint64_t kSettleAt = 400;
// How far in front of a Molotov the player stands, metres in plan: inside the search's reach of 1.5 m.
constexpr float kStandOff = 0.8F;
// The frames one attempt takes: the press, the pick-up clip (20 updates), and the press that drops it outside.
constexpr std::uint64_t kHeldBy = 60;
constexpr std::uint64_t kDropAt = 70;
constexpr std::uint64_t kAttemptFrames = 90;
// Where player 1 drops the knife he starts with, out of the store and far from every shelf.
constexpr coney::anim::Vec3 kDropKnifeAt{311.9F, 120.3F, 0.3F};

// The four directions the player may stand in (degrees of the Molotov-to-player direction, from +x): a shelf's
// back is against a wall, so some sides are out of sight or out of reach.
constexpr float kApproachDegrees[] = {0.0F, 90.0F, 180.0F, 270.0F};
constexpr float kPi = 3.14159265F;

} // namespace

TEST_CASE("the disc's level93 checkpoint 1: the liquor store's molotovs can be picked up", "[disc][story][pickup]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    pad.play(std::format("{} tap cross\n", kSkipAt));
    coney::test::DiscSession run(*wad, pad);
    run.session().startAtLevel("level93", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSettleAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);

    // The knife the script puts in Cleon's hand goes first: with something held, triangle drops it.
    play->teleport(coney::debug::Place{.name = "drop", .feet = kDropKnifeAt, .headingDegrees = 0.0F});
    std::uint64_t start = pad.nextFrame() + 2;
    pad.play(std::format("{} tap triangle\n", start + 3));
    REQUIRE(run.runToPlayFrame(start + kAttemptFrames));
    REQUIRE(play->heldObjectType().empty());

    // The Molotovs on the store's shelves (the zone's records parked below the map are left out).
    std::vector<coney::world_objects::SpawnRecord> molotovs;
    for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
        if (record.typeName == "dyn_molotv" && !record.removed && record.position[2] > 0.0F) {
            molotovs.push_back(record);
        }
    }
    REQUIRE(molotovs.size() == 6);

    std::size_t taken = 0;
    for (const coney::world_objects::SpawnRecord& molotov : molotovs) {
        float sideTaken = -1.0F; // none yet
        for (const float degrees : kApproachDegrees) {
            const float dx = std::cos(degrees * kPi / 180.0F);
            const float dy = std::sin(degrees * kPi / 180.0F);
            const coney::anim::Vec3 feet{molotov.position[0] + (dx * kStandOff), molotov.position[1] + (dy * kStandOff),
                                         kDropKnifeAt.z};
            // Facing the Molotov: heading 0 faces +y and turns counter-clockwise, so the facing is (-sin, cos).
            const float heading = std::atan2(dx, -dy) * 180.0F / kPi;
            play->teleport(coney::debug::Place{.name = "molotov", .feet = feet, .headingDegrees = heading});
            start = pad.nextFrame() + 2;
            pad.play(std::format("{} tap triangle\n", start + 3));
            REQUIRE(run.runToPlayFrame(start + kHeldBy));
            const bool held = play->heldObjectType() == "dyn_molotv";
            if (held) {
                // Dropped away from the store, where its break sets off nothing on the shelves.
                play->teleport(coney::debug::Place{.name = "drop", .feet = kDropKnifeAt, .headingDegrees = 0.0F});
                pad.play(std::format("{} tap triangle\n", start + kDropAt));
            }
            REQUIRE(run.runToPlayFrame(start + kAttemptFrames));
            if (held) {
                sideTaken = degrees;
                break;
            }
        }
        std::printf("  level93 liquor store: molotov %.0f at (%.2f, %.2f, %.2f): taken from the side at %.0f degrees\n",
                    molotov.handle, molotov.position[0], molotov.position[1], molotov.position[2], sideTaken);
        if (sideTaken >= 0.0F) {
            ++taken;
        }
    }
    CHECK(taken == molotovs.size());
}
