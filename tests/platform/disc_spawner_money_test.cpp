// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the street's humans of level87 start with money when the mission is played
// as `--play-level level87 --checkpoint 3` plays it (support/disc_session.h): after the level has run for a while, the
// street civilians and the bums hold the starting money their classes roll, so mugging them pays
// (docs/research/crimes.md#starting-money). The gang spawners' humans are made by the scripts' `HuCreate` as the
// scripts' own are, so one roll serves both (tests/ai/spawner_money_test.cpp). It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts and dollars only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The frames of play the level runs for, so the street's humans have been made.
constexpr std::uint64_t kRunFrames = 1500;

// The character types of the street's civilians (`cCivilians`; they hold $7-$15 in a run, the range of category 4,
// $5-$20) and of its bums (`bBums`; $1-$3, category 6's $1-$4): read from a run, so inferred.
constexpr int kCivilian = 395;
constexpr std::array<int, 4> kBums{272, 274, 276, 278};

// What the humans of one character type hold.
struct TypeMoney {
    int humans = 0;
    int withMoney = 0;
    int lowest = 1000;
    int highest = 0;
};

} // namespace

TEST_CASE("the disc's level87 checkpoint 3: the street's civilians and bums hold starting money",
          "[disc][story][spawner][money]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, "100 tap cross\n");
    run.session().startAtLevel("level87", 3);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kRunFrames));
    const coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    std::map<int, TypeMoney> byType;
    for (const coney::ai::AiHuman& made : play->fighters().humans()) {
        if (made.removed) {
            continue;
        }
        const coney::ai::Brain* brain = play->fighters().brainOf(*made.human);
        if (brain == nullptr) {
            continue;
        }
        TypeMoney& type = byType[brain->characterClass()];
        const int money = made.human->script().money;
        ++type.humans;
        type.withMoney += money > 0 ? 1 : 0;
        type.lowest = std::min(type.lowest, money);
        type.highest = std::max(type.highest, money);
    }
    for (const auto& [type, found] : byType) {
        std::printf("spawner money: type %d: %d humans, %d with money, $%d to $%d\n", type, found.humans,
                    found.withMoney, found.lowest, found.highest);
    }
    // The civilians roll $5-$20 (rarely five times that), so every one holds at least $5.
    REQUIRE(byType.contains(kCivilian));
    const TypeMoney& civilians = byType.at(kCivilian);
    CHECK(civilians.humans >= 1);
    CHECK(civilians.lowest >= 5);
    // The bums roll $1-$4, so some of them hold money.
    int bumsWithMoney = 0;
    for (const int type : kBums) {
        if (byType.contains(type)) {
            bumsWithMoney += byType.at(type).withMoney;
        }
    }
    CHECK(bumsWithMoney >= 1);
}
