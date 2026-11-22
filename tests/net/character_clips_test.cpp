// SPDX-License-Identifier: GPL-3.0-or-later
// The clips a client draws a human of one character with (docs/guides/client-world.md#clips): the character's clips
// by anim id and its locomotion's gait blends, rebuilt as the server's HumanAnimator builds them, on synthetic clips.
#include "net/character_clips.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_pose.h"
#include "animation/anim_task.h"
#include "characters/anim_set.h"
#include "characters/character_data.h"
#include "human/human_animator.h"
#include "net/client_world.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::characters::AnimSet;
using coney::characters::CharacterData;
using coney::net::CharacterClips;

namespace {

// The anim ids of a gait blend's five clips.
std::array<std::uint32_t, 5> idsOf(const std::array<coney::anim::GaitClip, 5>& blend) {
    std::array<std::uint32_t, 5> ids{};
    std::ranges::transform(blend, ids.begin(), [](const coney::anim::GaitClip& clip) { return clip.animId; });
    return ids;
}

} // namespace

// Expected: unverified, #1603 (Coney's own; the blends are HumanAnimator::gaitBlend()'s slots walk, jog, run, sprint
// and sprint, AnimSlots::player()'s 408 to 411, and the hidden style's walk 633, human_animator.h)
TEST_CASE("character clips: a clip by its anim id, and the gait blend of each move style its walk, jog, run or sprint "
          "is in",
          "[net][client_world]") {
    const CharacterData data = coney::test::locomotionData();
    const AnimSet anims(data, nullptr);
    const CharacterClips clips(anims);

    CHECK(clips.clip(408) == anims.clip(408));
    CHECK(clips.clip(408) != nullptr);
    CHECK(clips.clip(-1) == nullptr);
    CHECK(clips.clip(100000) == nullptr);
    CHECK(clips.clip(5) == nullptr); // no clip in the set

    constexpr std::array<std::uint32_t, 5> kNormal{408, 409, 410, 411, 411};
    for (const std::int32_t id : {408, 409, 410, 411}) {
        const auto blend = clips.gaitClips(id);
        REQUIRE(blend.has_value());
        CHECK(idsOf(*blend) == kNormal);
        for (const coney::anim::GaitClip& clip : *blend) {
            CHECK(clip.clip == anims.clip(clip.animId));
        }
    }
    // The hidden style's walk leads its own blend, whose jog, run and sprint are the normal style's.
    const auto hidden = clips.gaitClips(633);
    REQUIRE(hidden.has_value());
    CHECK(idsOf(*hidden) == std::array<std::uint32_t, 5>{633, 409, 410, 411, 411});
    // The idle, the sneak and a combat clip are in no blend.
    CHECK_FALSE(clips.gaitClips(388).has_value());
    CHECK_FALSE(clips.gaitClips(407).has_value());
    CHECK_FALSE(clips.gaitClips(-1).has_value());
}

// Expected: unverified, #1603 (Coney's own; a blend needs all five clips, GaitBlendTask's)
TEST_CASE("character clips: a set that lacks one of a blend's clips has no blend", "[net][client_world]") {
    std::vector<coney::test::LocomotionClip> without = coney::test::locomotionClips();
    std::erase_if(without, [](const coney::test::LocomotionClip& clip) { return clip.id == 411; });
    const CharacterData data = coney::test::locomotionData(without);
    const AnimSet anims(data, nullptr);
    const CharacterClips clips(anims);
    CHECK(clips.clip(409) != nullptr);
    CHECK_FALSE(clips.gaitClips(409).has_value());
}

// Expected: unverified, #1603 (Coney's own; the pose is GaitBlendTask::sample()'s, as the server's stack sampled it)
TEST_CASE("character clips: a human's jog-to-run layer is drawn as the server's gait blend samples it",
          "[net][client_world]") {
    const CharacterData data = coney::test::locomotionData();
    const AnimSet anims(data, nullptr);
    const CharacterClips clips(anims);
    const auto blendClips = clips.gaitClips(409);
    REQUIRE(blendClips.has_value());

    // The server's blend between the jog and the run, a third of the way through their shared phase.
    const coney::anim::GaitBlendTask blend(*blendClips, 1.4F, 0.0F, 1.0F, coney::anim::kGaitBlendFlags, 0.3F);
    coney::net::HumanState human;
    human.layers[0] = coney::net::HumanAnimLayer{
        .clip = static_cast<std::int32_t>(blend.animId()), .playhead = 0.3F, .gait = 1.4F, .fade = 1.0F};

    const auto bind = coney::test::identityBind();
    const coney::anim::Pose expected = blend.sample(bind);
    const coney::anim::Pose got = coney::net::humanPose(human, clips, bind);
    for (std::size_t bone = 0; bone < coney::anim::kPoseBones; ++bone) {
        CHECK(got.rotations.at(bone).x == Approx(expected.rotations.at(bone).x).margin(1e-5));
        CHECK(got.rotations.at(bone).y == Approx(expected.rotations.at(bone).y).margin(1e-5));
        CHECK(got.rotations.at(bone).z == Approx(expected.rotations.at(bone).z).margin(1e-5));
        CHECK(got.rotations.at(bone).w == Approx(expected.rotations.at(bone).w).margin(1e-5));
    }
}
