// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc the face clips (human/face.h, docs/research/face.md): every human of
// level99's cast has the 24 face clips (anim ids 698-721), the speaking ones turn the jaw further than the resting
// ones, and in level99 as `coney --play-level level99` plays it (support/disc_session.h) every human whose face shows
// plays the face clips of its talk state. Headless, on the fixed step with no clock. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts and angles only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/anim_set.h"
#include "fileio/wad.h"
#include "human/face.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

namespace anim = coney::anim;
namespace human = coney::human;

// The pose bone of the jaw (`bip_jaw`).
constexpr std::size_t kJaw = 8;
// The face bones' mask bits (pose bones 7-15).
constexpr std::uint64_t kFaceBoneMask = 0xff80ULL;
// The frame of play the faces are read at: the intro scene skipped and the cast free to move.
constexpr std::uint64_t kReadAt = 300;
constexpr std::string_view kPad = "100 tap cross\n";

// The widest turn of the jaw from the reference pose over a clip's keys, degrees; 0 when it has no channel.
float widestJawTurn(const anim::AnimClip& clip) {
    const auto* channel = clip.rotationChannel(kJaw);
    if (channel == nullptr) {
        return 0.0F;
    }
    const anim::Quat ref = anim::referenceRotations()[kJaw];
    float widest = 0.0F;
    for (const anim::RotationKey& key : *channel) {
        const float dot =
            std::abs(key.value.x * ref.x + key.value.y * ref.y + key.value.z * ref.z + key.value.w * ref.w);
        widest = std::max(widest, 2.0F * std::acos(std::min(1.0F, dot)) * 57.29578F);
    }
    return widest;
}

// What one human's anim set holds of the face clips: how many of the 24 resolve, and the jaw's widest turn in the
// resting and in the speaking ones.
struct FaceClips {
    std::size_t found = 0;
    float restingJaw = 0.0F;
    float speakingJaw = 0.0F;
};

// The face clips of `anims`.
FaceClips faceClipsOf(const coney::characters::AnimSet& anims) {
    FaceClips out;
    for (std::size_t state = 0; state < human::kFaceStates; ++state) {
        for (std::size_t i = 0; i < human::kFaceClipsPerState; ++i) {
            const anim::AnimClip* clip = anims.clip(human::kFirstFaceAnim + state * human::kFaceClipsPerState + i);
            if (clip == nullptr) {
                continue;
            }
            ++out.found;
            float& widest = (state & 1U) != 0 ? out.speakingJaw : out.restingJaw;
            widest = std::max(widest, widestJawTurn(*clip));
        }
    }
    return out;
}

} // namespace

TEST_CASE("the disc's level99: the cast has the face clips and every face shown plays its talk state's clips",
          "[disc][story][face]") {
    // Expected: face.md#shown, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kReadAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const anim::Vec3 eye = play->cameraEye();

    // Player 1 and every AI human still in the level.
    std::size_t humans = 0;
    std::size_t shown = 0;
    std::size_t far = 0;
    const auto check = [&](const human::Human& body, const coney::ai::Brain* brain) {
        ++humans;
        // Every one has the 24 face clips, and the speaking ones open the jaw further than the resting ones.
        const FaceClips clips = faceClipsOf(body.anims());
        CHECK(clips.found == human::kFaceStates * human::kFaceClipsPerState);
        CHECK(clips.speakingJaw > clips.restingJaw);
        std::printf("face clips: %zu, jaw up to %.1f resting, %.1f speaking\n", clips.found, clips.restingJaw,
                    clips.speakingJaw);
        // A face the rule shows (not down, a Warrior or within 17.5 m of the camera) plays its state's clips.
        const bool warrior = brain != nullptr && brain->type() == coney::ai::BrainType::Warrior;
        const bool near = anim::distance(body.position(), eye) < human::kFaceShownDistance;
        if (body.downOrDead() || !(warrior || near)) {
            ++far;
            return;
        }
        ++shown;
        CHECK(body.faceShown());
        CHECK(body.faceLayer().current() == body.faceState().firstClip());
        std::printf("face shown: state %d, clip %zu\n", body.faceState().value(),
                    body.faceLayer().playing().value_or(0));
    };
    check(play->player().human(), &play->fighters().playerBrain());
    for (const coney::ai::AiHuman& fighter : play->fighters().humans()) {
        if (!fighter.removed && fighter.human != nullptr) {
            check(*fighter.human, play->fighters().brainOf(*fighter.human));
        }
    }
    std::printf("humans %zu: %zu faces shown, %zu not\n", humans, shown, far);
    CHECK(shown >= 1);
    // How many of player 1's body clips (the ids below the face clips) carry face channels of their own.
    std::size_t body = 0;
    std::size_t withFace = 0;
    for (std::size_t id = 0; id < human::kFirstFaceAnim; ++id) {
        if (const anim::AnimClip* clip = play->player().human().anims().clip(id); clip != nullptr) {
            ++body;
            withFace += (clip->boneMask & kFaceBoneMask) != 0 ? 1 : 0;
        }
    }
    std::printf("player 1's body clips: %zu, with face channels %zu\n", body, withFace);
}
