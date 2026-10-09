// SPDX-License-Identifier: GPL-3.0-or-later
// The clip events a human's sounds read (docs/research/sound.md#anim-sounds): kept as their frames pass, the gait
// blend's leading clip's events, and none from a task whose events are muted (task flag 0x10). Synthetic clips only.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_task.h"

using coney::anim::AnimClip;
using coney::anim::AnimTaskStack;
using coney::anim::ClipEvent;
using coney::anim::GaitBlendTask;
using coney::anim::GaitClip;
using coney::anim::LoopTask;
namespace anim = coney::anim;

namespace {

constexpr float kStep = 1.0F / 30.0F;
constexpr std::uint16_t kAnimSound = 11;

// A still clip of `frames` frames with an animation sound event `sound` at each of `frames`.
AnimClip soundClip(float length, const std::vector<std::uint16_t>& frames, std::uint32_t sound) {
    AnimClip clip;
    clip.duration = length / anim::kClipFrameRate;
    for (const std::uint16_t frame : frames) {
        clip.events.push_back(ClipEvent{
            .frame = frame, .type = kAnimSound, .value = 0, .argument = sound, .position = {}, .rotation = {}});
    }
    return clip;
}

// The animation sound ids of the events kept over `updates` updates.
std::vector<std::uint32_t> soundsOver(AnimTaskStack& stack, int updates) {
    std::vector<std::uint32_t> sounds;
    for (int k = 0; k < updates; ++k) {
        stack.advance(kStep);
        for (const ClipEvent& event : stack.takeEvents()) {
            sounds.push_back(event.argument);
        }
    }
    return sounds;
}

} // namespace

TEST_CASE("a looping clip's sound events are kept once a loop as their frames pass", "[anim_task][sound]") {
    const AnimClip walk = soundClip(20.0F, {5, 15}, 1);
    AnimTaskStack stack;
    stack.keepEvents(true);
    stack.change(std::make_unique<LoopTask>(walk, 1, 1.0F, 0U), 0.0F);
    // 40 updates: two loops, four steps.
    CHECK(soundsOver(stack, 40) == std::vector<std::uint32_t>{1, 1, 1, 1});
}

TEST_CASE("events are not kept unless asked, and a muted task fires none", "[anim_task][sound]") {
    const AnimClip walk = soundClip(20.0F, {5, 15}, 1);
    AnimTaskStack quiet;
    quiet.change(std::make_unique<LoopTask>(walk, 1, 1.0F, 0U), 0.0F);
    CHECK(soundsOver(quiet, 40).empty());

    AnimTaskStack muted;
    muted.keepEvents(true);
    muted.change(std::make_unique<LoopTask>(walk, 1, 1.0F, anim::kTaskEventsMuted), 0.0F);
    CHECK(soundsOver(muted, 40).empty());
}

TEST_CASE("a gait blend keeps the leading clip's sound events only", "[anim_task][sound]") {
    const AnimClip walk = soundClip(30.0F, {10, 25}, 1);
    const AnimClip jog = soundClip(30.0F, {10, 25}, 3);
    const std::array<GaitClip, 5> clips{GaitClip{&walk, 1}, GaitClip{&jog, 2}, GaitClip{&jog, 3}, GaitClip{&jog, 4},
                                        GaitClip{&jog, 5}};
    AnimTaskStack stack;
    stack.keepEvents(true);
    // Mostly walking: the walk leads, and its steps (1) are the ones kept.
    stack.change(std::make_unique<GaitBlendTask>(clips, 0.2F, 0.0F, 1.0F, 0U), 0.0F);
    const std::vector<std::uint32_t> sounds = soundsOver(stack, 30);
    CHECK(sounds == std::vector<std::uint32_t>{1, 1});
}
