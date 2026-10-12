// SPDX-License-Identifier: GPL-3.0-or-later
// A human's clips in a revive (human/human_revive.cpp, docs/research/combat.md#revive): the reviver turns to the
// reach of 351 from the downed human's head and plays 351, 353 and 389 into his idle, holding 0x8000000; the revived is
// healed, gets up and plays 354 (holding 0x8000), 389 and his idle. Synthetic clips.
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "combat/revive.h"
#include "human/combatant.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/turn_and_slide.h"
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

TEST_CASE(
    "a reviver turns and slides to 351's reach from the downed human's head, then plays 351, 353 and 389 into his "
    "idle, holding 0x8000000",
    "[human][revive]") {
    const coney::test::FightCharacter character(reviveClips());
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human reviver(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &character.ranges);
    Human downed(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges);
    reviver.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    // The downed human 2 m to his right (+x): a quarter turn and a slide to 351's reach (1 m in the fixture's list,
    // straight ahead) from his head.
    downed.spawn(mesh.get(), Vec3{42.0F, 40.0F, 0.0F}, 0.0F);
    reviver.step(still(), mesh.get());
    downed.step(still(), mesh.get());
    const Vec3 head = downed.ledPoint();
    reviver.startRevive(downed);
    CHECK(reviver.reviveBeginPlaying());
    CHECK((reviver.animator().flags() & combat::kReviverHeld) != 0);

    // The steer lasts 351's time to its first event (the whole clip at its rate: it has none) + 0.1 s, at an even rate.
    const coney::anim::AnimClip* begin = reviver.animator().clip(combat::kReviveBeginAnim);
    REQUIRE(begin != nullptr);
    const float steerSeconds =
        coney::human::firstContactTime(*begin, reviver.animator().anims().rate(combat::kReviveBeginAnim)) + 0.1F;
    const int steerUpdates = static_cast<int>(std::lround(steerSeconds * 30.0F));
    std::vector<std::uint32_t> order{reviver.animator().animId()};
    bool heldThrough = true;
    int turning = 0;
    float lastHeading = reviver.heading();
    for (int update = 0; update < 60; ++update) {
        reviver.step(still(), mesh.get());
        const std::uint32_t id = reviver.animator().animId();
        if (id != order.back()) {
            order.push_back(id);
        }
        if (id == combat::kReviveBeginAnim || id == combat::kReviveEndAnim) {
            heldThrough = heldThrough && (reviver.animator().flags() & combat::kReviverHeld) != 0;
        }
        if (std::fabs(coney::human::wrapAngle(reviver.heading() - lastHeading)) > 1e-5F) {
            ++turning;
        }
        lastHeading = reviver.heading();
        if (update == steerUpdates + 2) {
            // In place: 1 m short of the head along the line to it, facing it (+x is heading -90 degrees).
            const float dx = head.x - reviver.position().x;
            const float dy = head.y - reviver.position().y;
            CHECK(std::hypot(dx, dy) == Approx(1.0F).margin(0.02F));
            CHECK(std::fabs(coney::human::wrapAngle(reviver.heading() + (std::numbers::pi_v<float> / 2.0F))) < 0.01F);
        }
    }
    CHECK(turning >= steerUpdates - 1);
    CHECK(turning <= steerUpdates + 1);
    REQUIRE(order.size() >= 4);
    CHECK(order[0] == combat::kReviveBeginAnim);
    CHECK(order[1] == combat::kReviveEndAnim);
    CHECK(order[2] == coney::human::kAnimNormalFromFight);
    CHECK(order[3] == 388);
    CHECK(heldThrough);
    CHECK_FALSE(reviver.reviveEndPlaying());
}

TEST_CASE("a downed human revived is up with full health, plays 354 holding 0x8000, then 389 and his idle",
          "[human][revive]") {
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
    CHECK((downed.animator().flags() & combat::kRevivedHeld) != 0);
    std::vector<std::uint32_t> order{downed.animator().animId()};
    for (int update = 0; update < 60; ++update) {
        downed.step(still(), mesh.get());
        if (const std::uint32_t id = downed.animator().animId(); id != order.back()) {
            order.push_back(id);
        }
    }
    REQUIRE(order.size() >= 3);
    CHECK(order[0] == combat::kRevivedAnim);
    CHECK(order[1] == coney::human::kAnimNormalFromFight);
    CHECK(order[2] == 388);
}
