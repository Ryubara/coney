// SPDX-License-Identifier: GPL-3.0-or-later
// The tag spots (docs/research/crimes.md#tag-spots): their messages, the fade and the tagger's messages, a CPU
// tagger's spray, and a player's tag session from the paint to the spot's end.
#include "world_objects/tag_spots.h"

#include <array>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "warriors/inventory.h"
#include "warriors/tag_game.h"
#include "warriors/tag_session.h"

using coney::world_objects::TagSpot;
using coney::world_objects::TagSpots;

namespace {

constexpr double kTag = 40.0;
constexpr double kHuman = 7.0;

// Spots that log what they tell taggers and the path flag.
struct Logged {
    TagSpots spots;
    std::vector<std::tuple<double, int, double>> told;
    std::vector<bool> flag;

    Logged() {
        spots.setTaggerMessage(
            [this](double human, int message, double tag) { told.emplace_back(human, message, tag); });
        spots.setPathFlag([this](double /*tag*/, bool on) { flag.push_back(on); });
    }
};

} // namespace

TEST_CASE("CfgTagSettings configures a spot, a negative depth counting as 0", "[tag_spots]") {
    TagSpots spots;
    spots.configure(kTag, 0x60000, 0.25F, 0.01F, -2.0F);
    const TagSpot* spot = spots.find(kTag);
    REQUIRE(spot != nullptr);
    CHECK(spot->sprite == 0x60000U);
    CHECK(spot->start == 0.25F);
    CHECK(spot->fadeStep == 0.01F);
    CHECK(spot->depth == 0.0F);
    CHECK(spot->sprayMode == TagSpot::kPaintIn);
    CHECK(spot->fraction == 0.0F);
}

TEST_CASE("ProcessTag's states paint, blank and choose the next spray", "[tag_spots]") {
    TagSpots spots;
    spots.setState(kTag, 6);
    CHECK(spots.find(kTag)->fraction == 1.0F);
    spots.setSprayMode(kTag, false);
    CHECK(spots.find(kTag)->sprayMode == TagSpot::kWipeOut);
    CHECK(spots.find(kTag)->fraction == 1.0F); // the look is unchanged
    spots.setState(kTag, 4);
    CHECK(spots.find(kTag)->fraction == 0.0F);
    CHECK(spots.find(kTag)->sprayMode == TagSpot::kPaintIn);
}

TEST_CASE("a tagger at a spot fades it in, and is told he is done at 1", "[tag_spots]") {
    Logged l;
    l.spots.configure(kTag, 0x50000, 0.5F, 0.25F, 1.0F);
    l.spots.setTagger(kTag, kHuman);
    REQUIRE(l.told.size() == 1);
    // Parenthesised: libstdc++ 14's tuple-like operator<=> breaks on Catch2's expression decomposer.
    CHECK((l.told[0] == std::tuple{kHuman, TagSpots::kTaggerSpray, kTag}));
    CHECK(l.flag == std::vector<bool>{true});
    for (int i = 0; i < 4; ++i) {
        l.spots.update();
    }
    CHECK(l.spots.find(kTag)->fraction == 1.0F);
    CHECK(l.spots.find(kTag)->fadeMode == TagSpot::kStill);
    CHECK(std::get<1>(l.told.back()) == TagSpots::kTaggerDone);
    // A finished spot has nothing to paint: the next tagger is told he is done at once.
    l.spots.setTagger(kTag, kHuman + 1);
    // Parenthesised: libstdc++ 14's tuple-like operator<=> breaks on Catch2's expression decomposer.
    CHECK((l.told.back() == std::tuple{kHuman + 1, TagSpots::kTaggerDone, kTag}));
}

TEST_CASE("a wipe-out spray fades a painted spot to blank", "[tag_spots]") {
    TagSpots spots;
    spots.configure(kTag, 0x50000, 0.5F, 0.5F, 1.0F);
    spots.setState(kTag, 6);
    spots.setSprayMode(kTag, false);
    spots.setTagger(kTag, kHuman);
    spots.update();
    spots.update();
    CHECK(spots.find(kTag)->fraction == 0.0F);
    CHECK(spots.find(kTag)->fadeMode == TagSpot::kStill);
}

TEST_CASE("hiding a spot clears its path flag, tells the tagger and stops the fade", "[tag_spots]") {
    Logged l;
    l.spots.setTagger(kTag, kHuman);
    l.spots.hide(kTag);
    CHECK(l.flag.back() == false);
    CHECK(std::get<1>(l.told.back()) == TagSpots::kTaggerDone);
    CHECK(l.spots.find(kTag)->fadeMode == TagSpot::kStill);
    l.spots.remove(kTag);
    CHECK(l.spots.find(kTag) == nullptr);
}

TEST_CASE("a CPU tagger's spray reports the spot finished only when it ends at 1", "[tag_spots]") {
    TagSpots spots;
    CHECK_FALSE(spots.spray(kTag, kHuman, 0.5F, false));
    CHECK(spots.find(kTag)->tagger == kHuman);
    CHECK(spots.find(kTag)->fraction == 0.5F);
    CHECK(spots.spray(kTag, kHuman, 0.9F, true));
    CHECK(spots.find(kTag)->fraction == 1.0F);
}

TEST_CASE("a player's tag session paints the spot from the inventory's paint", "[tag_spots]") {
    TagSpots spots;
    coney::Inventory inventory;
    CHECK_FALSE(coney::TagSession::hasPaint(inventory, 0));
    inventory.setSprayPaint(0, 2);
    REQUIRE(coney::TagSession::hasPaint(inventory, 0));
    constexpr std::array<float, 4> kLine{20.0F, 100.0F, 220.0F, 100.0F};
    coney::TagSession session(spots, inventory, 0, kHuman, kTag, coney::tagPath(kLine, 2), coney::tagTuning(3));
    for (int i = 0; i < 2000 && !session.ended(); ++i) {
        session.update(1.0F, 0.0F, 16);
        if (!session.ended()) {
            CHECK(spots.find(kTag)->fraction == session.game().fraction());
        }
    }
    REQUIRE(session.ended());
    CHECK(session.end().finished);
    CHECK(spots.find(kTag)->fraction == 1.0F);
    CHECK(inventory.count(0, coney::item::kSprayPaint) == 2); // a finish within one charge spends none
}

TEST_CASE("an abandoned session late in a charge spends it", "[tag_spots]") {
    TagSpots spots;
    coney::Inventory inventory;
    inventory.setSprayPaint(0, 2);
    constexpr std::array<float, 4> kLine{20.0F, 100.0F, 220.0F, 100.0F};
    coney::TagSession session(spots, inventory, 0, kHuman, kTag, coney::tagPath(kLine, 2),
                              coney::TagTuning{.chargeMs = 100, .pauseMs = 0, .cellsPerMs = 0.0F});
    session.update(0.0F, 0.0F, 30);
    session.update(0.0F, 0.0F, 30);
    session.update(0.0F, 0.0F, 30); // 10 ms of the 100 left
    session.abandon();
    CHECK_FALSE(session.end().finished);
    CHECK(session.end().wastedCharge);
    CHECK(inventory.count(0, coney::item::kSprayPaint) == 1);
    CHECK(spots.find(kTag)->fraction < 1.0F);
}
