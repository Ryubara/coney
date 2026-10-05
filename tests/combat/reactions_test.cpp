// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/reactions.h"

#include <array>
#include <cstddef>
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

TEST_CASE("the combo ids are 13-15 and 17-20: SS2 (16) is never one", "[combat]") {
    for (const int animId : {13, 14, 15, 17, 18, 19, 20}) {
        CHECK(isComboAttack(animId));
    }
    CHECK_FALSE(isComboAttack(16));
    CHECK_FALSE(isComboAttack(12));
    CHECK_FALSE(isComboAttack(21));
    // SS2 with a medium code keeps its strength on a fresh victim; XS2 (14) with the same code drops one.
    CHECK(react(anim_id::kAttackSS2, 0x1a, Side::Front) == 280);
    CHECK(react(anim_id::kAttackXS2, 0x1a, Side::Front) == 272);
}

TEST_CASE("the being-hit table of the runtime, from all four sides, on the player (flag 0x400)", "[combat]") {
    // The civilian's codes on the player: front, the victim's left, behind, the victim's right
    // (docs/research/combat.md#being-hit-runtime).
    struct Row {
        int animId;
        int code;
        std::array<int, 4> expected; // front, left, rear, right
    };
    const std::array<Row, 8> rows{{
        {.animId = anim_id::kAttackS1, .code = 0x0a, .expected = {272, 275, 274, 273}},
        {.animId = anim_id::kAttackX1, .code = 0x09, .expected = {275, 274, 273, 272}},
        {.animId = anim_id::kAttackSS2, .code = 0x09, .expected = {275, 274, 273, 272}},
        {.animId = anim_id::kAttackSX2, .code = 0x1a, .expected = {280, 283, 282, 281}},
        {.animId = anim_id::kAttackXS2, .code = 0x1b, .expected = {281, 280, 283, 282}},
        {.animId = anim_id::kAttackSSS3, .code = 0x16, .expected = {276, 279, 278, 277}},
        {.animId = anim_id::kAttackXX2, .code = 0x26, .expected = {288, 291, 290, 289}},
        {.animId = 653, .code = 0x39, .expected = {303, 302, 301, 300}},
    }};
    const std::array<Side, 4> sides{Side::Front, Side::Left, Side::Rear, Side::Right};
    for (const Row& row : rows) {
        for (std::size_t i = 0; i < sides.size(); ++i) {
            ReactionInput input;
            input.attackAnim = row.animId;
            input.code = row.code;
            input.side = sides.at(i);
            input.victimFlag400 = true;
            CHECK(hitReaction(input).animId == row.expected.at(i));
        }
    }
    // SSX3 (0x2b) from in front and the left: 293, 292.
    ReactionInput ssx3{.attackAnim = anim_id::kAttackSSX3, .code = 0x2b, .side = Side::Front, .victimFlag400 = true};
    CHECK(hitReaction(ssx3).animId == 293);
    ssx3.side = Side::Left;
    CHECK(hitReaction(ssx3).animId == 292);
}

TEST_CASE("the attacker's 0x200000 adds strength; the victim's 0x200 takes it, 0x80 caps it", "[combat]") {
    ReactionInput input{.attackAnim = anim_id::kAttackS1, .code = 0x1a, .side = Side::Front};
    CHECK(hitReaction(input).code.strength == 1);
    // +1 only with the victim's 0x400 or a combo id.
    input.attackerFlag200000 = true;
    CHECK(hitReaction(input).code.strength == 1);
    input.victimFlag400 = true;
    CHECK(hitReaction(input).code.strength == 2);
    input.victimFlag80 = true;
    CHECK(hitReaction(input).code.strength == 1);
    ReactionInput lighter{.attackAnim = anim_id::kAttackS1, .code = 0x1a, .side = Side::Front, .victimFlag200 = true};
    CHECK(hitReaction(lighter).code.strength == 0);
}
