// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/being_hit.h"

#include <set>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "combat/anim_ids.h"
#include "combat/reactions.h"

// The victim's side of a hit as pure rules: the attacker's warning events, the hit armour, the health floor, the block
// and the mash (docs/research/combat.md#block, docs/research/combat.md#damage).

using namespace coney::combat;

TEST_CASE("an attacker's clip warns by its events 0x24 (duck) and 0x26 (early block)", "[combat]") {
    CHECK(warningOf(0x24) == AttackWarning::Duck);
    CHECK(warningOf(0x26) == AttackWarning::EarlyBlock);
    CHECK_FALSE(warningOf(0x25).has_value());
    // A clip with a duck event at frame 3 (0.1 s): found in the update that passes it, once.
    coney::anim::AnimClip clip;
    clip.duration = 0.6F;
    clip.events.push_back(coney::anim::ClipEvent{.frame = 3, .type = 0x24, .word = 0, .position = {}, .rotation = {}});
    CHECK_FALSE(warningBetween(clip, -1.0F, 0.0F).has_value());
    CHECK_FALSE(warningBetween(clip, 0.0F, 0.0667F).has_value());
    CHECK(warningBetween(clip, 0.0667F, 0.1F) == AttackWarning::Duck);
    CHECK_FALSE(warningBetween(clip, 0.1F, 0.1333F).has_value());
}

TEST_CASE("hit armour holds in the wind-up and the chain window only", "[combat]") {
    CHECK(hitArmourHolds(0x1, anim_id::kAttackS1, false));
    CHECK(hitArmourHolds(0x2, anim_id::kAttackS1, false));
    CHECK_FALSE(hitArmourHolds(0x4, anim_id::kAttackS1, false));
    CHECK_FALSE(hitArmourHolds(0x40000, anim_id::kAttackS1, false));
    CHECK_FALSE(hitArmourHolds(0, anim_id::kAttackS1, false));
    // An attacker that ignores armour, or one playing 617-620, always gets its reaction.
    CHECK_FALSE(hitArmourHolds(0x1, anim_id::kAttackS1, true));
    CHECK_FALSE(hitArmourHolds(0x2, 618, false));
}

TEST_CASE("one hit cannot take a player below a quarter of its health", "[combat]") {
    // 900 health: the floor is 225.
    CHECK(flooredDamage(900, 900, 50, 0.25F) == 50);
    CHECK(flooredDamage(300, 900, 200, 0.25F) == 75);
    // At or below the floor the hit takes its whole damage.
    CHECK(flooredDamage(225, 900, 50, 0.25F) == 50);
    CHECK(flooredDamage(100, 900, 200, 0.25F) == 200);
}

TEST_CASE("a block holds below strength 3 with the block table's reaction", "[combat]") {
    // S1 (0x0a) from in front: 608; SSS3 (0x16) on the player from its left: 615; a strength-3 hit breaks it.
    const BlockResult s1 = blockHit(ReactionInput{.attackAnim = anim_id::kAttackS1, .code = 0x0a, .side = Side::Front});
    CHECK(s1.holds);
    CHECK(s1.reaction == 608);
    const BlockResult sss3 = blockHit(
        ReactionInput{.attackAnim = anim_id::kAttackSSS3, .code = 0x16, .side = Side::Left, .victimFlag400 = true});
    CHECK(sss3.holds);
    CHECK(sss3.reaction == 615);
    CHECK_FALSE(blockHit(ReactionInput{.attackAnim = 653, .code = 0x39, .side = Side::Front}).holds);
}

TEST_CASE("a mash cuts the ground time by a third, a half or all of it", "[combat]") {
    CombatRandom random(7);
    std::set<int> cuts;
    for (int i = 0; i < 60; ++i) {
        cuts.insert(mashCut(2750, random));
    }
    CHECK(cuts == std::set<int>{916, 1375, 2750});
}
