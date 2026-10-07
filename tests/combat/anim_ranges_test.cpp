// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/anim_ranges.h"

#include <array>
#include <bit>
#include <cstdint>
#include <span>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

using Catch::Approx;
using coney::combat::AnimRangeList;
using coney::test::Bytes;

namespace {

// Appends one 16-byte record: direction (thousandths), reach, far (thousandths), damage, kind, flags.
void record(Bytes& bytes, std::int16_t x, std::int16_t y, float reach, std::int16_t far, std::int16_t damage,
            std::int16_t kind, std::uint16_t flags) {
    bytes.u16(static_cast<std::uint16_t>(x)).u16(static_cast<std::uint16_t>(y));
    bytes.u32(std::bit_cast<std::uint32_t>(reach));
    bytes.u16(static_cast<std::uint16_t>(far)).u16(static_cast<std::uint16_t>(damage));
    bytes.u16(static_cast<std::uint16_t>(kind)).u16(flags);
}

} // namespace

TEST_CASE("an Anim Range List decodes each record's direction, ranges, damage, kind and flags", "[combat]") {
    // Four ids: a forward attack with a far range, a snap right whose far range is left to the reach, an id with no
    // range data and one whose stored far range is short of its reach.
    Bytes bytes;
    bytes.u32(4);
    record(bytes, 0, 1000, 1.5F, 2499, 17, 10, 0x1000);
    record(bytes, 999, -12, 2.0F, 0, 31, 10, 0);
    record(bytes, 0, 0, 0.0F, 0, 0, 0, 0);
    record(bytes, 0, 1000, 2.0F, 1500, 0, 0, 0);
    auto list = AnimRangeList::parse(bytes.span());
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 4);

    const auto* first = list->find(0);
    REQUIRE(first != nullptr);
    CHECK(first->directionX == Approx(0.0F));
    CHECK(first->directionY == Approx(1.0F));
    CHECK(first->reach == Approx(1.5F));
    CHECK(first->kind == 10);
    CHECK(first->flags == 0x1000);
    CHECK(list->damage(0) == 17);
    CHECK(list->farRange(0) == Approx(2.499F));

    // A far range of 0 means the reach × 1.25; a negative direction survives the s16 read.
    const auto* snap = list->find(1);
    REQUIRE(snap != nullptr);
    CHECK(snap->directionX == Approx(0.999F));
    CHECK(snap->directionY == Approx(-0.012F));
    CHECK(list->farRange(1) == Approx(2.5F));
    CHECK(list->damage(1) == 31);
    // A stored far range not above the reach gives way to the reach × 1.25, too.
    CHECK(list->farRange(3) == Approx(2.5F));

    // Past the list: no record, no damage, no range.
    CHECK(list->find(4) == nullptr);
    CHECK(list->damage(400) == 0);
    CHECK(list->farRange(400) == 0.0F);
}

TEST_CASE("an Anim Range List shorter than its count is refused", "[combat]") {
    Bytes bytes;
    bytes.u32(2);
    record(bytes, 0, 1000, 1.0F, 0, 5, 10, 0);
    auto list = AnimRangeList::parse(bytes.span());
    REQUIRE_FALSE(list.has_value());
    CHECK(list.error().code == coney::ErrorCode::Truncated);

    CHECK_FALSE(AnimRangeList::parse(Bytes{}.u16(1).span()).has_value());
}

TEST_CASE("a class's damage table is written over the list, scaled for a player by the Warrior percentage",
          "[combat]") {
    // 700 records, each doing 40 (as the file's Rembrandt S1 does).
    Bytes bytes;
    bytes.u32(700);
    for (int i = 0; i < 700; ++i) {
        record(bytes, 0, 1000, 1.0F, 0, 40, 0, 0);
    }
    auto player = AnimRangeList::parse(bytes.span());
    REQUIRE(player.has_value());
    AnimRangeList civilian = *player;

    // Index 1 (S1) 15, index 0 (X1) 30, index 10 (the snaps) 20, index 2 (XX2) 0: kept.
    std::array<std::int16_t, coney::combat::kClassDamageEntries> values{};
    values[0] = 30;
    values[1] = 15;
    values[10] = 20;
    // A player at 115 %: 15 plays as 17, and 30 as 34 (not 35), as the original rounds.
    CHECK(coney::combat::applyClassDamage(*player, values, 115) == 8);
    CHECK(player->damage(12) == 17);
    CHECK(player->damage(11) == 34);
    CHECK(player->damage(25) == 23);
    CHECK(player->damage(30) == 23);
    CHECK(player->damage(13) == 40);
    // A civilian keeps the values unscaled.
    CHECK(coney::combat::applyClassDamage(civilian, values, 0) == 8);
    CHECK(civilian.damage(12) == 15);
    CHECK(civilian.damage(11) == 30);
    // A short table writes only what it has.
    AnimRangeList shortList = civilian;
    CHECK(coney::combat::applyClassDamage(shortList, std::span(values).first(1), 0) == 1);
}
