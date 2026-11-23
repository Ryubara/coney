// SPDX-License-Identifier: GPL-3.0-or-later
// Checks against the player's own disc that `level34` checkpoint 2's molotov thrower throws on the throw goal's beat,
// as the original's does (ai::ThrowObjectGoal, docs/research/ai-goals.md#goal-throw-object). The level plays in the
// game session as `--play-level level34 --checkpoint 2` plays it (support/disc_session.h); player 1 is put in the box
// `vNearMolotovCar`, whose message 3 (`P1.ThrowMolotov`) gives the thrower his GoalThrowObject. The test notes the
// thrower's brain update when the goal first stands within range and facing its flag, and when the object leaves his
// hand: the throw must come on an update that is a multiple of 30, at most 29 updates after the first chance.
//
// It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints update numbers
// only (LEGAL.md).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/goal.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/place.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"

namespace {

// When player 1 is put in the box, and how long the throw may take after it, in frames.
constexpr std::uint64_t kEnterAt = 300;
constexpr std::uint64_t kThrowFrames = 30ULL * 40;
// A point inside `vNearMolotovCar` (level34.lua's AddVolumeBox, read either as a centre or as a corner).
constexpr float kBoxX = -85.5F;
constexpr float kBoxY = -188.4F;
// The thrower's flag `fThrowMolotov`, and the goal's range and facing limit (GoalThrowObject's 16 m; 15 degrees).
constexpr float kFlagX = -80.5F;
constexpr float kFlagY = -210.5F;
constexpr float kFlagZ = 0.0F;
constexpr float kRange = 16.0F;
constexpr float kFacing = 0.2618F;
constexpr std::uint64_t kBeat = 30;

// The brain whose goal stack holds a ThrowObject goal, or null.
coney::ai::Brain* thrower(coney::test::DiscSession& run) {
    const coney::ai::Brains* brains = run.session().gameplay().brains();
    if (brains == nullptr) {
        return nullptr;
    }
    auto& all = const_cast<coney::ai::Brains&>(*brains);
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all.at(i).findGoal(coney::ai::GoalType::ThrowObject) != nullptr) {
            return &all.at(i);
        }
    }
    return nullptr;
}

// Whether `brain`'s human stands within the range of the flag (in 3D) and faces it within the goal's limit, with no
// action queued: the goal's chance to throw.
bool atTheBeatTest(const coney::ai::Brain& brain) {
    const coney::human::Human& human = brain.human();
    const coney::anim::Vec3 feet = human.position();
    const float dx = kFlagX - feet.x;
    const float dy = kFlagY - feet.y;
    const float dz = kFlagZ - feet.z;
    if (brain.actionCount() > 0 || std::hypot(dx, dy, dz) > kRange) {
        return false;
    }
    const float heading = std::atan2(-dx, dy);
    return std::fabs(coney::human::wrapAngle(heading - human.heading())) <= kFacing;
}

} // namespace

TEST_CASE("the disc's level34 checkpoint 2: the molotov thrower throws on a brain update that is a multiple of 30",
          "[disc][story]") {
    // Expected: ai-goals.md#goal-throw-object, confirmed (runtime): the original's thrower first stood at the beat's
    // test on brain update 233 and queued the throw on 240
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    coney::test::DiscSession run(*wad, pad);
    run.session().startAtLevel("level34", 2, [] {});
    REQUIRE(run.runUntilPlay());
    while (run.playFrames() < kEnterAt) {
        run.run(1);
    }
    coney::platform::PlayLevelMode* mode = run.session().play();
    REQUIRE(mode != nullptr);
    mode->teleport(coney::human::Place{.name = "molotov", .feet = {kBoxX, kBoxY, mode->playerFeet().z}});
    const std::uint64_t entered = run.playFrames();

    // Each frame: the first update the goal could throw, and the update the object leaves his hand.
    std::optional<std::uint64_t> firstChance;
    std::optional<std::uint64_t> thrown;
    while (run.playFrames() < entered + kThrowFrames && !thrown) {
        run.run(1);
        coney::ai::Brain* brain = thrower(run);
        if (brain == nullptr) {
            continue;
        }
        if (brain->human().script().heldObject == 0.0) {
            // Thrown on this update; a throw at the first chance is its own first chance.
            thrown = brain->updates();
            if (!firstChance) {
                firstChance = thrown;
            }
        } else if (!firstChance && brain->topGoal() != nullptr &&
                   brain->topGoal()->type() == coney::ai::GoalType::ThrowObject && atTheBeatTest(*brain)) {
            firstChance = brain->updates();
        }
    }
    std::printf("  level34 checkpoint 2, molotov thrower: first chance on brain update %lld, thrown on %lld\n",
                firstChance ? static_cast<long long>(*firstChance) : -1LL,
                thrown ? static_cast<long long>(*thrown) : -1LL);
    REQUIRE(firstChance.has_value());
    REQUIRE(thrown.has_value());
    const std::uint64_t chanceAt = firstChance.value_or(0);
    const std::uint64_t thrownAt = thrown.value_or(0);
    CHECK(thrownAt % kBeat == 0);
    CHECK(thrownAt >= chanceAt);
    CHECK(thrownAt - chanceAt < kBeat);
}
