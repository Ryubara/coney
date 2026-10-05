// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/pair_placement.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

// The pair's placement: offsets in the grabber's frame, the alignment before the connecting clips, the gate at their
// end and the check a move in the hold makes, against the values on docs/research/combat.md#grab-posing.

using Catch::Approx;
using coney::anim::Vec3;
using namespace coney::human;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// A range list whose record 82 is the front hold's: direction (0.351, 0.936), reach 1.081 m.
coney::combat::AnimRangeList holdRanges() {
    coney::test::Bytes bytes;
    bytes.u32(83);
    for (std::uint32_t id = 0; id < 83; ++id) {
        const bool hold = id == 82;
        bytes.u16(hold ? 351 : 0).u16(hold ? 936 : 0).u32(std::bit_cast<std::uint32_t>(hold ? 1.081F : 0.0F));
        bytes.u16(0).u16(0).u16(0).u16(0);
    }
    auto list = coney::combat::AnimRangeList::parse(bytes.span());
    REQUIRE(list.has_value());
    return std::move(list).value_or(coney::combat::AnimRangeList{});
}

} // namespace

TEST_CASE("a point in a human's frame turns with its heading", "[pair_placement]") {
    // Facing +y, x is to the right; turned a quarter to the left (facing -x), the right is -y.
    const Vec3 ahead = fromFrame(Vec3{1, 2, 0}, 0.0F, Vec3{0.5F, 1.0F, 0.0F});
    CHECK(ahead.x == Approx(1.5F));
    CHECK(ahead.y == Approx(3.0F));
    const Vec3 left = fromFrame(Vec3{}, kPi / 2.0F, Vec3{0.5F, 1.0F, 0.0F});
    CHECK(left.x == Approx(-1.0F));
    CHECK(left.y == Approx(0.5F));
    const Vec3 back = toFrame(Vec3{}, kPi / 2.0F, left);
    CHECK(back.x == Approx(0.5F));
    CHECK(back.y == Approx(1.0F));
}

TEST_CASE("the hold's point is the range record's direction times its reach", "[pair_placement]") {
    const coney::combat::AnimRangeList ranges = holdRanges();
    const Vec3 hold = pairPoint(&ranges, 82, Vec3{});
    CHECK(hold.x == Approx(kFrontHoldOffset.x).margin(0.001F));
    CHECK(hold.y == Approx(kFrontHoldOffset.y).margin(0.001F));
    // No reach (or no list): the fallback.
    CHECK(pairPoint(&ranges, 84, kRearHoldOffset).x == kRearHoldOffset.x);
    CHECK(pairPoint(nullptr, 82, kFrontHoldOffset).y == kFrontHoldOffset.y);
}

TEST_CASE("the alignment faces the victim and puts it at the reach straight ahead", "[pair_placement]") {
    // The victim 2 m to the grabber's left (-x): the grabber turns a quarter left and slides 1.001 m towards it.
    const PairAlignment front = alignPair(Vec3{0, 0, 0}, Vec3{-2, 0, 0}, kConnectReachFront, 3.125F, false);
    CHECK(front.inRange);
    CHECK(front.grabberHeading == Approx(kPi / 2.0F));
    CHECK(front.grabberFeet.x == Approx(-2.0F + kConnectReachFront));
    CHECK(front.grabberFeet.y == Approx(0.0F).margin(1e-5));
    CHECK(std::fabs(front.victimHeading) == Approx(kPi / 2.0F)); // facing back along +x
    CHECK(front.victimHeading == Approx(-kPi / 2.0F));
    // From the rear the victim faces the way the grabber does.
    const PairAlignment rear = alignPair(Vec3{0, 0, 0}, Vec3{-2, 0, 0}, kConnectReachRear, 3.125F, true);
    CHECK(rear.victimHeading == Approx(kPi / 2.0F));
    // Beyond the far range (2.5 m × 1.25 for a player) it fails.
    CHECK_FALSE(alignPair(Vec3{0, 0, 0}, Vec3{0, 3.2F, 0}, kConnectReachFront, 3.125F, false).inRange);
}

TEST_CASE("the gate releases a victim beyond 1.297 m or 0.2 m up or down", "[pair_placement]") {
    // The front hold's reach 1.081: the larger of 1.281 and 1.297.
    CHECK(holdGatePasses(Vec3{}, Vec3{0, 1.29F, 0}, 1.081F));
    CHECK_FALSE(holdGatePasses(Vec3{}, Vec3{0, 1.31F, 0}, 1.081F));
    CHECK_FALSE(holdGatePasses(Vec3{}, Vec3{0, 1.0F, 0.25F}, 1.081F));
}

TEST_CASE("a move in the hold needs the victim within 0.3 m of its point", "[pair_placement]") {
    CHECK(pairInPlace(Vec3{}, 0.0F, Vec3{0.38F, 1.01F, 0}, kFrontHoldOffset));
    CHECK(pairInPlace(Vec3{}, 0.0F, Vec3{0.2F, 0.9F, 0}, kFrontHoldOffset));
    // Where the connecting clip starts it (straight ahead at 0.999 m) is 0.38 m off: refused.
    CHECK_FALSE(pairInPlace(Vec3{}, 0.0F, Vec3{0.0F, 0.999F, 0}, kFrontHoldOffset));
}

TEST_CASE("the alignment lasts a tenth of the time to the first contact, over the rate", "[pair_placement]") {
    coney::anim::AnimClip clip;
    clip.duration = 0.467F;
    // No contact event: the duration (72's two updates at rate 0.75).
    CHECK(alignSeconds(clip, 0.75F) == Approx(0.1F * 0.467F / 0.75F));
    // A contact event (type 9) at frame 6: 0.2 s.
    clip.events.push_back(coney::anim::ClipEvent{.frame = 6, .type = 9, .word = 0, .position = {}, .rotation = {}});
    clip.events.push_back(coney::anim::ClipEvent{.frame = 3, .type = 8, .word = 0, .position = {}, .rotation = {}});
    CHECK(alignSeconds(clip, 1.0F) == Approx(0.02F));
}
