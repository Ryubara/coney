// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/grab.h"

#include <array>
#include <bit>
#include <cstdint>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

using Catch::Approx;
using namespace coney::combat;

namespace {

// A range list of 71 ids where the grab's (70) far range is 2.499 m and the tackle's (3) is left to its reach of
// 2.3992 m (so 2.999 m), as the player's read at runtime.
AnimRangeList grabRanges() {
    coney::test::Bytes bytes;
    bytes.u32(71);
    for (std::uint32_t id = 0; id < 71; ++id) {
        const float reach = id == 3 ? 2.3992F : 1.0F;
        const std::uint16_t far = id == 70 ? 2499 : 0;
        bytes.u16(0).u16(1000).u32(std::bit_cast<std::uint32_t>(reach)).u16(far).u16(10).u16(10).u16(0);
    }
    auto list = AnimRangeList::parse(bytes.span());
    REQUIRE(list.has_value());
    return std::move(list).value_or(AnimRangeList{});
}

// One grab update with `command` and the stick at (x, y) in the player's frame.
GrabOutcome grabWith(CommandId command, Stick stick, PowerMeter& power, CombatRandom& random, bool wall = false) {
    GrabInput input;
    input.command = command;
    input.stick = stick;
    input.wallInReach = wall;
    return updateGrab(input, power, CombatTuning{}, random);
}

} // namespace

TEST_CASE("a grab searches 3.12 m and a tackle 3.75 m from the clips' far ranges", "[combat]") {
    const AnimRangeList ranges = grabRanges();
    CHECK(grabSearchRange(ranges, GrabKind::Grab, 1.25F) == Approx(3.124F).margin(0.001F));
    CHECK(grabSearchRange(ranges, GrabKind::Tackle, 1.25F) == Approx(3.749F).margin(0.001F));
}

TEST_CASE("a grab or tackle is refused while an attack phase is set", "[combat]") {
    CHECK(grabAllowed(0));
    CHECK_FALSE(grabAllowed(0x1));
    CHECK_FALSE(grabAllowed(0x2));
    CHECK_FALSE(grabAllowed(0x40000));
    CHECK(grabAllowed(0x8)); // not among the refusing bits
}

TEST_CASE("the target search takes the nearest available candidate within range", "[combat]") {
    const std::array<TargetCandidate, 4> candidates{
        TargetCandidate{{0.0F, 3.5F, 0.0F}, true},  // a tackle's reach, not a grab's
        TargetCandidate{{0.0F, 1.0F, 0.0F}, false}, // nearest, but not available
        TargetCandidate{{2.0F, 2.0F, 0.0F}, true},  // 2.83 m
        TargetCandidate{{-6.0F, 0.0F, 0.0F}, true},
    };
    const coney::anim::Vec3 player{};
    CHECK(nearestTarget(player, candidates, 3.12F) == 2);
    CHECK(nearestTarget(player, candidates, 2.5F) == kNoTarget);
    CHECK(nearestTarget({0.0F, 3.0F, 0.0F}, candidates, 3.75F) == 0);
}

TEST_CASE("a throw goes by the stick's side, the THROW_02 set with a wall in reach", "[combat]") {
    CHECK(throwAttack(sideOf(Stick{0.0F, 0.6F}.angleDegrees()), false) == anim_id::kThrow1Front);
    CHECK(throwAttack(sideOf(Stick{0.5F, 0.4F}.angleDegrees()), false) == anim_id::kThrow1Right);
    CHECK(throwAttack(sideOf(Stick{-0.1F, -0.4F}.angleDegrees()), false) == anim_id::kThrow1Rear);
    CHECK(throwAttack(sideOf(Stick{-0.6F, 0.1F}.angleDegrees()), false) == anim_id::kThrow1Left);
    CHECK(throwAttack(Side::Front, true) == anim_id::kThrow2Front);
    CHECK(throwAttack(Side::Rear, true) == anim_id::kThrow2Rear);
}

TEST_CASE("in a grab, strikes cost 40, a throw 100, and the power strike needs a quarter of the meter", "[combat]") {
    PowerMeter power;
    CombatRandom random(7);

    // Square: 51 or 53 at random, both seen over a few presses; 40 of 400 each.
    bool sawFirst = false;
    bool sawSecond = false;
    for (int i = 0; i < 6; ++i) {
        power.set(400);
        const GrabOutcome strike = grabWith(command::kSquarePressed, {}, power, random);
        CHECK(strike.action == GrabAction::Strike);
        CHECK(strike.powerSpent == 40);
        sawFirst = sawFirst || strike.animId == anim_id::kGrabComboStrike1;
        sawSecond = sawSecond || strike.animId == anim_id::kGrabComboStrike2;
        CHECK((strike.animId == anim_id::kGrabComboStrike1 || strike.animId == anim_id::kGrabComboStrike2));
    }
    CHECK(sawFirst);
    CHECK(sawSecond);

    // Cross (its 0x10): 55.
    power.set(400);
    CHECK(grabWith(command::kCrossLongHold, {}, power, random).animId == anim_id::kGrabComboStrike3);
    CHECK(power.value() == 360);

    // Circle with the stick at 0.6 ahead: the front throw, 100 of the meter. A stick of 0.2 spins instead.
    const GrabOutcome thrown = grabWith(command::kCirclePressed, {0.0F, 0.6F}, power, random);
    CHECK(thrown.action == GrabAction::Throw);
    CHECK(thrown.animId == anim_id::kThrow1Front);
    CHECK(thrown.powerSpent == 100);
    const GrabOutcome spun = grabWith(command::kCirclePressed, {0.1F, 0.17F}, power, random);
    CHECK(spun.action == GrabAction::Spin);
    CHECK(spun.powerSpent == 0);

    // The power strike: above 0.25 of the meter it plays, at or below it nothing happens.
    power.set(101);
    const GrabOutcome strong = grabWith(command::kCrossSquare, {}, power, random);
    CHECK(strong.action == GrabAction::PowerStrike);
    CHECK(strong.animId == anim_id::kGrabPower1Strike1);
    power.set(100);
    CHECK(grabWith(command::kCrossSquare, {}, power, random).action == GrabAction::None);

    // Triangle mugs only a victim that qualifies.
    GrabInput mug;
    mug.command = command::kTrianglePressed;
    CHECK(updateGrab(mug, power, CombatTuning{}, random).action == GrabAction::None);
    mug.victimMuggable = true;
    CHECK(updateGrab(mug, power, CombatTuning{}, random).action == GrabAction::Mug);

    // In rage the power strike is the second set's.
    GrabInput rage;
    rage.command = command::kCrossSquare;
    rage.raging = true;
    power.set(400);
    CHECK(updateGrab(rage, power, CombatTuning{}, random).animId == anim_id::kGrabPower2Strike1);
}
