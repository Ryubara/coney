// SPDX-License-Identifier: GPL-3.0-or-later
// The tandem's rules on their own (combat/tandem.h, docs/research/combat-moves.md#tandem): who may start one
// (`Tandem_CanStart`), which set of six clips it plays, the damage its victim takes at the end of his intro, and when
// that damage defeats him (`Human_IsDamageDefeating`).
#include <array>
#include <bit>
#include <cstdint>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ranges.h"
#include "combat/reactions.h"
#include "combat/stick.h"
#include "combat/tandem.h"
#include "support/fixtures.h"

namespace combat = coney::combat;
using combat::Side;

TEST_CASE("a tandem starts only at a victim held from the rear, from his front, holding none of 0xc12200",
          "[combat][tandem]") {
    const combat::TandemCheck ok{.victimRearHeld = true, .attackerSide = Side::Front, .victimHeld = 0};
    CHECK(combat::tandemCanStart(ok));
    // Held from the front (state 0x10), or not held at all: no tandem.
    combat::TandemCheck front = ok;
    front.victimRearHeld = false;
    CHECK_FALSE(combat::tandemCanStart(front));
    // The attacker must stand within 45 degrees of the victim's front: his sides and rear refuse.
    for (const Side side : {Side::Right, Side::Left, Side::Rear}) {
        combat::TandemCheck off = ok;
        off.attackerSide = side;
        CHECK_FALSE(combat::tandemCanStart(off));
    }
    // Each bit of 0xc12200 on the victim's record refuses (already in a paired move, a tandem among them); others do
    // not.
    for (const std::uint32_t bit : {0x800000U, 0x400000U, 0x10000U, 0x2000U, 0x200U}) {
        combat::TandemCheck busy = ok;
        busy.victimHeld = bit;
        CHECK_FALSE(combat::tandemCanStart(busy));
    }
    combat::TandemCheck other = ok;
    other.victimHeld = 0x10U | 0x1U | 0x1000000U;
    CHECK(combat::tandemCanStart(other));
}

TEST_CASE("the attacker's side of the victim is the 45-degree front cone", "[combat][tandem]") {
    // The victim at the origin facing +y: an attacker 1 m ahead and 0.9 m to the side is within 45 degrees, 1.1 m to
    // the side is not.
    const coney::anim::Vec3 victim{0.0F, 0.0F, 0.0F};
    CHECK(combat::victimSide(victim, 0.0F, coney::anim::Vec3{0.9F, 1.0F, 0.0F}) == Side::Front);
    CHECK(combat::victimSide(victim, 0.0F, coney::anim::Vec3{1.1F, 1.0F, 0.0F}) != Side::Front);
}

TEST_CASE("the tandem's set follows the attacker's weapon, else one of three at random", "[combat][tandem]") {
    combat::CombatRandom random(7);
    // A baton (set 2) or a bat (set 3): 181-186; a knife (set 1): 187-192.
    CHECK(combat::tandemSet(2, random) == 181);
    CHECK(combat::tandemSet(3, random) == 181);
    CHECK(combat::tandemSet(1, random) == 187);
    // Empty-handed (or a thrown object's set): 163, 169 or 175, each drawn from the seeded sequence.
    std::set<int> seen;
    for (int i = 0; i < 60; ++i) {
        const int base = combat::tandemSet(i % 2 == 0 ? 0 : 5, random);
        CHECK((base == 163 || base == 169 || base == 175));
        seen.insert(base);
    }
    CHECK(seen.size() == 3);
    // The same seed gives the same sets.
    combat::CombatRandom a(11);
    combat::CombatRandom b(11);
    for (int i = 0; i < 10; ++i) {
        CHECK(combat::tandemSet(0, a) == combat::tandemSet(0, b));
    }
}

TEST_CASE("a tandem's six clips are the set's base id plus 0 to 5", "[combat][tandem]") {
    const combat::TandemClips clips = combat::tandemClips(169);
    CHECK(clips.grabberIntro == 169);
    CHECK(clips.victimIntro == 170);
    CHECK(clips.attackerIntro == 171);
    CHECK(clips.grabberEnd == 172);
    CHECK(clips.victimEnd == 173);
    CHECK(clips.attackerEnd == 174);
}

TEST_CASE("the victim's damage is the attacker's intro and end values", "[combat][tandem]") {
    // An Anim Range List of 193 records with the frame data's damage on the attackers' ids: 50 on each intro, 350 on
    // the unarmed ends, 500 on the bat's and 600 on the knife's (docs/research/combat-moves.md#frame-data).
    coney::test::Bytes bytes;
    bytes.u32(193);
    for (int id = 0; id < 193; ++id) {
        std::int16_t damage = 0;
        if (id == 165 || id == 171 || id == 177 || id == 183 || id == 189) {
            damage = 50;
        } else if (id == 168 || id == 174 || id == 180) {
            damage = 350;
        } else if (id == 186) {
            damage = 500;
        } else if (id == 192) {
            damage = 600;
        }
        bytes.u16(0)
            .u16(1000)
            .u32(std::bit_cast<std::uint32_t>(1.0F))
            .u16(0)
            .u16(static_cast<std::uint16_t>(damage))
            .u16(0)
            .u16(0);
    }
    const auto ranges = combat::AnimRangeList::parse(bytes.span());
    REQUIRE(ranges.has_value());
    CHECK(combat::tandemDamage(*ranges, 163) == 400);
    CHECK(combat::tandemDamage(*ranges, 169) == 400);
    CHECK(combat::tandemDamage(*ranges, 175) == 400);
    CHECK(combat::tandemDamage(*ranges, 181) == 550);
    CHECK(combat::tandemDamage(*ranges, 187) == 650);
}

TEST_CASE("damage defeats a human at or below it, tripled when he is cuffed, never a god", "[combat][tandem]") {
    const combat::DefeatTest base{.health = 400, .damage = 400};
    CHECK(combat::damageDefeats(base));
    combat::DefeatTest more = base;
    more.health = 401;
    CHECK_FALSE(combat::damageDefeats(more));
    // Cuffed, 400 counts as 1200.
    combat::DefeatTest cuffed = more;
    cuffed.health = 1200;
    cuffed.cuffed = true;
    CHECK(combat::damageDefeats(cuffed));
    cuffed.health = 1201;
    CHECK_FALSE(combat::damageDefeats(cuffed));
    // Never with human flag 0x10 or 0x20000000000, nor once knocked out.
    for (const auto set :
         std::array{&combat::DefeatTest::god, &combat::DefeatTest::demiGod, &combat::DefeatTest::knockedOut}) {
        combat::DefeatTest never = base;
        never.*set = true;
        CHECK_FALSE(combat::damageDefeats(never));
    }
}
