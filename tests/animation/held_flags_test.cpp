// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_task.h"

// The flags a clip task holds on its human's record +0x08 (docs/research/tasks.md#held-flags): set as it starts, moved
// by its clip's events 0x2c, 0x2d and 0x48, and given back when its clip ends or it is cut off. Synthetic clips only.

using coney::anim::AnimClip;
using coney::anim::AnimTaskStack;
using coney::anim::ClipEvent;
using coney::anim::ClipThenNextTask;
using coney::anim::LoopTask;
namespace anim = coney::anim;

namespace {

constexpr float kStep = 1.0F / 30.0F;
// An attack's clip task holds its three phases and starts in the wind-up, as Attack_Start builds it.
constexpr std::uint32_t kAttackHeld = anim::kFlagAttackPhases;
// A bit no clip holds, set by the human's own code.
constexpr std::uint32_t kOwnBit = 0x8000;

// A still clip of `frames` frames with the events `events` (frame, type).
AnimClip eventClip(float frames, const std::vector<std::pair<std::uint16_t, std::uint16_t>>& events) {
    AnimClip clip;
    clip.duration = frames / anim::kClipFrameRate;
    for (const auto& [frame, type] : events) {
        clip.events.push_back(ClipEvent{.frame = frame, .type = type, .word = 0, .position = {}, .rotation = {}});
    }
    return clip;
}

// S1 as Rembrandt's clip has it: 16 frames, the window at 5, the end at 12, the recovery at 13.
AnimClip s1Clip() {
    return eventClip(16.0F, {{5, anim::kEventChainWindow}, {12, anim::kEventAttackEnd}, {13, anim::kEventRecovery}});
}

// A clip played once at `rate` holding the attack's bits, then `loop`.
std::unique_ptr<ClipThenNextTask> attackTask(const AnimClip& clip, const AnimClip& loop, float rate) {
    auto task = std::make_unique<ClipThenNextTask>(clip, 12, rate, 0U, std::make_unique<LoopTask>(loop, 358, 1.0F, 0U));
    task->holdFlags(kAttackHeld, anim::kFlagWindUp);
    return task;
}

// The record's +0x08 after each update, from the update the task is played (0) for `updates` updates.
std::vector<std::uint32_t> flagsByUpdate(AnimTaskStack& stack, int updates) {
    std::vector<std::uint32_t> flags{stack.flags()};
    for (int k = 1; k < updates; ++k) {
        stack.advance(kStep);
        flags.push_back(stack.flags());
    }
    return flags;
}

} // namespace

TEST_CASE("an attack's clip sets its wind-up, its events open the window, end it and start the recovery, and its end "
          "gives the bits back: S1's phases at runtime",
          "[anim_task][held_flags]") {
    const AnimClip clip = s1Clip();
    const AnimClip idle = eventClip(30.0F, {});
    AnimTaskStack stack;
    stack.setFlags(kOwnBit);
    stack.change(attackTask(clip, idle, 0.8F), 0.0F);
    const std::vector<std::uint32_t> flags = flagsByUpdate(stack, 22);
    REQUIRE(flags.size() == 22);
    // Wind-up 0-5, window 6-14, the end phase at 15, the recovery 16-19, nothing from 20 (combat.md#input-return);
    // the human's own bit is left alone throughout.
    for (int k = 0; k < 22; ++k) {
        INFO("update " << k);
        const std::uint32_t phase = flags[static_cast<std::size_t>(k)] & ~kOwnBit;
        std::uint32_t expected = 0;
        if (k <= 5) {
            expected = anim::kFlagWindUp;
        } else if (k <= 14) {
            expected = anim::kFlagChainWindow;
        } else if (k == 15) {
            expected = anim::kFlagAttackEnd;
        } else if (k <= 19) {
            expected = anim::kFlagRecovery;
        }
        CHECK(phase == expected);
        CHECK((flags[static_cast<std::size_t>(k)] & kOwnBit) != 0);
    }
    REQUIRE(stack.top() != nullptr);
    CHECK(stack.top()->animId() == 358U);
}

TEST_CASE("events fire on the update the clip's nearest frame reaches them, a tie going to the lower frame",
          "[anim_task][held_flags]") {
    CHECK(anim::eventFrame(0.0F) == 0);
    CHECK(anim::eventFrame(4.6F / 30.0F) == 5);
    CHECK(anim::eventFrame(4.4F / 30.0F) == 4);
    CHECK(anim::eventFrame(13.5F / 30.0F) == 13); // the tie goes down
    // The power strike's window event at frame 14, at rate 0.75: 13.5 frames after 18 updates (a tie), so 19.
    const AnimClip clip = eventClip(33.0F, {{14, anim::kEventChainWindow}, {25, anim::kEventAttackEnd}});
    const AnimClip idle = eventClip(30.0F, {});
    AnimTaskStack stack;
    stack.change(attackTask(clip, idle, 0.75F), 0.0F);
    const std::vector<std::uint32_t> flags = flagsByUpdate(stack, 34);
    REQUIRE(flags.size() == 34);
    CHECK(flags[18] == anim::kFlagWindUp);
    CHECK(flags[19] == anim::kFlagChainWindow);
    CHECK(flags[32] == anim::kFlagChainWindow);
    CHECK(flags[33] == anim::kFlagAttackEnd);
}

TEST_CASE("a clip cut off by a new task gives its bits back once its fade ends, unless the new task holds them",
          "[anim_task][held_flags]") {
    const AnimClip clip = s1Clip();
    const AnimClip idle = eventClip(30.0F, {});
    // Cut by an idle over a 0.1 s fade: the bits stay while the attack fades out, then go.
    AnimTaskStack stack;
    stack.change(attackTask(clip, idle, 0.8F), 0.0F);
    stack.advance(kStep);
    stack.change(std::make_unique<LoopTask>(idle, 388, 1.0F, 0U), 0.1F);
    CHECK(stack.flags() == anim::kFlagWindUp);
    for (int k = 0; k < 4; ++k) {
        stack.advance(kStep);
    }
    CHECK(stack.layerCount() == 1);
    CHECK(stack.flags() == 0);

    // The next attack of a chain over the old one: it starts in its wind-up, and the old one leaving clears nothing
    // the new one holds; only the newest task's events fire.
    AnimTaskStack chain;
    chain.change(attackTask(clip, idle, 0.8F), 0.0F);
    for (int k = 0; k < 6; ++k) {
        chain.advance(kStep);
    }
    REQUIRE(chain.flags() == anim::kFlagChainWindow);
    chain.change(attackTask(clip, idle, 0.8F), 0.1F);
    CHECK(chain.flags() == anim::kFlagWindUp);
    for (int k = 0; k < 4; ++k) {
        chain.advance(kStep);
    }
    CHECK(chain.layerCount() == 1);
    CHECK(chain.flags() == anim::kFlagWindUp);
}

TEST_CASE("an event changes nothing on a task that does not hold its bits; the recovery replaces what the task holds",
          "[anim_task][held_flags]") {
    const AnimClip clip = s1Clip();
    const AnimClip idle = eventClip(30.0F, {});
    // A clip played with no flags (a reaction, a paired clip): its events leave the record alone.
    AnimTaskStack plain;
    plain.change(std::make_unique<ClipThenNextTask>(clip, 270, 0.8F, 0U, std::make_unique<LoopTask>(idle, 1, 1.0F, 0U)),
                 0.0F);
    for (int k = 0; k < 20; ++k) {
        plain.advance(kStep);
        CHECK(plain.flags() == 0);
    }
    // The events one by one on a task holding the grab bit: only the recovery acts, replacing it.
    LoopTask grab(idle, 71, 1.0F, 0U);
    grab.holdFlags(0x10, 0x10);
    std::uint32_t flags = 0x10;
    anim::applyHeldFlagEvent(anim::kEventChainWindow, grab, flags);
    anim::applyHeldFlagEvent(anim::kEventAttackEnd, grab, flags);
    CHECK(flags == 0x10);
    anim::applyHeldFlagEvent(anim::kEventRecovery, grab, flags);
    CHECK(flags == anim::kFlagRecovery);
    CHECK(grab.heldFlags() == anim::kFlagRecovery);
    // Any other type changes nothing.
    anim::applyHeldFlagEvent(0x24, grab, flags);
    CHECK(flags == anim::kFlagRecovery);
}

TEST_CASE("a clip handing over to a next clip gives its bits back and the next one sets its own",
          "[anim_task][held_flags]") {
    const AnimClip first = eventClip(6.0F, {});
    const AnimClip second = eventClip(12.0F, {});
    const AnimClip idle = eventClip(30.0F, {});
    auto after =
        std::make_unique<ClipThenNextTask>(second, 69, 1.0F, 0U, std::make_unique<LoopTask>(idle, 1, 1.0F, 0U));
    after->holdFlags(0x10, 0x10);
    auto intro = std::make_unique<ClipThenNextTask>(first, 71, 1.0F, 0U, std::move(after));
    intro->holdFlags(0x40000000, 0x40000000);
    AnimTaskStack stack;
    stack.change(std::move(intro), 0.0F);
    CHECK(stack.flags() == 0x40000000U);
    for (int k = 0; k < 7; ++k) {
        stack.advance(kStep);
    }
    REQUIRE(stack.top() != nullptr);
    CHECK(stack.top()->animId() == 69U);
    CHECK(stack.flags() == 0x10U);
    for (int k = 0; k < 13; ++k) {
        stack.advance(kStep);
    }
    CHECK(stack.flags() == 0U);
}
