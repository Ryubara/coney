// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "combat/anim_ids.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/target_human.h"
#include "support/fight_fixtures.h"

// The turn of an attack whose target stands beyond its far range (`Attack_TurnToTarget` 0x00276008, called from
// `Attack_Start` 0x002625a8, docs/research/combat-targets.md#attack-steer).

using coney::anim::Vec3;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace id = coney::combat::anim_id;

namespace {

constexpr float kPi = 3.14159265F;
constexpr float kDegrees = kPi / 180.0F;

// How far the player's heading is from facing the fight's target, radians.
float offFacing(Fight& fight) {
    const Vec3 to = coney::anim::subtract(fight.target().position(), fight.human().position());
    return std::fabs(coney::human::wrapAngle(coney::human::headingOf(to) - fight.human().heading()));
}

} // namespace

TEST_CASE("S1 at a kept target beyond its far range turns fully onto him over the time to its hit, not 8 degrees",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#attack-steer, confirmed (code). Attack_TurnToTarget 0x00276008 turns the attack's
    // direction onto the target, led by his velocity over the time to the clip's first event (the lead's speed held to
    // 8 m/s), over that time; nothing caps the angle.
    static const FightCharacter character;
    Fight fight(character, 1.5F);
    fight.run("", 5);
    REQUIRE(fight.human().fighter().lockTarget() == &fight.target());
    // The target, still standing, now 2.6 m off at 40 degrees to the right: beyond S1's far range (2.0 m here) and the
    // lock-on walk's 2.5 m, within the stance's 3 m, so square keeps him as its target.
    const Vec3 at = fight.human().position();
    fight.target().place(Vec3{at.x + 2.6F * std::sin(40.0F * kDegrees), at.y + 2.6F * std::cos(40.0F * kDegrees), at.z},
                         kPi);
    REQUIRE(offFacing(fight) > 35.0F * kDegrees);
    bool played = false;
    float closest = kPi;
    fight.run("0 tap square\n", 30, [&](std::uint64_t) {
        played = played || fight.human().animator().animId() == static_cast<std::uint32_t>(id::kAttackS1);
        closest = std::fmin(closest, offFacing(fight));
    });
    REQUIRE(played);
    CHECK(fight.human().fighter().target() == &fight.target());
    CHECK(closest < 2.0F * kDegrees);
}
