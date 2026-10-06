// SPDX-License-Identifier: GPL-3.0-or-later
// The tagging stick game (docs/research/crimes.md#tagging): the difficulty table, the pattern's Catmull-Rom path, the
// cursor tracing it to a finish, a slip snapping it back, the charges of paint and the 30 % rule.
#include "warriors/tag_game.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::TagCell;
using coney::TagGame;

namespace {

// A straight pattern along x from (20, 100) to (220, 100).
constexpr std::array<float, 4> kLine{20.0F, 100.0F, 220.0F, 100.0F};

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
    TagGame game(coney::tagPath(kLine, 2), 0.0F, coney::tagTuning(2));
    int left = 0;
    const TagGame::SpendCharge spend = charges(left);
    TagGame::Result result = TagGame::Result::Playing;
    for (int i = 0; i < 2000 && result == TagGame::Result::Playing; ++i) {
        result = game.update(1.0F, 0.0F, 16, spend);
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
    // Out of paint, the charge in use was gone: under 30 % left.
    CHECK(game.wastesCharge());
}

TEST_CASE("abandoning early wastes no charge", "[tag_game]") {
    TagGame game(coney::tagPath(kLine, 2), 0.0F, coney::tagTuning(1));
    int left = 0;
    game.update(0.0F, 0.0F, 30, charges(left));
    game.abandon();
    CHECK(game.result() == TagGame::Result::Unfinished);
    CHECK_FALSE(game.wastesCharge());
}
