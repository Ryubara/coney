// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_task.h"

#include <array>
#include <memory>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::anim::AnimClip;
using coney::anim::AnimTaskStack;
using coney::anim::GaitBlendTask;
using coney::anim::GaitClip;
using coney::anim::LoopTask;
using coney::anim::Quat;
using coney::anim::Vec3;

namespace {

// A clip of `duration` seconds whose root velocity (section A) is `velocity` throughout and whose bone 2 turns about z
// from 0 to `endAngle` radians over its length: enough to tell two clips' poses apart in a blend.
AnimClip testClip(float duration, float velocity, float endAngle = 0.0F) {
    AnimClip clip;
    clip.duration = duration;
    clip.rootVelocity = {{.frame = 0, .value = Vec3{0.0F, velocity, 0.0F}}};
    clip.rootTranslation = {{.frame = 0, .value = Vec3{}}};
    const auto lastFrame = static_cast<std::uint32_t>(duration * coney::anim::kClipFrameRate);
    const float half = endAngle * 0.5F;
    clip.rotations = {{{.frame = 0, .value = Quat{}},
                       {.frame = lastFrame, .value = Quat{0.0F, 0.0F, std::sin(half), std::cos(half)}}}};
    clip.rotationBones = {2};
    return clip;
}

// The identity bind pose.
const std::array<Quat, coney::anim::kPoseBones> kBind{};

} // namespace

TEST_CASE("a fade weighs the outgoing pose by a raised cosine of its elapsed share", "[anim_task]") {
    CHECK(AnimTaskStack::outgoingWeight(0.0F, 0.15F) == Approx(1.0F));
    CHECK(AnimTaskStack::outgoingWeight(0.075F, 0.15F) == Approx(0.5F));
    CHECK(AnimTaskStack::outgoingWeight(0.15F, 0.15F) == 0.0F);
    CHECK(AnimTaskStack::outgoingWeight(0.0F, 0.0F) == 0.0F);
}

TEST_CASE("a looping task wraps with its overshoot and scales its root velocity by its rate", "[anim_task]") {
    const AnimClip clip = testClip(1.0F, 2.0F);
    LoopTask task(clip, 7, 0.75F, 0U);
    task.advance(1.0F); // 0.75 s of clip time
    CHECK(task.time() == Approx(0.75F));
    task.advance(0.5F); // 0.375 more: 1.125, wraps to 0.125
    CHECK(task.time() == Approx(0.125F));
    CHECK(task.sample(kBind).rootVelocity.y == Approx(1.5F));
    CHECK(task.animId() == 7U);
    // Without root velocity the pose carries none.
    LoopTask still(clip, 7, 1.0F, coney::anim::kTaskNoRootVelocity);
    CHECK(still.sample(kBind).rootVelocity.y == 0.0F);
    CHECK_FALSE(still.sample(kBind).hasRootVelocity);
}

TEST_CASE("a clip-then-next task hands over to its next task at the clip's end", "[anim_task]") {
    const AnimClip start = testClip(0.3F, 1.0F);
    const AnimClip loop = testClip(1.0F, 0.0F);
    AnimTaskStack stack;
    stack.change(std::make_unique<coney::anim::ClipThenNextTask>(start, 413, 1.0F, 0U,
                                                                 std::make_unique<LoopTask>(loop, 408, 1.0F, 0U)),
                 0.0F);
    REQUIRE(stack.top()->type() == coney::anim::AnimTaskType::ClipThenNext);
    stack.advance(0.2F);
    CHECK(stack.top()->animId() == 413U);
    stack.advance(0.2F); // 0.1 s past the clip's end
    REQUIRE(stack.top()->type() == coney::anim::AnimTaskType::Loop);
    CHECK(stack.top()->animId() == 408U);
    CHECK(stack.top()->time() == Approx(0.1F)); // the overshoot is kept
}

TEST_CASE("a change fades the new task in over the old ones, which go when the fade ends", "[anim_task]") {
    const AnimClip a = testClip(1.0F, 0.0F, 0.0F);
    const AnimClip b = testClip(1.0F, 0.0F, 0.0F);
    AnimTaskStack stack;
    stack.change(std::make_unique<LoopTask>(a, 1, 1.0F, 0U), 0.0F);
    CHECK(stack.taskCount() == 1);
    stack.change(std::make_unique<LoopTask>(b, 2, 1.0F, 0U), 0.15F);
    CHECK(stack.layerCount() == 2);
    CHECK(stack.taskCount() == 3); // new, old, fade
    CHECK(stack.top()->animId() == 2U);
    stack.advance(1.0F / 30.0F);
    stack.advance(1.0F / 30.0F);
    CHECK(stack.layerCount() == 2);
    for (int i = 0; i < 3; ++i) {
        stack.advance(1.0F / 30.0F);
    }
    CHECK(stack.layerCount() == 1); // 0.167 s > 0.15 s
    // A fade of no length replaces at once.
    stack.change(std::make_unique<LoopTask>(a, 3, 1.0F, 0U), 0.0F);
    CHECK(stack.layerCount() == 1);
    CHECK(stack.top()->animId() == 3U);
}

TEST_CASE("a fade's pose moves from the old task's to the new task's", "[anim_task]") {
    // The old clip holds bone 2 at 0 (its whole clip is at 0 for t = 0); the new one is a clip with root velocity 4.
    const AnimClip still = testClip(1.0F, 0.0F);
    const AnimClip moving = testClip(1.0F, 4.0F);
    AnimTaskStack stack;
    stack.change(std::make_unique<LoopTask>(still, 1, 1.0F, 0U), 0.0F);
    stack.change(std::make_unique<LoopTask>(moving, 2, 1.0F, 0U), 0.2F);
    CHECK(stack.sample(kBind).rootVelocity.y == Approx(0.0F)); // t = 0: all old
    stack.advance(0.1F);
    CHECK(stack.sample(kBind).rootVelocity.y == Approx(2.0F)); // halfway: (1 + cos(π/2)) / 2 = 0.5
}

TEST_CASE("a gait blend eases its value at 10 units a second and keeps its clips in step", "[anim_task]") {
    const AnimClip walk = testClip(1.0F, 0.0F, 0.0F);
    const AnimClip jog = testClip(0.5F, 0.0F, 1.0F);
    const AnimClip run = testClip(0.5F, 0.0F, 2.0F);
    const AnimClip sprint = testClip(0.4F, 0.0F, 3.0F);
    const std::array<GaitClip, 5> clips{GaitClip{&walk, 408}, GaitClip{&jog, 409}, GaitClip{&run, 410},
                                        GaitClip{&sprint, 411}, GaitClip{&sprint, 411}};
    GaitBlendTask blend(clips, 0.0F, 10.0F, 1.0F, coney::anim::kGaitBlendFlags);
    CHECK(blend.lowerIndex() == 0);
    // A target of 2 is reached in 0.2 s: a third of a unit per 1/30 s update.
    blend.setTarget(2.0F);
    blend.advance(1.0F / 30.0F);
    CHECK(blend.value() == Approx(1.0F / 3.0F));
    for (int i = 0; i < 5; ++i) {
        blend.advance(1.0F / 30.0F);
    }
    CHECK(blend.value() == Approx(2.0F));
    CHECK(blend.lowerIndex() == 2);
    CHECK(blend.animId() == 410U);
    // While the value falls the upper clip leads.
    blend.setTarget(1.0F);
    blend.advance(1.0F / 30.0F);
    CHECK(blend.upperLeads());
    // The rounding steps: 0.995 already counts as 1.
    blend.setValue(0.995F);
    CHECK(blend.lowerIndex() == 1);
    // Without the continuous flag a target is rounded.
    GaitBlendTask rounded(clips, 0.0F, 10.0F, 1.0F, 0U);
    rounded.setTarget(1.4F);
    CHECK(rounded.target() == 1.0F);
}

TEST_CASE("a gait blend's leading clip sets the shared normalised time", "[anim_task]") {
    const AnimClip walk = testClip(1.0F, 0.0F);
    const AnimClip jog = testClip(0.5F, 0.0F);
    const std::array<GaitClip, 5> clips{GaitClip{&walk, 408}, GaitClip{&jog, 409}, GaitClip{&jog, 409},
                                        GaitClip{&jog, 409}, GaitClip{&jog, 409}};
    GaitBlendTask blend(clips, 0.5F, 10.0F, 1.0F, coney::anim::kGaitBlendFlags);
    // The walk leads (value steady, fraction 0.5): 0.25 s is a quarter of its 1 s.
    blend.advance(0.25F);
    CHECK(blend.phase() == Approx(0.25F));
    CHECK(blend.time() == Approx(0.25F));
    // Above a fraction of 0.875 the jog leads: 0.25 s is half of its 0.5 s.
    blend.setValue(0.9F);
    blend.advance(0.25F);
    CHECK(blend.phase() == Approx(0.75F));
}

TEST_CASE("a pose's root motion is section A as sampled, and bone 0 read as a turn", "[anim_task]") {
    coney::anim::Pose pose;
    pose.rootVelocity = Vec3{0.0F, 1.5F, 0.0F};
    pose.hasRootVelocity = true;
    const float angle = 0.1F;
    pose.rotations[0] = Quat{0.0F, 0.0F, -std::sin(angle / 2.0F), std::cos(angle / 2.0F)};
    const coney::anim::RootMotion motion = coney::anim::rootMotionOf(pose);
    CHECK(motion.velocity.y == Approx(1.5F));
    CHECK(motion.turn == Approx(-angle));
}
