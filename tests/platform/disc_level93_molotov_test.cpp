// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 can pick up the molotovs lying on the liquor store's shelves in
// `level93` (mission 10) checkpoint 1: the six `dyn_molotv` of object zone 3 that the checkpoint's script switches on.
// The level plays in the game session as `--play-level level93` plays it (support/disc_session.h), its opening scene
// skipped with the pad; player 1 is put in front of each molotov in turn, facing it, and presses triangle as a player
// does (docs/research/objects.md#pickable, docs/research/combat.md#breakables). It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

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
#include "warriors/created_humans.h"
#include "world_objects/spawn_records.h"

namespace {

// The frame of play at which the opening scene is skipped (cross on its skip prompt, after its first seconds).
constexpr std::uint64_t kSkipAt = 100;
// The frames the scene's end and the player's hand-over take before the first attempt.
constexpr std::uint64_t kSettleAt = 400;
// How far in front of a molotov the player stands, metres in plan: inside the search's reach of 1.5 m.
constexpr float kStandOff = 0.8F;
// The frames one attempt takes: the press, the pick-up clip, and the press that lets go again.
constexpr std::uint64_t kHeldBy = 60;
constexpr std::uint64_t kAttemptFrames = 90;

// The eight directions the player may stand in (degrees of the molotov-to-player direction, from +x).
constexpr float kApproachDegrees[] = {0.0F, 90.0F, 180.0F, 270.0F};

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

    // The molotovs the level placed, by handle.
    std::vector<coney::world_objects::SpawnRecord> molotovs;
    for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
        const bool wanted = record.typeName == "dyn_molotv" || record.typeName == "dyn_beerbottle" ||
                            record.typeName == "dyn_bottle_e";
        if (wanted && !record.removed && record.position[2] > 0.0F) {
            molotovs.push_back(record);
        }
    }
    REQUIRE(!molotovs.empty());

    std::size_t taken = 0;
    for (const coney::world_objects::SpawnRecord& molotov : molotovs) {
        std::size_t fromSides = 0;
        for (const float degrees : kApproachDegrees) {
            const float radians = degrees * 3.14159265F / 180.0F;
            const float dx = std::cos(radians);
            const float dy = std::sin(radians);
            const coney::anim::Vec3 feet{molotov.position[0] + (dx * kStandOff), molotov.position[1] + (dy * kStandOff),
                                         0.3F};
            // Facing the molotov: heading 0 faces +y, counter-clockwise, so (-sin, cos) is the facing.
            const float heading = std::atan2(dx, -dy) * 180.0F / 3.14159265F + 0.0F;
            play->teleport(coney::debug::Place{.name = "molotov", .feet = feet, .headingDegrees = heading});
            const std::size_t seen = run.log().size();
            const std::uint64_t start = pad.nextFrame() + 2;
            pad.play(std::format("{} tap triangle\n{} tap triangle\n", start + 3, start + 70));
            REQUIRE(run.runToPlayFrame(start + kHeldBy));
            if (play->heldObjectType() == molotov.typeName) {
                ++fromSides;
            }
            for (std::size_t i = seen; i < run.log().size(); ++i) {
                if (run.log()[i].starts_with("pickup:") || run.log()[i].starts_with("objects:")) {
                    std::printf("    [%.0f %.0f] %s", molotov.handle, degrees, run.log()[i].c_str());
                }
            }
            REQUIRE(run.runToPlayFrame(start + kAttemptFrames));
        }
        std::printf("  level93 liquor store: %s %.0f at (%.2f, %.2f, %.2f): taken from %zu of 4 sides\n",
                    molotov.typeName.c_str(), molotov.handle, molotov.position[0], molotov.position[1], molotov.position[2], fromSides);
        if (fromSides > 0) {
            ++taken;
        }
    }
    CHECK(taken == molotovs.size());
}
