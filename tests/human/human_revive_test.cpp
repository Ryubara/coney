// SPDX-License-Identifier: GPL-3.0-or-later
// A human's clips in a revive (human/human_revive.cpp, docs/research/combat.md#revive): the reviver turns to the
// downed human and plays 351, 353 and 389 into his idle, holding 0x8000000; the revived is healed, gets up and plays
// 354 into his idle. Synthetic clips.
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/revive.h"
#include "human/combatant.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Human;
using coney::human::HumanInput;
namespace combat = coney::combat;

namespace {

// The fight clips with the revive's three (351, 353 of 0.5 s, 354 of 0.6 s), all still.
std::vector<coney::test::LocomotionClip> reviveClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    clips.push_back(coney::test::LocomotionClip{.id = combat::kReviveBeginAnim, .duration = 0.5F});
    clips.push_back(coney::test::LocomotionClip{.id = combat::kReviveEndAnim, .duration = 0.5F});
    clips.push_back(coney::test::LocomotionClip{.id = combat::kRevivedAnim, .duration = 0.6F});
    return clips;
}

// The stick at rest, the camera looking along +y.
HumanInput still() {
    return HumanInput{.stickX = 0.0F, .stickY = 0.0F, .cameraForward = coney::test::kAlongY, .targets = {}};
}

} // namespace

TEST_CASE("a reviver turns to the downed human and plays 351, 353 and 389 into his idle, holding 0x8000000",
          "[human][revive]") {
    const coney::test::FightCharacter character(reviveClips());
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human reviver(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &character.ranges);
    reviver.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    reviver.step(still(), mesh.get());
    // The downed human 1 m to his right (+x): a quarter turn.
    reviver.startRevive(Vec3{41.0F, 40.0F, 0.0F});
    CHECK(reviver.reviveBeginPlaying());
    CHECK((reviver.animator().flags() & combat::kReviverHeld) != 0);

    std::vector<std::uint32_t> order{reviver.animator().animId()};
    bool heldThrough = true;
    for (int update = 0; update < 60; ++update) {
        reviver.step(still(), mesh.get());
        const std::uint32_t id = reviver.animator().animId();
        if (id != order.back()) {
            order.push_back(id);
        }
        if (id == combat::kReviveBeginAnim || id == combat::kReviveEndAnim) {
            heldThrough = heldThrough && (reviver.animator().flags() & combat::kReviverHeld) != 0;
        }
        if (update == 5) {
            // Turned within 0.1 s (3 updates): facing +x is heading -90 degrees.
            CHECK(std::fabs(coney::human::wrapAngle(reviver.heading() + (std::numbers::pi_v<float> / 2.0F))) < 0.01F);
        }
    }
    REQUIRE(order.size() >= 4);
    CHECK(order[0] == combat::kReviveBeginAnim);
    CHECK(order[1] == combat::kReviveEndAnim);
    CHECK(order[2] == coney::human::kAnimNormalFromFight);
    CHECK(order[3] == 388);
    CHECK(heldThrough);
    CHECK_FALSE(reviver.reviveEndPlaying());
    // He stayed where he stood.
    CHECK(reviver.position().x == Approx(40.0F).margin(0.01F));
    CHECK(reviver.position().y == Approx(40.0F).margin(0.01F));
}

TEST_CASE("a downed human revived is up with full health, plays 354 and then his idle", "[human][revive]") {
    const coney::test::FightCharacter character(reviveClips());
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human reviver(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &character.ranges);
    Human downed(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges);
    reviver.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    downed.spawn(mesh.get(), Vec3{41.0F, 40.0F, 0.0F}, 0.0F);
    // Out of health: knocked out, on the ground.
    downed.fighter().health().set(0);
    downed.step(still(), mesh.get());
    REQUIRE(downed.state() == coney::human::TargetState::Grounded);

    downed.playRevived(reviver);
    CHECK(downed.fighter().health().value() == downed.fighter().health().maximum());
    CHECK(downed.state() == coney::human::TargetState::Standing);
    CHECK(downed.animator().animId() == combat::kRevivedAnim);
    bool idle = false;
    for (int update = 0; update < 40 && !idle; ++update) {
        downed.step(still(), mesh.get());
        idle = downed.animator().animId() == 388;
    }
    CHECK(idle);
}
