// SPDX-License-Identifier: GPL-3.0-or-later
// A human's face (human/face.h): the talk state's moods and speaking half, the face clips that loop under the body's
// clips and cross-fade on a change, and a human whose line plays opening its mouth with the speaking clips. Over the
// synthetic character of support/human_fixtures.h with a face clip for each of the 24 face ids, each holding the jaw
// (pose bone 8) at its own angle, so a pose tells which face clip it came from.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_pose.h"
#include "characters/anim_set.h"
#include "human/face.h"
#include "human/human.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::FaceLayer;
using coney::human::FaceMood;
using coney::human::FaceState;
using coney::human::Human;
using coney::human::HumanInput;

namespace {

// The pad at rest, seen from a camera looking along +y.
const HumanInput kStill{.stickX = 0.0F, .stickY = 0.0F, .cameraForward = Vec3{0.0F, 1.0F, 0.0F}, .targets = {}};

// The jaw's stored x of face clip `id`: its own for each id.
std::int16_t jawOf(std::size_t id) {
    return static_cast<std::int16_t>(1000 + (id - coney::human::kFirstFaceAnim) * 300);
}

// The jaw's decoded x for face clip `id`.
float jawX(std::size_t id) { return static_cast<float>(jawOf(id)) / 32768.0F; }

// The locomotion clips and the 24 face clips: 1 s each (0.75 s of game time is 1 s of clip at the default rate 0.75).
std::vector<coney::test::LocomotionClip> clipsWithFaces() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::locomotionClips();
    for (std::size_t id = coney::human::kFirstFaceAnim; id < coney::human::kFirstFaceAnim + 24; ++id) {
        clips.push_back({.id = static_cast<std::uint32_t>(id), .duration = 1.0F, .jawX = jawOf(id)});
    }
    return clips;
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData(clipsWithFaces());
    coney::characters::AnimSet anims{data, nullptr};
};

// The identity rotations, as the reference a face's defaults start from.
const std::array<coney::anim::Quat, coney::anim::kPoseBones> kReference{};

} // namespace

TEST_CASE("the talk state keeps the mood and adds one while a line plays", "[human][face]") {
    // Expected: face.md#talk-state, confirmed (code)
    FaceState state;
    // Human_Init clears the record: angry (0) until a brain sets the mood.
    CHECK(state.value() == 0);
    CHECK(state.firstClip() == 698);
    state.setMood(FaceMood::Neutral);
    CHECK(state.value() == 2);
    CHECK_FALSE(state.talking());
    state.beginTalk();
    CHECK(state.value() == 3);
    CHECK(state.talking());
    CHECK(state.firstClip() == 707);
    CHECK(state.lastClip() == 709);
    // A second start changes nothing; a mood set while talking keeps the speaking half.
    state.beginTalk();
    CHECK(state.value() == 3);
    state.setMood(FaceMood::Excited);
    CHECK(state.value() == 7);
    CHECK(state.firstClip() == 719);
    state.endTalk();
    CHECK(state.value() == 6);
    state.endTalk();
    CHECK(state.value() == 6);
    state.setMood(FaceMood::Scared);
    CHECK(state.firstClip() == 710);
}

TEST_CASE("the face clips start only when shown, loop a random clip of the state, and pose only the jaw",
          "[human][face]") {
    // Expected: face.md#face-clips, confirmed (code)
    const TestCharacter character;
    FaceState state;
    state.setMood(FaceMood::Neutral);
    FaceLayer face;
    // Not shown (far from the camera): nothing starts, the defaults are the reference.
    face.update(state, character.anims, false);
    CHECK_FALSE(face.current().has_value());
    CHECK(face.defaults(kReference, character.anims)[8].x == Approx(0.0F));
    // Shown: the state's first clip (704) plays.
    face.update(state, character.anims, true);
    REQUIRE(face.current() == 704U);
    CHECK(face.playing() == 704U);
    const auto pose = face.defaults(kReference, character.anims);
    CHECK(pose[8].x == Approx(jawX(704)).margin(1e-3));
    CHECK(pose[9].x == Approx(0.0F)); // a bone the face clip leaves out keeps the reference
    // At the clip's end (1 s of clip at rate 0.75: 1.34 s) the loop draws its next clip from the state's three.
    std::vector<std::pair<int, int>> asked;
    const FaceLayer::Draw draw = [&asked](int low, int high) {
        asked.emplace_back(low, high);
        return high;
    };
    for (int i = 0; i < 41; ++i) {
        face.advance(1.0F / 30.0F, character.anims, draw);
    }
    REQUIRE(asked.size() == 1);
    CHECK(asked.front() == std::pair{704, 706});
    CHECK(face.playing() == 706U);
    CHECK(face.current() == 704U); // the state's first id stays the record's
}

TEST_CASE("into a speaking state the face changes at once; back to a resting one it fades over 1/30 s",
          "[human][face]") {
    // Expected: face.md#face-clips, confirmed (code)
    const TestCharacter character;
    FaceState state;
    state.setMood(FaceMood::Neutral);
    FaceLayer face;
    face.update(state, character.anims, true);
    // A line starts: the speaking clips (707-709) at once, no fade.
    state.beginTalk();
    face.update(state, character.anims, true);
    CHECK(face.current() == 707U);
    CHECK_FALSE(face.fading());
    CHECK(face.defaults(kReference, character.anims)[8].x == Approx(jawX(707)).margin(1e-3));
    // It ends: the resting clips under a fade of the speaking ones, gone after 1/30 s.
    state.endTalk();
    face.update(state, character.anims, true);
    CHECK(face.current() == 704U);
    REQUIRE(face.fading());
    // At the fade's start the outgoing (speaking) pose has all the weight.
    CHECK(face.defaults(kReference, character.anims)[8].x == Approx(jawX(707)).margin(1e-3));
    // One update in, the fade has run its length and the old pose weighs nothing; it is given back at the next
    // update, the first past its length (FaceBlendOut_Update's strict test).
    face.advance(1.0F / 30.0F, character.anims, {});
    CHECK(face.fading());
    CHECK(face.defaults(kReference, character.anims)[8].x == Approx(jawX(704)).margin(1e-3));
    face.advance(1.0F / 30.0F, character.anims, {});
    CHECK_FALSE(face.fading());
    CHECK(face.defaults(kReference, character.anims)[8].x == Approx(jawX(704)).margin(1e-3));
}

TEST_CASE("a face clip ends on the step whose rounded frame reaches its frame count, and restarts from its start",
          "[human][face]") {
    // Expected: formats/animation.md#clip-end, confirmed (code)
    const TestCharacter character;
    FaceState state;
    FaceLayer face;
    face.update(state, character.anims, true);
    int draws = 0;
    const FaceLayer::Draw draw = [&draws](int low, int /*high*/) {
        ++draws;
        return low;
    };
    // A 1 s clip has 30 frames. 1.3 s at rate 0.75 reaches 0.975 s, frame 29.25, which rounds to 29: it plays on.
    face.advance(1.3F, character.anims, draw);
    CHECK(draws == 0);
    // From the start again, 1.32 s reaches 0.99 s, frame 29.7, which rounds to 30: the clip has ended though its
    // time is short of its length, and the next one starts from its first frame, the step's time dropped.
    FaceLayer again;
    again.update(state, character.anims, true);
    again.advance(1.32F, character.anims, draw);
    CHECK(draws == 1);
    CHECK(again.playing() == 698U);
    CHECK(again.loopTime() == 0.0F);
    CHECK(face.loopTime() == Approx(0.975F));
}

TEST_CASE("a change into a resting state fades no longer than the new first clip takes to play", "[human][face]") {
    // Expected: face.md#face-clips, confirmed (code)
    // Angry's resting clips (698-700) last 0.02 s: 0.0267 s at rate 0.75, under the 1/30 s fade.
    std::vector<coney::test::LocomotionClip> clips = coney::test::locomotionClips();
    for (std::size_t id = coney::human::kFirstFaceAnim; id < coney::human::kFirstFaceAnim + 24; ++id) {
        const float seconds = id <= 700 ? 0.02F : 1.0F;
        clips.push_back({.id = static_cast<std::uint32_t>(id), .duration = seconds, .jawX = jawOf(id)});
    }
    const coney::characters::CharacterData data = coney::test::locomotionData(clips);
    const coney::characters::AnimSet anims{data, nullptr};
    FaceState state;
    state.beginTalk();
    FaceLayer face;
    face.update(state, anims, true);
    REQUIRE(face.current() == 701U);
    state.endTalk();
    face.update(state, anims, true);
    REQUIRE(face.fading());
    // 0.025 s into the fade: past half of 0.0267 s, so the outgoing pose weighs under a half.
    face.advance(0.025F, anims, {});
    const float jaw = face.defaults(kReference, anims)[8].x;
    CHECK(jaw < (jawX(701) + jawX(698)) * 0.5F);
}

TEST_CASE("a human whose line plays opens its mouth with the mood's speaking clips; a face not shown rests",
          "[human][face]") {
    // Expected: face.md#face-clips, confirmed (code)
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    human.setFaceMood(FaceMood::Neutral);
    human.step(kStill, mesh.get());
    // The idle leaves the jaw out: the face clip's jaw shows through.
    CHECK(human.pose().rotations[8].x == Approx(jawX(704)).margin(1e-3));
    // A line plays: the next update starts the speaking clips.
    human.setSpeaking(true);
    CHECK(human.faceState().value() == 3);
    human.step(kStill, mesh.get());
    CHECK(human.pose().rotations[8].x == Approx(jawX(707)).margin(1e-3));
    // The line ends: back to the resting face after the fade.
    human.setSpeaking(false);
    human.step(kStill, mesh.get());
    human.step(kStill, mesh.get());
    CHECK(human.pose().rotations[8].x == Approx(jawX(704)).margin(1e-3));
    // Not shown (far from the camera): the bind rotations, as the original samples the reference pose.
    coney::human::FaceRules hidden{.shown = [](const Human&) { return false; }, .random = {}};
    human.setFaceRules(&hidden);
    CHECK(human.pose().rotations[8].x == Approx(0.0F));
}

TEST_CASE("only the head and the face bones up to the eyelid take the face clip; the upper lip keeps the reference",
          "[human][face]") {
    // Expected: face.md#shown, confirmed (code)
    // The copy's two edges: neutral's resting clips turn the upper lip (15), scared's the head (6), excited's the bone
    // below the head (5), the others the eyelid (14), which blinks.
    std::vector<coney::test::LocomotionClip> clips = coney::test::locomotionClips();
    for (std::size_t id = coney::human::kFirstFaceAnim; id < coney::human::kFirstFaceAnim + 24; ++id) {
        std::uint8_t bone = 14;
        if (id >= 704 && id <= 706) {
            bone = 15;
        } else if (id >= 710 && id <= 712) {
            bone = 6;
        } else if (id >= 716 && id <= 718) {
            bone = 5;
        }
        clips.push_back({.id = static_cast<std::uint32_t>(id), .duration = 1.0F, .jawX = jawOf(id), .jawBone = bone});
    }
    const coney::characters::CharacterData data = coney::test::locomotionData(clips);
    const coney::characters::AnimSet anims{data, nullptr};
    FaceState state;
    FaceLayer face;
    face.update(state, anims, true);
    CHECK(face.defaults(kReference, anims)[14].x == Approx(jawX(698)).margin(1e-3));
    state.setMood(FaceMood::Neutral);
    face.update(state, anims, true);
    face.advance(0.1F, anims, {});
    REQUIRE_FALSE(face.fading());
    CHECK(face.playing() == 704U);
    CHECK(face.defaults(kReference, anims)[15].x == Approx(0.0F));
    // The bottom edge: the head (6) takes the face clip, the bone below it (5) keeps the reference.
    state.setMood(FaceMood::Scared);
    face.update(state, anims, true);
    face.advance(0.1F, anims, {});
    REQUIRE_FALSE(face.fading());
    CHECK(face.playing() == 710U);
    CHECK(face.defaults(kReference, anims)[6].x == Approx(jawX(710)).margin(1e-3));
    state.setMood(FaceMood::Excited);
    face.update(state, anims, true);
    face.advance(0.1F, anims, {});
    REQUIRE_FALSE(face.fading());
    CHECK(face.playing() == 716U);
    CHECK(face.defaults(kReference, anims)[5].x == Approx(0.0F));
}
