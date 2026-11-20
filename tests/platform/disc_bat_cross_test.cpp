// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a cross with a bat in hand starts a swing exactly when the original's does
// (docs/research/combat-throws.md#bat-cross, issue #228), in a level as `coney --play-level level99` plays it
// (support/disc_session.h): the player is given a bat, nobody is near, and the pad (a script, cross as a tap) presses
// cross at a chosen update after a square (clip 34) or a cross (clip 36). The original drops a cross whose `0x10`
// falls inside the move before it and plays 36 on the first update after the move: update k37 after 34 and k35
// after 36, k counting from the move's first update. It also checks that cross + square with a bat and nobody near
// throws it (502), as the original's does, and is not the special 653. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_pickups.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/player.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "world_objects/spawn_records.h"

namespace {

namespace id = coney::combat::anim_id;

// The play frame the first trial's move starts on: the intro scene is skipped with cross at 100, and the player is
// given the bat once frame kArmAt has played.
constexpr std::uint64_t kArmAt = 250;
constexpr std::uint64_t kFirstTrial = 300;
// The play frames each trial has to itself: a move, the other swing and the wait for everything to end.
constexpr std::uint64_t kTrialLength = 120;
// The bat's anim set (docs/research/combat-throws.md#bat) and its object's type name.
constexpr int kBatSet = 3;
constexpr double kBatObject = 9999.0;

// One trial: the move that opens it and the update k (counted from that move's first) on which cross's `0x10` falls.
struct Trial {
    std::uint32_t first; // 34 (a square) or 36 (a cross)
    int crossOn;         // k of cross's 0x10
};

// The pad script of the trials: trial i's first move at kFirstTrial + i * kTrialLength; a square tap starts 34 on its
// frame and a cross tap starts 36 on the next (its release is the `0x10`).
std::string scriptFor(const std::vector<Trial>& trials) {
    std::string text = "100 tap cross\n";
    for (std::size_t i = 0; i < trials.size(); ++i) {
        const std::uint64_t start = kFirstTrial + (i * kTrialLength);
        const Trial& trial = trials[i];
        if (trial.first == 34) {
            text += std::to_string(start) + " tap square\n";
        } else {
            text += std::to_string(start - 1) + " tap cross\n";
        }
        // A tap's `0x10` is on the update after its press, so the tap is one update before the wanted k.
        text += std::to_string(start + static_cast<std::uint64_t>(trial.crossOn) - 1) + " tap cross\n";
    }
    return text;
}

// Plays the trials and returns, for each, the updates (counted from the move's first) on which a swing started after
// the move that opens it.
std::vector<std::vector<int>> playTrials(const coney::io::Wad& wad, const std::vector<Trial>& trials) {
    coney::test::DiscSession run(wad, scriptFor(trials));
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kArmAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    // The test's own player, handed a bat: the held object and the anim set it gives, as a pick-up leaves them.
    auto& player = const_cast<coney::human::Player&>(play->player());
    player.human().script().heldObject = kBatObject;
    player.human().script().heldObjectName = "dyn_bat_tuff";
    player.human().fighter().setAnimSet(kBatSet);

    // Every update's start: the clip the player's combat began on it.
    const std::uint64_t end = kFirstTrial + (trials.size() * kTrialLength);
    std::map<std::uint64_t, int> starts;
    const std::uint64_t first = run.playFrames();
    for (std::uint64_t frame = first; frame < end; ++frame) {
        run.run(1);
        const int started = player.human().fighter().last().startAnim;
        if (started != id::kNone) {
            starts.emplace(frame, started);
        }
    }
    REQUIRE(player.human().fighter().animSet() == kBatSet);
    std::vector<std::vector<int>> result;
    for (std::size_t i = 0; i < trials.size(); ++i) {
        const std::uint64_t lo = kFirstTrial + (i * kTrialLength);
        const std::uint64_t hi = lo + kTrialLength;
        // The move that opens the trial is the first start in its window; k counts from the update it was seen on.
        std::optional<std::uint64_t> opened;
        std::vector<int> later;
        for (auto it = starts.lower_bound(lo); it != starts.end() && it->first < hi; ++it) {
            if (!opened) {
                opened = it->first;
                REQUIRE(static_cast<std::uint32_t>(it->second) == trials[i].first);
            } else {
                later.push_back(static_cast<int>(it->first - *opened));
                REQUIRE(it->second == 36);
            }
        }
        REQUIRE(opened.has_value());
        result.push_back(later);
    }
    return result;
}

} // namespace

TEST_CASE("the disc's level99: with a bat, cross swings again only from k37 after a square and k35 after a cross",
          "[disc][story][combat]") {
    // Expected: combat-throws.md#bat-cross, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Cross's 0x10 around each move's end (the original's runtime: dropped through k36 after 34 and k34 after 36).
    std::vector<Trial> trials;
    for (int k = 33; k <= 40; ++k) {
        trials.push_back(Trial{34, k});
    }
    for (int k = 31; k <= 38; ++k) {
        trials.push_back(Trial{36, k});
    }
    const std::vector<std::vector<int>> swings = playTrials(*wad, trials);
    REQUIRE(swings.size() == trials.size());

    int firstAfterSquare = -1;
    int firstAfterCross = -1;
    for (std::size_t i = 0; i < trials.size(); ++i) {
        const Trial& trial = trials[i];
        INFO("after " << trial.first << ", cross's 0x10 on k" << trial.crossOn);
        int& first = trial.first == 34 ? firstAfterSquare : firstAfterCross;
        if (!swings[i].empty() && first < 0) {
            first = trial.crossOn;
        }
        // One swing, on the very update asked for, or none.
        CHECK(swings[i].size() <= 1);
        if (!swings[i].empty()) {
            CHECK(swings[i].front() == trial.crossOn);
        }
    }
    std::printf("bat cross: after 34 the first swing is at k%d, after 36 at k%d (%zu trials)\n", firstAfterSquare,
                firstAfterCross, trials.size());
    CHECK(firstAfterSquare == 37);
    CHECK(firstAfterCross == 35);
    // Once the move has ended every later cross swings.
    for (std::size_t i = 0; i < trials.size(); ++i) {
        const int free = trials[i].first == 34 ? 37 : 35;
        CHECK(swings[i].empty() == (trials[i].crossOn < free));
    }
}

namespace {

// What one cross + square with a bat in hand did, from the `0x22` update on.
struct ThrowRun {
    bool scriptForbids = false;    // the level's script had set the no-throw flag (HuSetNoThrowWeapon) on the player
    std::optional<int> releasedAt; // the update after 502's start on which the bat left the hand
    bool thrown = false;           // 502 started as the armed special's throw
    int clipUpdates = 0;           // updates with 502 playing
    int specials = 0;              // starts of the special 653
    int swings = 0;                // starts of the bat's cross swing 36
};

// Plays level99 with a bat put in the player's hand and nobody near, then cross held 3 updates with square tapped on
// its 2nd (the input that gives `0x22`). With `allowThrow` the no-throw flag the combat lesson sets is cleared first,
// as the lesson's own Cleanup clears it.
ThrowRun throwBat(const coney::io::Wad& wad, bool allowThrow) {
    const std::string script = "100 tap cross\n300 press cross\n301 tap square\n303 release cross\n";
    coney::test::DiscSession run(wad, script);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kArmAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    auto& player = const_cast<coney::human::Player&>(play->player());
    // A bat put in the hand as a pick-up leaves it (**the test's stand-in** for the pick-up, which other tests cover):
    // a record of the type, taken into the hand, then held; the next step gives the fighter its anim set.
    coney::world_objects::SpawnRecord made;
    made.handle = run.session().flow().scripts().nextObjectHandle();
    REQUIRE(made.handle != 0.0);
    made.typeName = "dyn_bat_tuff";
    const coney::anim::Vec3 feet = player.human().position();
    made.position = {feet.x, feet.y, feet.z};
    made.live = true;
    REQUIRE(run.session().flow().spawnRecords().add(made) != nullptr);
    REQUIRE(run.session().gameplay().pickups() != nullptr);
    auto& pickups = const_cast<coney::LevelPickups&>(*run.session().gameplay().pickups());
    REQUIRE(pickups.take(made.handle, 0) == coney::TakeResult::InHand);
    player.human().script().heldObject = made.handle;
    player.human().script().heldObjectName = "dyn_bat_tuff";
    run.run(2);
    REQUIRE(player.human().fighter().animSet() == kBatSet);

    ThrowRun result;
    result.scriptForbids = player.human().fighter().hasFlag(coney::human::flag::kNoThrowWeapon);
    if (allowThrow) {
        player.human().setFlag(coney::human::flag::kNoThrowWeapon, false);
    }
    std::optional<std::uint64_t> thrownOn;
    for (std::uint64_t frame = run.playFrames(); frame < 400; ++frame) {
        run.run(1);
        const coney::human::Fighter& fighter = player.human().fighter();
        const int started = fighter.last().startAnim;
        result.specials += started == id::kSpecial ? 1 : 0;
        result.swings += started == 36 ? 1 : 0;
        if (started == id::kSwingableThrow && fighter.last().meleeThrow && !thrownOn) {
            thrownOn = frame;
        }
        if (thrownOn && !result.releasedAt && player.human().script().heldObject == coney::world_objects::kNoObject) {
            result.releasedAt = static_cast<int>(frame - *thrownOn);
        }
        result.clipUpdates +=
            player.human().animator().animId() == static_cast<std::uint32_t>(id::kSwingableThrow) ? 1 : 0;
    }
    result.thrown = thrownOn.has_value();
    return result;
}

} // namespace

TEST_CASE("the disc's level99: with a bat and nobody near, cross + square throws it: 502, out of the hand at k20, "
          "35 updates, and no special",
          "[disc][story][combat]") {
    // Expected: combat-throws.md#bat-cross, confirmed (runtime)
    // The original's run bat_cross_square: cross held 3 updates, square tapped on its 2nd, nobody near: 0x22 played
    // 502, the bat left the hand at k20 and the clip lasted 35 updates; the release's 0x10 was dropped.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const ThrowRun run = throwBat(*wad, true);
    std::printf(
        "bat cross + square: 502 thrown %d, out of the hand at k%d, %d updates of 502, %d specials, %d swings\n",
        static_cast<int>(run.thrown), run.releasedAt.value_or(-1), run.clipUpdates, run.specials, run.swings);
    REQUIRE(run.thrown);
    REQUIRE(run.releasedAt.has_value());
    CHECK(run.releasedAt == 20);
    CHECK(run.clipUpdates == 35);
    CHECK(run.specials == 0);
    CHECK(run.swings == 0);
}

TEST_CASE("the disc's level99: the combat lesson's no-throw flag keeps the bat in hand: cross + square throws nothing "
          "and the cross's release swings 36",
          "[disc][story][combat]") {
    // Expected: combat-throws.md#armed-special, inferred
    // level99_combat.lua's SetupCombat sets HuSetNoThrowWeapon on the player (its Cleanup clears it); the armed
    // special then refuses the throw, Player_Special refuses a player holding an object, and the release swings.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const ThrowRun run = throwBat(*wad, false);
    std::printf("bat cross + square in the lesson: flag set %d, 502 thrown %d, %d specials, %d swings\n",
                static_cast<int>(run.scriptForbids), static_cast<int>(run.thrown), run.specials, run.swings);
    CHECK(run.scriptForbids);
    CHECK_FALSE(run.thrown);
    CHECK_FALSE(run.releasedAt.has_value());
    CHECK(run.specials == 0);
    CHECK(run.swings == 1);
}
