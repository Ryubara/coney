// SPDX-License-Identifier: GPL-3.0-or-later
// What one click of the AI fighters page spawns: the models picked (a chosen one, or a random one each from a seeded
// generator) and the spots they stand on.
#include "debug/fighter_spawn.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::debug::fighterSpots;
using coney::debug::kMaxFighterAmount;
using coney::debug::pickFighterModels;
using coney::debug::SpawnRandom;

namespace {

const std::vector<std::string> kModels{"alpha", "bravo", "charlie", "delta", "echo"};

} // namespace

TEST_CASE("the spawn random generator repeats from its seed and never sticks at zero", "[debug][fighters]") {
    SpawnRandom a(123);
    SpawnRandom b(123);
    for (int i = 0; i < 20; ++i) {
        CHECK(a.next() == b.next());
    }
    // A seed of 0 would stay 0 for ever in xorshift; it counts as 1.
    SpawnRandom zero(0);
    CHECK(zero.next() != 0);
    // below() stays in range, and a range of nothing gives 0.
    SpawnRandom r(7);
    for (int i = 0; i < 100; ++i) {
        CHECK(r.below(5) < 5);
    }
    CHECK(r.below(0) == 0);
}

TEST_CASE("a chosen model is used for every fighter", "[debug][fighters]") {
    SpawnRandom random(1);
    const std::vector<std::string> picked = pickFighterModels(kModels, 3, 4, random); // 3: "charlie" (0 is Random)
    CHECK(picked == std::vector<std::string>(4, "charlie"));
}

TEST_CASE("Random gives each fighter its own draw, the same draws from the same seed", "[debug][fighters]") {
    SpawnRandom first(99);
    const std::vector<std::string> a = pickFighterModels(kModels, 0, 40, first);
    SpawnRandom second(99);
    const std::vector<std::string> b = pickFighterModels(kModels, 0, 40, second);
    REQUIRE(a.size() == 40);
    CHECK(a == b);
    // Every pick is one of the models, and 40 draws from 5 reach more than one of them.
    std::set<std::string> seen;
    for (const std::string& model : a) {
        CHECK(std::ranges::find(kModels, model) != kModels.end());
        seen.insert(model);
    }
    CHECK(seen.size() > 1);
    // A different seed gives a different run.
    SpawnRandom other(100);
    CHECK(pickFighterModels(kModels, 0, 40, other) != a);
}

TEST_CASE("successive clicks continue the random run instead of repeating it", "[debug][fighters]") {
    SpawnRandom random(5);
    const std::vector<std::string> one = pickFighterModels(kModels, 0, 8, random);
    const std::vector<std::string> two = pickFighterModels(kModels, 0, 8, random);
    CHECK(one != two);
}

TEST_CASE("the amount is clamped and an empty list gives the scene's own character", "[debug][fighters]") {
    SpawnRandom random(1);
    CHECK(pickFighterModels(kModels, 0, 0, random).empty());
    CHECK(pickFighterModels(kModels, 0, -3, random).empty());
    CHECK(pickFighterModels(kModels, 0, 1000, random).size() == static_cast<std::size_t>(kMaxFighterAmount));
    // No models known: every entry is empty, which the play mode reads as the scene's character.
    const std::vector<std::string> none;
    CHECK(pickFighterModels(none, 0, 3, random) == std::vector<std::string>(3, std::string{}));
    CHECK(pickFighterModels(none, 4, 2, random) == std::vector<std::string>(2, std::string{}));
    // A choice past the end counts as Random.
    const std::vector<std::string> past = pickFighterModels(kModels, 99, 6, random);
    CHECK(past.size() == 6);
}

TEST_CASE("one fighter stands exactly the distance ahead of the player", "[debug][fighters]") {
    const auto spots = fighterSpots({10.0F, 20.0F, 1.0F}, 0.0F, 4.0F, 1);
    REQUIRE(spots.size() == 1);
    CHECK(spots[0].x == Approx(10.0F));
    CHECK(spots[0].y == Approx(24.0F));
    CHECK(spots[0].z == Approx(1.0F));
    // Facing 90 degrees (counter-clockwise from +y) is facing -x.
    const auto left = fighterSpots({0.0F, 0.0F, 0.0F}, 90.0F, 4.0F, 1);
    CHECK(left[0].x == Approx(-4.0F).margin(1e-4));
    CHECK(left[0].y == Approx(0.0F).margin(1e-4));
}

TEST_CASE("several fighters stand in rows of five, centred on the player's line", "[debug][fighters]") {
    const auto spots = fighterSpots({0.0F, 0.0F, 0.0F}, 0.0F, 3.0F, 7);
    REQUIRE(spots.size() == 7);
    // The first row of five: across the line (x), all 3 m ahead, symmetric about x = 0.
    for (int i = 0; i < 5; ++i) {
        CHECK(spots[static_cast<std::size_t>(i)].y == Approx(3.0F));
    }
    CHECK(spots[0].x == Approx(-spots[4].x));
    CHECK(spots[2].x == Approx(0.0F).margin(1e-4));
    CHECK(spots[1].x - spots[0].x == Approx(1.2F));
    // The second row of two is one spacing further out, centred.
    CHECK(spots[5].y == Approx(4.2F));
    CHECK(spots[5].x == Approx(-spots[6].x));
    // No two stand on the same spot.
    for (std::size_t i = 0; i < spots.size(); ++i) {
        for (std::size_t j = i + 1; j < spots.size(); ++j) {
            CHECK(std::hypot(spots[i].x - spots[j].x, spots[i].y - spots[j].y) > 1.0F);
        }
    }
    CHECK(fighterSpots({}, 0.0F, 3.0F, 0).empty());
    CHECK(fighterSpots({}, 0.0F, 3.0F, 1000).size() == static_cast<std::size_t>(kMaxFighterAmount));
}
