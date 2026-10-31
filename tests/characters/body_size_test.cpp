// SPDX-License-Identifier: GPL-3.0-or-later
// A new human's random body size (docs/research/characters.md#body-scale): one Random_Int(99) on the gameplay stream
// for the humans it does not skip, mapped by sex, hat, brain type and gang kind. The table is a synthetic one.
#include "characters/body_size.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"

using coney::GameRandom;
using coney::RandomStream;
using coney::characters::BodySizeInput;
using coney::characters::randomBodySize;

namespace {

// A stream whose first gameplay draw is `r` (entry 1 of a synthetic table).
GameRandom drawing(std::uint32_t r) {
    std::vector<std::uint32_t> table(GameRandom::kTableSize, 0U);
    table[1] = r;
    GameRandom random;
    random.setTable(table);
    return random;
}

// The size `input` gets for the draw `r`.
std::optional<float> sizeFor(const BodySizeInput& input, std::uint32_t r) {
    GameRandom random = drawing(r);
    return randomBodySize(input, random);
}

// A gang member (brain type 2) of category 2 in a gang of kind 5, no hat.
BodySizeInput gangMember() {
    return BodySizeInput{.classType = 40, .brainType = 2, .category = 2, .woman = false, .hat = false, .gangKind = 5};
}

} // namespace

TEST_CASE("a man's random size: 1.0 to 30, 1.02 or 0.98 to 54, 1.01 to 89, then 1.0", "[characters]") {
    // Expected: characters.md#body-scale, confirmed (code)
    const BodySizeInput man = gangMember();
    CHECK(sizeFor(man, 30) == std::optional<float>(1.0F));
    // 31-54 without a hat: a gang member of a gang of kind below 23 or a cop grows, anyone else shrinks.
    CHECK(sizeFor(man, 31) == std::optional<float>(1.02F));
    CHECK(sizeFor(man, 54) == std::optional<float>(1.02F));
    BodySizeInput bigGang = man;
    bigGang.gangKind = 23;
    CHECK(sizeFor(bigGang, 40) == std::optional<float>(0.98F));
    BodySizeInput cop = man;
    cop.brainType = 1;
    cop.gangKind = std::nullopt;
    CHECK(sizeFor(cop, 40) == std::optional<float>(1.02F));
    BodySizeInput civilian = man;
    civilian.brainType = 4;
    CHECK(sizeFor(civilian, 40) == std::optional<float>(0.98F));
    // A hat takes 31-54 to 1.01 like 55-89.
    BodySizeInput hatted = man;
    hatted.hat = true;
    CHECK(sizeFor(hatted, 40) == std::optional<float>(1.01F));
    CHECK(sizeFor(man, 55) == std::optional<float>(1.01F));
    CHECK(sizeFor(man, 89) == std::optional<float>(1.01F));
    CHECK(sizeFor(man, 90) == std::optional<float>(1.0F));
    // The draw is 0-99, the raw number mod 100: 100 is 0 (1.0) and 140 is 40 (1.02).
    CHECK(sizeFor(man, 100) == std::optional<float>(1.0F));
    CHECK(sizeFor(man, 140) == std::optional<float>(1.02F));
}

TEST_CASE("a woman's random size falls by 0.01 a quarter of the draw, a hat keeping it at 0.99 or more",
          "[characters]") {
    // Expected: characters.md#body-scale, confirmed (code)
    BodySizeInput woman = gangMember();
    woman.woman = true;
    // value_or(-1) keeps an empty result a failed check rather than a read of an empty optional.
    CHECK(sizeFor(woman, 0).value_or(-1.0F) == Catch::Approx(1.0F));
    CHECK(sizeFor(woman, 25).value_or(-1.0F) == Catch::Approx(0.99F));
    CHECK(sizeFor(woman, 50).value_or(-1.0F) == Catch::Approx(0.98F));
    CHECK(sizeFor(woman, 99).value_or(-1.0F) == Catch::Approx(0.97F));
    woman.hat = true;
    CHECK(sizeFor(woman, 99).value_or(-1.0F) == Catch::Approx(0.99F));
    CHECK(sizeFor(woman, 0).value_or(-1.0F) == Catch::Approx(1.0F));
}

TEST_CASE("the random size draws once from the gameplay stream, and not at all for the humans it skips",
          "[characters]") {
    // Expected: characters.md#body-scale, confirmed (code)
    GameRandom random = drawing(0);
    CHECK(randomBodySize(gangMember(), random).has_value());
    CHECK(random.index(RandomStream::Gameplay) == 1);

    // A Warrior, categories 12 and 13, and a listed type: no size and no draw.
    BodySizeInput warrior = gangMember();
    warrior.brainType = 3;
    BodySizeInput twelve = gangMember();
    twelve.category = 12;
    BodySizeInput thirteen = gangMember();
    thirteen.category = 13;
    BodySizeInput listed = gangMember();
    listed.classType = 351;
    for (const BodySizeInput& skipped : {warrior, twelve, thirteen, listed}) {
        GameRandom untouched = drawing(0);
        CHECK_FALSE(randomBodySize(skipped, untouched).has_value());
        CHECK(untouched.index(RandomStream::Gameplay) == 0);
    }
}
