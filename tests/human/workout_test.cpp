// SPDX-License-Identifier: GPL-3.0-or-later
// One workout update's rules (docs/research/workouts.md#engine): the pumps, the decay, the loop blend's target and
// rate, and the end, with the stations' own CfgWorkoutParams tables; the blend is a gait blend over synthetic clips.
#include "human/workout.h"

#include <array>
#include <cstddef>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "animation/anim_task.h"

using Catch::Approx;
using coney::human::WorkoutEffort;
using coney::human::WorkoutParams;
using coney::human::WorkoutStep;

namespace {

// The chin-ups' and push-ups' tables (workout.Setup, docs/research/workouts.md#stations); the factor 1.8 read as a
// flag.
const WorkoutParams kChinUps{
    .gain = {0.04F, 0.04F, 0.03F}, .decay = {0.03F, 0.03F, 0.03F}, .hold = {3, 2, 2}, .factor = 1.0F};
const WorkoutParams kPushUps{
    .gain = {0.02F, 0.04F, 0.04F}, .decay = {0.03F, 0.03F, 0.03F}, .hold = {3, 2, 3}, .factor = 1.0F};

// A clip of `duration` seconds with nothing in it.
coney::anim::AnimClip clipOf(float duration) {
    coney::anim::AnimClip clip;
    clip.duration = duration;
    return clip;
}

// A workout's loop: the slow, mid and fast clips in a gait blend at 16 units a second, its target kept as given, and
// the effort rules driving it one 1/30 s update at a time.
struct Loop {
    coney::anim::AnimClip slow = clipOf(2.0F);
    coney::anim::AnimClip mid = clipOf(1.5F);
    coney::anim::AnimClip fast = clipOf(0.933F);
    coney::anim::GaitBlendTask blend{
        std::array<coney::anim::GaitClip, 5>{coney::anim::GaitClip{&slow, 693}, coney::anim::GaitClip{&mid, 694},
                                             coney::anim::GaitClip{&fast, 695}, coney::anim::GaitClip{&fast, 695},
                                             coney::anim::GaitClip{&fast, 695}},
        0.0F, 16.0F, 1.0F, coney::anim::kTaskContinuous | coney::anim::kTaskNoRootVelocity};
    WorkoutEffort effort;
    WorkoutStep last;

    // One update: the rules on the blend as it stands, then the blend advanced at the rate and target they set.
    void update(const WorkoutParams& params, bool pumped, bool quit = false) {
        last = effort.padUpdate(params, blend.value(), blend.lowerTime(), blend.lowerClip().duration, pumped, quit);
        blend.setTarget(last.target);
        blend.setRate(last.rate);
        blend.advance(1.0F / 30.0F);
    }
};

} // namespace

TEST_CASE("the blend's level is its value rounded, and the timing factor is 1 with the scripts' flag", "[workout]") {
    CHECK(WorkoutEffort::levelOf(0.0F) == 0);
    CHECK(WorkoutEffort::levelOf(0.49F) == 0);
    CHECK(WorkoutEffort::levelOf(0.5F) == 1);
    CHECK(WorkoutEffort::levelOf(2.0F) == 2);
    CHECK(WorkoutEffort::timingFactor(kChinUps, 0.1F, 2.0F) == 1.0F);
    // A factor other than 1 applies over the lower clip's first half only.
    WorkoutParams halved = kChinUps;
    halved.factor = 0.5F;
    CHECK(WorkoutEffort::timingFactor(halved, 0.5F, 2.0F) == Approx(0.25F));
    CHECK(WorkoutEffort::timingFactor(halved, 1.5F, 2.0F) == 1.0F);
}

TEST_CASE("a cross press adds the level's gain on as many updates as its hold, capped at 1", "[workout]") {
    WorkoutEffort effort;
    // The start's hold counter of 6 caps the script's 1.5 to 1 on the first loop update.
    effort.setEffort(1.5F);
    static_cast<void>(effort.padUpdate(kChinUps, 2.0F, 0.0F, 1.0F, false, false));
    CHECK(effort.effort() == 1.0F);
    // The counter runs out (4 more updates add), then 0.03 an update comes off.
    for (int i = 0; i < 5; ++i) {
        static_cast<void>(effort.padUpdate(kChinUps, 2.0F, 0.0F, 1.0F, false, false));
    }
    CHECK(effort.hold() == 0);
    CHECK(effort.effort() == Approx(0.97F));
    // A press at level 2 (hold 2): the gain 0.03 on the press and on the next update, then the decay again.
    effort.setEffort(0.5F);
    static_cast<void>(effort.padUpdate(kChinUps, 2.0F, 0.0F, 1.0F, true, false));
    CHECK(effort.effort() == Approx(0.53F));
    static_cast<void>(effort.padUpdate(kChinUps, 2.0F, 0.0F, 1.0F, false, false));
    CHECK(effort.effort() == Approx(0.56F));
    static_cast<void>(effort.padUpdate(kChinUps, 2.0F, 0.0F, 1.0F, false, false));
    CHECK(effort.effort() == Approx(0.53F));
}

TEST_CASE("below a third of full effort the decay is cut to 0.3 and the slow clip slows down", "[workout]") {
    WorkoutEffort effort;
    // The start's hold of 6 runs out first.
    for (int i = 0; i < 6; ++i) {
        static_cast<void>(effort.padUpdate(kPushUps, 0.0F, 0.0F, 2.0F, false, false));
    }
    REQUIRE(effort.hold() == 0);
    effort.setEffort(0.3F);
    const float before = effort.effort();
    const WorkoutStep step = effort.padUpdate(kPushUps, 0.0F, 0.0F, 2.0F, false, false);
    CHECK(effort.effort() == Approx(before - 0.009F));
    CHECK(step.target == 0.0F);
    CHECK(step.rate == Approx((3.0F * effort.effort()) + 0.06F));
    CHECK_FALSE(step.end);
    // Above a third the target is 3 × effort − 1 at rate 1.
    effort.setEffort(0.8F);
    const WorkoutStep up = effort.padUpdate(kPushUps, 1.0F, 0.0F, 1.5F, false, false);
    CHECK(up.target == Approx((3.0F * effort.effort()) - 1.0F));
    CHECK(up.rate == 1.0F);
}

TEST_CASE("left alone, a workout runs down and ends about 63 updates into its loop, with no repetition", "[workout]") {
    // The push-ups at runtime: 0.03 an update to 1/3, then 0.009, the rate falling, the end 63 updates (2.1 s) after
    // the loop starts, no repetition (docs/research/workouts.md#repetitions).
    Loop loop;
    loop.effort.setEffort(1.5F);
    int updates = 0;
    while (!loop.last.end && updates < 300) {
        loop.update(kPushUps, false);
        ++updates;
    }
    CHECK(loop.last.end);
    CHECK(updates >= 58);
    CHECK(updates <= 68);
    CHECK(loop.blend.wraps() == 0);
}

TEST_CASE("pumping every 4 updates keeps the effort near 1 and the fast clip looping", "[workout]") {
    Loop loop;
    loop.effort.setEffort(1.5F);
    for (int i = 0; i < 150; ++i) {
        loop.update(kChinUps, i % 4 == 0);
        CHECK_FALSE(loop.last.end);
    }
    CHECK(loop.effort.effort() >= 0.94F);
    CHECK(loop.blend.value() >= 1.8F);
    // The blend holds between the mid (1.5 s) and fast (0.933 s) clips, the mid one leading below a fraction of
    // 0.875, as at runtime (chin-ups pumped every 4 updates: 37 updates a repetition at v 1.8-2.0): four wraps in 5 s.
    CHECK(loop.blend.wraps() == 4);
    // Triangle ends it.
    loop.update(kChinUps, false, true);
    CHECK(loop.last.end);
}

TEST_CASE("an AI's effort is what the script wrote: no pumps, no decay, no end of its own", "[workout]") {
    WorkoutEffort effort;
    effort.setEffort(2.5F);
    WorkoutStep step = effort.aiUpdate(kChinUps, 1.0F);
    CHECK(step.target == Approx(1.5F));
    CHECK(step.rate == 1.0F);
    effort.setEffort(0.5F);
    step = effort.aiUpdate(kChinUps, 0.0F);
    CHECK(step.target == 0.0F);
    CHECK(step.rate == Approx(0.5F + 0.09F));
    CHECK_FALSE(step.end);
    CHECK(effort.effort() == 0.5F);
}
