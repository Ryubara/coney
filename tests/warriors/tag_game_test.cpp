// SPDX-License-Identifier: GPL-3.0-or-later
// The tagging stick game (docs/research/crimes.md#tagging): the difficulty table, the pattern's Catmull-Rom path, the
// cursor tracing it to a finish, a slip snapping it back, the charges of paint and the 30 % rule.
#include "warriors/tag_game.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/tag_driver.h"

using coney::TagCell;
using coney::TagGame;

namespace {

// A straight pattern along x from (20, 100) to (220, 100).
constexpr std::array<float, 4> kLine{20.0F, 100.0F, 220.0F, 100.0F};
// The same line as nine points 25 cells apart: short segments, as the scripts' patterns have, so the curve's hook past
// the last point stays small (a long last segment hooks out and back over the points before the finish).
constexpr std::array<float, 18> kNinePoints{20.0F,  100.0F, 45.0F,  100.0F, 70.0F,  100.0F, 95.0F,  100.0F, 120.0F,
                                            100.0F, 145.0F, 100.0F, 170.0F, 100.0F, 195.0F, 100.0F, 220.0F, 100.0F};

// A spender with `charges` charges.
TagGame::SpendCharge charges(int& left) {
    return [&left] {
        if (left <= 0) {
            return false;
        }
        --left;
        return true;
    };
}

} // namespace

TEST_CASE("the tagging tuning follows the Warrior class's difficulty, 0 counting as 1", "[tag_game]") {
    CHECK(coney::tagTuning(1).chargeMs == 13000);
    CHECK(coney::tagTuning(2).pauseMs == 600);
    CHECK(coney::tagTuning(3).cellsPerMs == 0.073F);
    CHECK(coney::tagTuning(0).chargeMs == 13000);
    CHECK(coney::defaultTagTuning().chargeMs == 15000);
}

TEST_CASE("a pattern's path samples the Catmull-Rom curve in grid cells, spaced over 6 squared", "[tag_game]") {
    const std::vector<TagCell> path = coney::tagPath(kLine, 2);
    REQUIRE_FALSE(path.empty());
    CHECK(path.size() <= TagGame::kMaxPoints);
    CHECK(path.front() == TagCell{20, 100});
    // Along a line every point is on it (to the truncation), x rising, and each is more than sqrt(6) from the one
    // before.
    for (std::size_t i = 1; i < path.size(); ++i) {
        CHECK(path[i].y >= 99); // truncated: the weights' rounding can land just under 100
        CHECK(path[i].y <= 100);
        const int dx = path[i].x - path[i - 1].x;
        CHECK(dx * dx > 6);
    }
    // The last segment stays on the last point, so the path ends there.
    CHECK(path.back().x >= 215);
    // Values past the grid wrap to their low byte.
    const std::array<float, 2> wide{300.0F, 10.0F};
    const std::vector<TagCell> wrapped = coney::tagPath(wide, 1);
    REQUIRE(wrapped.size() == 1);
    CHECK(wrapped[0] == TagCell{44, 10});
    CHECK(coney::tagPath({}, 3).empty());
}

TEST_CASE("following the path with the stick finishes the tag", "[tag_game]") {
    // The stick follows the path as a player would, at 0.8 deflection, within one charge.
    TagGame game(coney::tagPath(kNinePoints, 9), 0.0F, coney::tagTuning(2));
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    TagGame::Result result = TagGame::Result::Playing;
    for (int i = 0; i < 2000 && result == TagGame::Result::Playing; ++i) {
        const auto [x, y] = coney::test::tagStick(game);
        result = game.update(x, y, 16, spend);
        CHECK_FALSE(game.events().slipped);
    }
    CHECK(result == TagGame::Result::Finished);
    CHECK(game.fraction() > 0.95F);
    CHECK_FALSE(game.painted().empty());
}

TEST_CASE("a game starts where the spot's painted fraction says", "[tag_game]") {
    const std::vector<TagCell> path = coney::tagPath(kLine, 2);
    const TagGame game(path, 0.5F, coney::tagTuning(1));
    const std::size_t half = path.size() / 2;
    CHECK(game.progress() == half);
    CHECK(game.cursorX() == static_cast<float>(path[half].x));
}

TEST_CASE("leaving the path snaps the cursor back and pauses the game", "[tag_game]") {
    TagGame game(coney::tagPath(kLine, 2), 0.0F, coney::tagTuning(1));
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    bool slipped = false;
    for (int i = 0; i < 500 && !slipped; ++i) {
        game.update(0.0F, 1.0F, 30, spend);
        slipped = game.events().slipped;
    }
    REQUIRE(slipped);
    CHECK(game.cursorY() == 100.0F);
    CHECK(game.pauseLeftMs() == coney::tagTuning(1).pauseMs);
    // The pause holds the cursor.
    game.update(1.0F, 0.0F, 30, spend);
    CHECK(game.cursorY() == 100.0F);
}

TEST_CASE("a spent charge takes the next, and with none left the tag ends unfinished", "[tag_game]") {
    TagGame game(coney::tagPath(kLine, 2), 0.0F, coney::TagTuning{.chargeMs = 100, .pauseMs = 0, .cellsPerMs = 0.0F});
    int left = 1;
    const TagGame::SpendCharge spend = charges(left);
    bool spent = false;
    TagGame::Result result = TagGame::Result::Playing;
    for (int i = 0; i < 50 && result == TagGame::Result::Playing; ++i) {
        result = game.update(0.0F, 0.0F, 30, spend);
        spent = spent || game.events().chargeSpent;
    }
    CHECK(spent);
    CHECK(left == 0);
    CHECK(result == TagGame::Result::Unfinished);
    // The charge that found no can had just begun: nothing more to waste.
    CHECK_FALSE(game.wastesCharge());
}

TEST_CASE("abandoning early wastes no charge, late wastes it", "[tag_game]") {
    TagGame game(coney::tagPath(kLine, 2), 0.0F, coney::TagTuning{.chargeMs = 100, .pauseMs = 0, .cellsPerMs = 0.0F});
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    // The first update stamps the charge's start; 60 ms on, 40 % is left.
    for (int i = 0; i < 3; ++i) {
        game.update(0.0F, 0.0F, 30, spend);
    }
    TagGame early = game;
    early.abandon();
    CHECK(early.result() == TagGame::Result::Unfinished);
    CHECK_FALSE(early.wastesCharge());
    // 90 ms on, 10 % is left.
    game.update(0.0F, 0.0F, 30, spend);
    game.abandon();
    CHECK(game.wastesCharge());
}

namespace {

// A path along x at y = 100: `count` points 3 cells apart from x = 20.
std::vector<TagCell> straightPath(int count) {
    std::vector<TagCell> path;
    for (int i = 0; i < count; ++i) {
        path.push_back(TagCell{20 + (3 * i), 100});
    }
    return path;
}

// One update's elapsed time in these tests.
constexpr std::uint32_t kStepMs = 16;
// The updates of the 2 s speed ramp, and one more.
constexpr int kRampUpdates = (2000 / 16) + 2;
// The clock after rampUp().
constexpr std::uint64_t kRampedClock = static_cast<std::uint64_t>(kRampUpdates) * kStepMs;

// A pause of `pauseMs`, a charge of `chargeMs` (long by default) and 1/16 cell a millisecond: one cell in a 16 ms
// update at full deflection once the ramp is over (exact in binary floats).
coney::TagTuning cellTuning(std::uint32_t pauseMs = 500, std::uint32_t chargeMs = 1000000) {
    return coney::TagTuning{.chargeMs = chargeMs, .pauseMs = pauseMs, .cellsPerMs = 0.0625F};
}

// Still updates until the speed ramp (2 s from the first update) is over.
void rampUp(TagGame& game, const TagGame::SpendCharge& spend) {
    for (int i = 0; i < kRampUpdates; ++i) {
        game.update(0.0F, 0.0F, kStepMs, spend);
    }
}

// Moving updates straight up, off the path, until a slip; whether one came.
bool slipUpward(TagGame& game, const TagGame::SpendCharge& spend, std::uint64_t* clock = nullptr) {
    for (int i = 0; i < 40; ++i) {
        game.update(0.0F, 1.0F, kStepMs, spend);
        if (clock != nullptr) {
            *clock += kStepMs;
        }
        if (game.events().slipped) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("a 20-point path finishes when the progress reaches 17, not at 15 or 16", "[tag_game]") {
    TagGame game(straightPath(20), 0.0F, cellTuning());
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    rampUp(game, spend);
    std::size_t highest = 0;
    TagGame::Result result = TagGame::Result::Playing;
    for (int i = 0; i < 200 && result == TagGame::Result::Playing; ++i) {
        result = game.update(1.0F, 0.0F, kStepMs, spend);
        if (result == TagGame::Result::Playing) {
            CHECK(game.progress() < 17);
            CHECK(game.fraction() == static_cast<float>(game.progress()) / 20.0F);
            highest = std::max(highest, game.progress());
        }
    }
    REQUIRE(result == TagGame::Result::Finished);
    CHECK(highest == 16);
    CHECK(game.progress() == 17);
    CHECK(game.fraction() == 1.0F);
}

TEST_CASE("tracking moves to the first near point from the progress, not the nearest", "[tag_game]") {
    TagGame game(straightPath(20), 0.0F, cellTuning());
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    rampUp(game, spend);
    // Cell 24: 4 cells past point 0 (x 20) and 1 past point 1 (x 23): point 0 is near, so the progress stays.
    for (int i = 0; i < 4; ++i) {
        game.update(1.0F, 0.0F, kStepMs, spend);
    }
    REQUIRE(game.cursorX() == 24.0F);
    CHECK(game.progress() == 0);
    CHECK(game.onTrack());
    // Cell 26: 6 from point 0 (36 squared, not below it), so the first near one is point 1, though point 2 (x 26) is
    // nearer.
    game.update(1.0F, 0.0F, kStepMs, spend);
    game.update(1.0F, 0.0F, kStepMs, spend);
    REQUIRE(game.cursorX() == 26.0F);
    CHECK(game.progress() == 1);
}

TEST_CASE("near a point behind the progress the cursor is on track and the progress stays", "[tag_game]") {
    const std::vector<TagCell> path = straightPath(20);
    TagGame game(path, 0.0F, cellTuning());
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    rampUp(game, spend);
    for (int i = 0; i < 24; ++i) {
        game.update(1.0F, 0.0F, kStepMs, spend);
    }
    const std::size_t reached = game.progress();
    REQUIRE(reached >= 4);
    // Back to point p - 2, a cell at a time: always near, never moving the progress back.
    const auto target = static_cast<float>(path[reached - 2].x);
    while (game.cursorX() > target) {
        game.update(-1.0F, 0.0F, kStepMs, spend);
        CHECK(game.progress() == reached);
        CHECK(game.onTrack());
    }
}

TEST_CASE("five off-track moving updates split by on-track ones make the next moving update slip", "[tag_game]") {
    TagGame game(straightPath(20), 0.0F, cellTuning());
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    rampUp(game, spend);
    // Up 5 cells: still on track (25 squared).
    for (int i = 0; i < 5; ++i) {
        game.update(0.0F, 1.0F, kStepMs, spend);
    }
    REQUIRE(game.onTrack());
    // Five times: up to 6 cells off (off track, counted), then back down to 5 (on track: the count stays).
    for (int n = 1; n <= 5; ++n) {
        game.update(0.0F, 1.0F, kStepMs, spend);
        CHECK_FALSE(game.onTrack());
        CHECK(game.offTrackCount() == n);
        CHECK_FALSE(game.events().slipped);
        if (n < 5) {
            game.update(0.0F, -1.0F, kStepMs, spend);
            CHECK(game.onTrack());
            CHECK(game.offTrackCount() == n);
        }
    }
    // A still update counts nothing; the next moving one slips, even heading back toward the path.
    game.update(0.0F, 0.0F, kStepMs, spend);
    CHECK(game.offTrackCount() == 5);
    game.update(0.0F, -1.0F, kStepMs, spend);
    CHECK(game.events().slipped);
    CHECK(game.offTrackCount() == 0);
    CHECK(game.cursorY() == 100.0F);
    CHECK(game.pauseLeftMs() == 500);
    // Through the slip's pause the on-track flag keeps its last value: off track.
    CHECK_FALSE(game.onTrack());
}

TEST_CASE("the charge drains through a slip's pause and the next charge starts on time", "[tag_game]") {
    // The first update, at 16 ms, stamps the charge: it runs out at 2516 ms.
    constexpr std::uint64_t kChargeEnds = 2516;
    TagGame game(straightPath(20), 0.0F, cellTuning(1000, 2500));
    int left = 3;
    const TagGame::SpendCharge spend = charges(left);
    rampUp(game, spend);
    std::uint64_t clock = kRampedClock;
    REQUIRE(slipUpward(game, spend, &clock));
    REQUIRE(clock < kChargeEnds);
    REQUIRE(clock + 1000 > kChargeEnds);
    // Still updates through the pause: the charge keeps draining, and a new one starts at 2516 ms, inside the pause.
    std::uint32_t before = game.chargeLeftMs();
    bool spent = false;
    for (int i = 0; i < 100 && !spent; ++i) {
        game.update(0.0F, 0.0F, kStepMs, spend);
        clock += kStepMs;
        spent = game.events().chargeSpent;
        if (!spent) {
            CHECK(game.pauseLeftMs() > 0);
            CHECK(game.chargeLeftMs() < before);
            before = game.chargeLeftMs();
        }
    }
    REQUIRE(spent);
    CHECK(clock >= kChargeEnds);
    CHECK(clock < kChargeEnds + kStepMs);
    CHECK(left == 2);
    CHECK(game.pauseLeftMs() == 1000);
    CHECK(game.chargeLeft() == 1.0F);
}

TEST_CASE("a new charge restarts the speed ramp after its pause; a slip does not", "[tag_game]") {
    // After a slip's pause, the cursor moves at full speed at once.
    {
        TagGame game(straightPath(20), 0.0F, cellTuning(100));
        int left = 0;
        const TagGame::SpendCharge spend = charges(left);
        rampUp(game, spend);
        REQUIRE(slipUpward(game, spend));
        while (game.pauseLeftMs() > 0) {
            game.update(0.0F, 0.0F, kStepMs, spend);
        }
        const float from = game.cursorX();
        game.update(1.0F, 0.0F, kStepMs, spend);
        CHECK(game.cursorX() - from == 1.0F);
    }
    // After a new charge's pause, it starts again from (nearly) 0.
    {
        TagGame game(straightPath(20), 0.0F, cellTuning(100, 2500));
        int left = 1;
        const TagGame::SpendCharge spend = charges(left);
        bool spent = false;
        for (int i = 0; i < 400 && !spent; ++i) {
            game.update(0.0F, 0.0F, kStepMs, spend);
            spent = game.events().chargeSpent;
        }
        REQUIRE(spent);
        while (game.pauseLeftMs() > 0) {
            game.update(0.0F, 0.0F, kStepMs, spend);
        }
        const float from = game.cursorX();
        game.update(1.0F, 0.0F, kStepMs, spend);
        CHECK(game.cursorX() - from < 0.15F);
    }
}

TEST_CASE("a game started part way shows the path before it as painted", "[tag_game]") {
    const TagGame game(straightPath(20), 0.5F, cellTuning());
    CHECK(game.progress() == 10);
    CHECK(game.painted().size() == 10);
    CHECK(game.fraction() == 0.5F);
}

TEST_CASE("only overhead and ghetto weapons leave the hand as the spray starts", "[tag_game]") {
    CHECK(coney::tagDropsHeld(4));
    CHECK(coney::tagDropsHeld(6));
    CHECK_FALSE(coney::tagDropsHeld(3)); // a pipe, a bat or a crowbar stays
    CHECK_FALSE(coney::tagDropsHeld(0));
}
