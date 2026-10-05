// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/meters.h"

#include <bit>
#include <cstdint>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

using Catch::Approx;
using namespace coney::combat;

namespace {

// Game time in whole milliseconds after `update` fixed steps of 1/30 s, as the game timer keeps it.
std::uint64_t msAt(std::uint64_t update) { return update * 1000 / 30; }

// A range list where anim `id` (and every id before it) does `damage`.
AnimRangeList rangesWithDamage(std::size_t count, std::int16_t damage) {
    coney::test::Bytes bytes;
    bytes.u32(static_cast<std::uint32_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        bytes.u16(0).u16(1000).u32(std::bit_cast<std::uint32_t>(1.0F)).u16(0);
        bytes.u16(static_cast<std::uint16_t>(damage)).u16(10).u16(0);
    }
    auto list = AnimRangeList::parse(bytes.span());
    REQUIRE(list.has_value());
    return std::move(list).value_or(AnimRangeList{});
}

} // namespace

TEST_CASE("health takes damage down to 0 and reports its fraction", "[combat]") {
    Health health(314, 900);
    CHECK(health.fraction() == Approx(314.0F / 900.0F));
    CHECK(health.apply(17) == 17);
    CHECK(health.value() == 297);
    CHECK(health.apply(400) == 297);
    CHECK(health.depleted());
    CHECK(Health(600).value() == 600);
}

TEST_CASE("pending damage keeps the update's largest hit and its kind", "[combat]") {
    PendingDamage pending;
    pending.add(17, 10);
    pending.add(53, 38);
    pending.add(36, 11);
    CHECK(pending.damage() == 53);
    CHECK(pending.kind() == 38);
    Health civilian(600);
    CHECK(pending.applyTo(civilian) == 53);
    CHECK(civilian.value() == 547);
    CHECK(pending.damage() == 0);
}

TEST_CASE("a strike's damage is its anim's range list value, doubled or quartered", "[combat]") {
    AnimRangeList ranges = rangesWithDamage(20, 36);
    CHECK(strikeDamage(ranges, 16) == 36);
    CHECK(strikeDamage(ranges, 16, true) == 72);
    CHECK(strikeDamage(ranges, 16, false, true) == 9);
    CHECK(strikeDamage(ranges, 300) == 0);
    CHECK(strikeDamage(ranges, -1) == 0);
    // The class's damage table overrides the file's value.
    CHECK(ranges.setDamage(12, 17));
    CHECK(strikeDamage(ranges, 12) == 17);
    CHECK_FALSE(ranges.setDamage(300, 5));
}

TEST_CASE("the power meter refills 2 an update and drains 1 every 2 while holding someone", "[combat]") {
    PowerMeter power;
    CHECK(power.value() == 400);
    // A grab strike costs 0.2 × 0.5 of 400, a throw 0.25.
    CHECK(power.spend(0.1F) == 40);
    CHECK(power.spend(0.25F) == 100);
    CHECK(power.value() == 260);

    // Refill at 60 a second: 60 over 30 updates of game time in whole milliseconds.
    for (std::uint64_t update = 1; update <= 30; ++update) {
        power.update(msAt(update), false, 15.0F);
    }
    CHECK(power.value() == 320);

    // Drain at 15 a second: about 1 every 2 updates, 21 over the 1.4 s of a tackle.
    const int before = power.value();
    for (std::uint64_t update = 31; update <= 72; ++update) {
        power.update(msAt(update), true, 15.0F);
    }
    CHECK(before - power.value() == 21);

    // Refilling stops at the maximum; spending stops at 0.
    for (std::uint64_t update = 73; update <= 200; ++update) {
        power.update(msAt(update), false, 15.0F);
    }
    CHECK(power.value() == 400);
    CHECK(power.fraction() == Approx(1.0F));
    power.set(30);
    CHECK(power.spend(0.25F) == 30);
    CHECK(power.value() == 0);
}

TEST_CASE("rage gains points times the class's percentage, the part above the cap at 0.1", "[combat]") {
    const CombatTuning tuning;
    RageMeter rage;
    CHECK(rage.maximum() == 78);
    // 7 points × 144 % = 10.08: 10.
    CHECK(rage.add(7.0F, tuning) == 10);
    // 1 point: 1.44, rounded to 1; halved: 0.72, rounded to 1.
    CHECK(rage.add(1.0F, tuning) == 1);
    CHECK(rage.add(1.0F, tuning, RageGain{true, 1.0F}) == 1);
    // 35 points: 25 in full and 10 at 0.1, 26 × 1.44 = 37.44.
    CHECK(rage.add(35.0F, tuning) == 37);
    CHECK(rage.value() == 49);
    // The meter stops at its maximum.
    CHECK(rage.add(30.0F, tuning) == 29);
    CHECK(rage.full());
}

TEST_CASE("rage starts only with a full meter, gains nothing while on and drains to its end", "[combat]") {
    const CombatTuning tuning;
    RageMeter rage;
    rage.set(77);
    CHECK_FALSE(rage.start(0));
    rage.set(78);
    CHECK(rage.start(0));
    CHECK(rage.raging());
    CHECK_FALSE(rage.start(0));
    CHECK(rage.add(10.0F, tuning) == 0);

    // About 9.5 a second: after 2 s the meter has lost 19.
    for (std::uint64_t update = 1; update <= 60; ++update) {
        rage.update(msAt(update), tuning.rageDrainPerSecond);
    }
    CHECK(rage.value() == 59);
    std::uint64_t update = 60;
    while (rage.raging() && update < 1000) {
        rage.update(msAt(++update), tuning.rageDrainPerSecond);
    }
    CHECK_FALSE(rage.raging());
    CHECK(rage.value() == 0);
}
