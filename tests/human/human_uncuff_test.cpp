// SPDX-License-Identifier: GPL-3.0-or-later
// The freer's place in an uncuff (human/human.cpp, docs/research/crimes.md#uncuff-pose): `Uncuff_Start` steers him with
// `Attack_SteerToTarget` for clip 325 over 0.1 s, turned so that the cuffed human lies along 325's direction and slid
// to 325's reach from him, and leaves the cuffed human where he stands. Synthetic clips and range list.
#include <cmath>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "human/fighter_clips.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/turn_and_slide.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Human;
using coney::human::HumanInput;
namespace clips = coney::human::clips;

namespace {

// The fight clips with the uncuff's: the freer's 325 (0.8 s) and loop 329, the cuffed human's 320, 326 and 330, all
// still.
std::vector<coney::test::LocomotionClip> uncuffClips() {
    std::vector<coney::test::LocomotionClip> all = coney::test::fightClips();
    all.push_back(coney::test::LocomotionClip{.id = clips::kArrestedIdle, .duration = 1.0F});
    all.push_back(coney::test::LocomotionClip{.id = clips::kReleaseIntro, .duration = 0.8F});
    all.push_back(coney::test::LocomotionClip{.id = clips::kReleaseIntroReact, .duration = 0.8F});
    all.push_back(coney::test::LocomotionClip{.id = clips::kReleaseLoop, .duration = 1.0F});
    all.push_back(coney::test::LocomotionClip{.id = clips::kReleaseLoopReact, .duration = 1.0F});
    return all;
}

// The stick at rest, the camera looking along +y.
HumanInput still() {
    return HumanInput{.stickX = 0.0F, .stickY = 0.0F, .cameraForward = coney::test::kAlongY, .targets = {}};
}

} // namespace

TEST_CASE("an uncuff steers the freer to 325's reach from the cuffed human over 0.1 s and leaves the cuffed one be",
          "[human][uncuff]") {
    const coney::test::FightCharacter character(uncuffClips());
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human freer(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                &character.ranges);
    Human cuffed(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges);
    // The freer facing +y; the cuffed human 2 m to his right (+x), facing him (heading 90 degrees faces -x), so the
    // freer is in front of him and 325's reach (1 m in the fixture's list, straight ahead) is not shortened.
    constexpr float kQuarter = std::numbers::pi_v<float> / 2.0F;
    freer.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    cuffed.spawn(mesh.get(), Vec3{42.0F, 40.0F, 0.0F}, 90.0F);
    freer.step(still(), mesh.get());
    cuffed.step(still(), mesh.get());
    const Vec3 cuffedAt = cuffed.position();
    const float cuffedHeading = cuffed.heading();

    freer.startUncuff(cuffed, 1.0F);
    cuffed.playUncuffReact(freer);
    REQUIRE(freer.animator().animId() == clips::kReleaseIntro);
    // Three updates (0.1 s): turned to face him (+x is heading -90 degrees) and slid to 1 m from him.
    for (int update = 0; update < 3; ++update) {
        freer.step(still(), mesh.get());
        cuffed.step(still(), mesh.get());
    }
    CHECK(std::hypot(cuffedAt.x - freer.position().x, cuffedAt.y - freer.position().y) == Approx(1.0F).margin(0.01F));
    CHECK(std::fabs(coney::human::wrapAngle(freer.heading() + kQuarter)) < 0.01F);
    // Then nothing more turns or moves him (the synthetic 325 has no root motion), and the cuffed human never moves.
    const Vec3 steered = freer.position();
    const float steeredHeading = freer.heading();
    for (int update = 0; update < 30; ++update) {
        freer.step(still(), mesh.get());
        cuffed.step(still(), mesh.get());
    }
    CHECK(std::hypot(freer.position().x - steered.x, freer.position().y - steered.y) < 0.001F);
    CHECK(std::fabs(coney::human::wrapAngle(freer.heading() - steeredHeading)) < 0.001F);
    CHECK(std::hypot(cuffed.position().x - cuffedAt.x, cuffed.position().y - cuffedAt.y) < 0.001F);
    CHECK(std::fabs(coney::human::wrapAngle(cuffed.heading() - cuffedHeading)) < 0.001F);
    // Linked until the end (Uncuff_BeginMash's shared body group and push weight 1e9; MiniGame_Abort undoes the
    // freer's).
    CHECK(freer.linkedWith(cuffed));
    CHECK(cuffed.linkedWith(freer));
    CHECK(freer.pushWeight() == Approx(1e9F));
    freer.endUncuff(false);
    CHECK_FALSE(freer.linkedWith(cuffed));
    CHECK(freer.pushWeight() == Approx(1.0F));
}

TEST_CASE("an uncuff from behind the cuffed human stands 0.1 m nearer, as 325's reach is shortened from the rear",
          "[human][uncuff]") {
    const coney::test::FightCharacter character(uncuffClips());
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human freer(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                &character.ranges);
    Human cuffed(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges);
    // The cuffed human 2 m to the freer's right, facing away from him (heading -90 degrees faces +x).
    constexpr float kQuarter = std::numbers::pi_v<float> / 2.0F;
    freer.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    cuffed.spawn(mesh.get(), Vec3{42.0F, 40.0F, 0.0F}, -90.0F);
    freer.step(still(), mesh.get());
    cuffed.step(still(), mesh.get());
    const Vec3 cuffedAt = cuffed.position();
    freer.startUncuff(cuffed, 1.0F);
    for (int update = 0; update < 3; ++update) {
        freer.step(still(), mesh.get());
        cuffed.step(still(), mesh.get());
    }
    CHECK(std::hypot(cuffedAt.x - freer.position().x, cuffedAt.y - freer.position().y) == Approx(0.9F).margin(0.01F));
    CHECK(std::fabs(coney::human::wrapAngle(freer.heading() + kQuarter)) < 0.01F);
}

TEST_CASE("an uncuff from behind a big cuffed human stands 0.07 m farther, as the rear cut is skipped for him",
          "[human][uncuff]") {
    const coney::test::FightCharacter character(uncuffClips());
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human freer(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                &character.ranges);
    // Scaled 1.2, above the big-target scale 1.1: Attack_SteerToTarget adds 0.07 m and then skips the side test
    // (0x002762f8-0x00276344), so the reach is 1.07 m, not 0.97 m, though the freer comes from behind him.
    Human cuffed(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.2F,
                 &character.ranges);
    constexpr float kQuarter = std::numbers::pi_v<float> / 2.0F;
    freer.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    cuffed.spawn(mesh.get(), Vec3{42.0F, 40.0F, 0.0F}, -90.0F);
    freer.step(still(), mesh.get());
    cuffed.step(still(), mesh.get());
    const Vec3 cuffedAt = cuffed.position();
    freer.startUncuff(cuffed, 1.0F);
    for (int update = 0; update < 3; ++update) {
        freer.step(still(), mesh.get());
        cuffed.step(still(), mesh.get());
    }
    CHECK(std::hypot(cuffedAt.x - freer.position().x, cuffedAt.y - freer.position().y) == Approx(1.07F).margin(0.01F));
    CHECK(std::fabs(coney::human::wrapAngle(freer.heading() + kQuarter)) < 0.01F);
}
