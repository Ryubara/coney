// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/reactions.h"

#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"

using namespace coney::combat;

namespace {

// A reaction to attack `animId` with hit code `code` from `side`.
int react(int animId, int code, Side side, float above = 0.0F, bool hurt = false) {
    ReactionInput input;
    input.attackAnim = animId;
    input.code = code;
    input.side = side;
    input.attackerAbove = above;
    input.victimHurt = hurt;
    return hitReaction(input).animId;
}

} // namespace

TEST_CASE("a hit code splits into direction, height and strength", "[combat]") {
    const HitCode ssx3 = decodeHitCode(0x25);
    CHECK(ssx3.direction == 1);
    CHECK(ssx3.height == 1);
    CHECK(ssx3.strength == 2);
    const HitCode charge = decodeHitCode(0x36);
    CHECK(charge.direction == 2);
    CHECK(charge.height == 1);
    CHECK(charge.strength == 3);
}

TEST_CASE("the side is where the attacker stands around the victim's facing", "[combat]") {
    const coney::anim::Vec3 victim{0.0F, 0.0F, 0.0F};
    // The victim faces +y (heading 0): its right is +x.
    CHECK(victimSide(victim, 0.0F, {0.0F, 1.0F, 0.0F}) == Side::Front);
    CHECK(victimSide(victim, 0.0F, {1.0F, 0.2F, 0.0F}) == Side::Right);
    CHECK(victimSide(victim, 0.0F, {-1.0F, 0.0F, 0.0F}) == Side::Left);
    CHECK(victimSide(victim, 0.0F, {0.1F, -1.0F, 0.0F}) == Side::Rear);
    // Turned a quarter anticlockwise it faces -x, its right is +y.
    const float quarter = std::numbers::pi_v<float> / 2.0F;
    CHECK(victimSide(victim, quarter, {-1.0F, 0.0F, 0.0F}) == Side::Front);
    CHECK(victimSide(victim, quarter, {0.0F, 1.0F, 0.0F}) == Side::Right);
}

TEST_CASE("the reactions seen at runtime come out of the table", "[combat]") {
    // S1 (0x0a) from in front: 272; from the victim's left: 275.
    CHECK(react(anim_id::kAttackS1, 0x0a, Side::Front) == 272);
    CHECK(react(anim_id::kAttackS1, 0x0a, Side::Left) == 275);
    // X1 (0x09) from the victim's left: 274; from in front it comes from a side: 275.
    CHECK(react(anim_id::kAttackX1, 0x09, Side::Left) == 274);
    CHECK(react(anim_id::kAttackX1, 0x09, Side::Front) == 275);
    // SSS3 (0x26) is a combo id, a strength lighter on a fresh victim: 276; hurt, it keeps strength 2: 288.
    CHECK(react(anim_id::kAttackSSS3, 0x26, Side::Front) == 276);
    CHECK(react(anim_id::kAttackSSS3, 0x26, Side::Front, 0.0F, true) == 288);
    // The special 653 (0x26) from the left: 291.
    CHECK(react(653, 0x26, Side::Left) == 291);
    // SS2 (0x0b, a combo id at strength 0): 273 from in front.
    CHECK(react(anim_id::kAttackSS2, 0x0b, Side::Front) == 273);
}

TEST_CASE("height follows where the attacker stands, and a low hit is heavy", "[combat]") {
    // S1 from 0.5 m above stays high (at most 2); from 0.5 m below it is mid: 268.
    CHECK(react(anim_id::kAttackS1, 0x0a, Side::Front, 0.5F) == 272);
    CHECK(react(anim_id::kAttackS1, 0x0a, Side::Front, -0.5F) == 268);
    // From 1 m below it is low, so strength 2: 284.
    const ReactionInput low{.attackAnim = anim_id::kAttackS1,
                            .code = 0x0a,
                            .side = Side::Front,
                            .attackerAbove = -1.0F,
                            .victimFlag400 = false,
                            .victimHurt = false};
    const Reaction reaction = hitReaction(low);
    CHECK(reaction.animId == 284);
    CHECK(reaction.code.strength == 2);
    CHECK(reaction.code.height == 0);
}

TEST_CASE("dying takes the DIE set or 292; a block holds below strength 3", "[combat]") {
    ReactionInput input;
    input.attackAnim = anim_id::kAttackS1;
    input.code = 0x0a;
    CHECK(deathReaction(input, true) == 292 + kDeathOffset);
    CHECK(deathReaction(input, false) == kDeathFallback);
    CHECK(blockReaction(2, 2) == 608);
    CHECK(blockReaction(1, 0) == 614);
    CHECK(blockReaction(0, 3) == 613);
    CHECK(blockHolds(2, anim_id::kAttackS1));
    CHECK_FALSE(blockHolds(3, anim_id::kAttackS1));
    CHECK_FALSE(blockHolds(1, 30));
}
