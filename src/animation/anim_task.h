// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/anim_pose.h"

// Animation tasks: what a character plays. A task owns one or more clip cursors and answers "advance by dt" and
// "sample a pose"; the task stack cross-fades from the old tasks to a new one. Pure and deterministic: no clock is
// read, so the same calls give the same poses on every run (test mode).
// Research: docs/research/formats/animation.md#animation-tasks

namespace coney::anim {

/// Task flags (`+0x10` of a task, docs/research/formats/animation.md#animation-tasks).
inline constexpr std::uint32_t kTaskNoRootVelocity = 0x001; ///< Section A is not sampled: no root motion.
inline constexpr std::uint32_t kTaskNoRootTurn = 0x002;     ///< Bone 0's rotation (the root's turn) is not sampled.
inline constexpr std::uint32_t kTaskEventsMuted = 0x010;    ///< The task's events do not fire.
inline constexpr std::uint32_t kTaskLocomotion = 0x040;     ///< Set on the locomotion blends; meaning not traced.
inline constexpr std::uint32_t kTaskGaitBlendMark = 0x080;  ///< The locomotion's "this is already a gait blend" mark.
inline constexpr std::uint32_t kTaskContinuous = 0x200;     ///< A blend target is kept as given, not rounded.
/// The flags the locomotion gives its gait blend: 0x2c1.
inline constexpr std::uint32_t kGaitBlendFlags =
    kTaskContinuous | kTaskGaitBlendMark | kTaskLocomotion | kTaskNoRootVelocity;

/// The task types Coney plays, by the original's type number.
enum class AnimTaskType : std::uint8_t {
    Loop = 1,         ///< One clip, looping.
    ClipThenNext = 3, ///< One clip, then a next task takes its place.
    GaitBlend = 12,   ///< Five clips on a 0-4 scale.
};

/// The root's motion a pose carries, after the task flags: what the human adds to its own movement
/// (docs/research/formats/animation.md#root-motion).
struct RootMotion {
    Vec3 velocity;     ///< Section A times the task's rate, in the character's axes (facing +y), m/s.
    float turn = 0.0F; ///< Bone 0's rotation as a turn about z per 1/30 s, radians, positive anticlockwise.
};

/// A playing task. Every task has a playback rate (multiplying dt) and task flags.
class AnimTask {
  public:
    AnimTask(float rate, std::uint32_t flags) : m_rate(rate), m_flags(flags) {}
    virtual ~AnimTask() = default;
    AnimTask(const AnimTask&) = delete;
    AnimTask& operator=(const AnimTask&) = delete;
    AnimTask(AnimTask&&) = delete;
    AnimTask& operator=(AnimTask&&) = delete;

    /// The original's type number.
    [[nodiscard]] virtual AnimTaskType type() const = 0;
    /// Moves the task on by `seconds` of game time (the task scales it by its rate).
    virtual void advance(float seconds) = 0;
    /// The task's pose now; bones the clips leave alone take `bindRotations`. Section A is scaled by the rate, and
    /// removed under kTaskNoRootVelocity; bone 0 takes its bind rotation under kTaskNoRootTurn.
    [[nodiscard]] virtual Pose sample(std::span<const Quat, kPoseBones> bindRotations) const = 0;
    /// The time into the (leading) clip and its duration, in seconds of clip time.
    [[nodiscard]] virtual float time() const = 0;
    [[nodiscard]] virtual float duration() const = 0;
    /// The anim id the task reports (the leading clip's).
    [[nodiscard]] virtual std::uint32_t animId() const = 0;
    /// A task that has finished and hands over to another gives it up here once (ClipThenNextTask); others return
    /// null.
    [[nodiscard]] virtual std::unique_ptr<AnimTask> takeReplacement() { return nullptr; }

    /// time() over duration(), 0 for an empty clip.
    [[nodiscard]] float normalisedTime() const;
    [[nodiscard]] float rate() const { return m_rate; }
    [[nodiscard]] std::uint32_t flags() const { return m_flags; }

  protected:
    // Applies the task flags and the rate to a pose sampled from a clip.
    [[nodiscard]] Pose applyFlags(Pose pose, std::span<const Quat, kPoseBones> bindRotations) const;

  private:
    float m_rate;
    std::uint32_t m_flags;
};

/// Type 1: one clip, looping; the overshoot past the end carries into the next pass.
/// @orig 0x00105678 AnimTask_Loop (AnimationBlend.cpp)
class LoopTask final : public AnimTask {
  public:
    /// Plays `clip` (anim id `animId`, which must outlive the task) from `startSeconds`.
    LoopTask(const AnimClip& clip, std::uint32_t animId, float rate, std::uint32_t flags, float startSeconds = 0.0F);
    [[nodiscard]] AnimTaskType type() const override { return AnimTaskType::Loop; }
    void advance(float seconds) override;
    [[nodiscard]] Pose sample(std::span<const Quat, kPoseBones> bindRotations) const override;
    [[nodiscard]] float time() const override { return m_cursor.time(); }
    [[nodiscard]] float duration() const override { return m_cursor.clip().duration; }
    [[nodiscard]] std::uint32_t animId() const override { return m_animId; }

  private:
    AnimCursor m_cursor;
    std::uint32_t m_animId;
};

/// Type 3: one clip played once, then a next task. Coney implements the blend time 0 the locomotion uses: at the
/// clip's end the next task takes its place (takeReplacement()), advanced by the overshoot so no time is lost.
/// @orig 0x00105990 AnimTask_ClipThenNext (AnimationBlend.cpp)
class ClipThenNextTask final : public AnimTask {
  public:
    /// Plays `clip` (anim id `animId`) from `startSeconds`, then hands over to `next` (not null).
    ClipThenNextTask(const AnimClip& clip, std::uint32_t animId, float rate, std::uint32_t flags,
                     std::unique_ptr<AnimTask> next, float startSeconds = 0.0F);
    [[nodiscard]] AnimTaskType type() const override { return AnimTaskType::ClipThenNext; }
    void advance(float seconds) override;
    [[nodiscard]] Pose sample(std::span<const Quat, kPoseBones> bindRotations) const override;
    [[nodiscard]] float time() const override { return m_cursor.time(); }
    [[nodiscard]] float duration() const override { return m_cursor.clip().duration; }
    [[nodiscard]] std::uint32_t animId() const override { return m_animId; }
    [[nodiscard]] std::unique_ptr<AnimTask> takeReplacement() override;
    /// The task that follows, until it has been taken.
    [[nodiscard]] AnimTask* next() const { return m_next.get(); }

  private:
    AnimCursor m_cursor;
    std::uint32_t m_animId;
    std::unique_ptr<AnimTask> m_next;
    bool m_finished = false;
};

/// One clip of a gait blend and its anim id.
struct GaitClip {
    const AnimClip* clip = nullptr; ///< Not owned; not null.
    std::uint32_t animId = 0;
};

/// Type 12, the gait blend: five clips on a blend value from 0 to 4 (walk, jog, run, sprint, sprint for the
/// locomotion). The value eases toward a target at `valueSpeed` units a second; the two clips either side of it play
/// in step (one leads, the other follows at the same normalised time) and are blended by the value's fraction.
/// @orig 0x0010a310 AnimTask_GaitBlend (AnimationBlend.cpp)
class GaitBlendTask final : public AnimTask {
  public:
    /// The largest blend value.
    static constexpr float kMaxValue = 4.0F;

    /// A blend of `clips` at `value` (also the target), the clips at normalised time `phase` (0 to 1).
    GaitBlendTask(const std::array<GaitClip, 5>& clips, float value, float valueSpeed, float rate, std::uint32_t flags,
                  float phase = 0.0F);
    [[nodiscard]] AnimTaskType type() const override { return AnimTaskType::GaitBlend; }
    /// Steps 1-3 of the gait blend's advance: the value toward the target, the pair, the leading clip's time.
    /// @orig 0x0010a5b8 GaitBlend_Advance (AnimationBlend.cpp)
    void advance(float seconds) override;
    /// The lower clip alone below a fraction of 0.01, the upper above 0.995, otherwise both blended by the fraction.
    /// @orig 0x0010adf8 GaitBlend_Sample (AnimationBlend.cpp)
    [[nodiscard]] Pose sample(std::span<const Quat, kPoseBones> bindRotations) const override;
    [[nodiscard]] float time() const override;
    [[nodiscard]] float duration() const override;
    [[nodiscard]] std::uint32_t animId() const override;

    /// Sets the target, clamped to 0-4; rounded to the nearest whole number unless the flags have kTaskContinuous.
    /// @orig 0x0010a500 GaitBlend_SetTarget (AnimationBlend.cpp)
    void setTarget(float target);
    /// Sets the value at once (and the target), clamped to 0-4.
    /// @orig 0x0010a558 GaitBlend_SetValue (AnimationBlend.cpp)
    void setValue(float value);
    [[nodiscard]] float target() const { return m_target; }
    [[nodiscard]] float value() const { return m_value; }
    /// The shared normalised time of the two clips, 0 to 1.
    [[nodiscard]] float phase() const { return m_phase; }
    /// The lower clip of the pair: floor(value) with 0.995, 1.995, 2.995 as the steps (0 to 3).
    [[nodiscard]] std::size_t lowerIndex() const;
    /// Whether the upper clip leads: while the value falls toward the target or its fraction is above 0.875.
    [[nodiscard]] bool upperLeads() const;

  private:
    std::array<GaitClip, 5> m_clips;
    float m_value;
    float m_target;
    float m_valueSpeed;
    float m_phase;
};

/// A character's tasks: the newest first, each fading in over the ones older than it. The original keeps one stack of
/// tasks with fade tasks between them ("new task at the bottom, fade on top",
/// docs/research/formats/animation.md#task-stack); Coney keeps the same thing as layers, each a task with the fade it
/// came in with, which samples and cleans up the same way: a fade blends its new task over everything older, and when
/// it ends everything older goes.
class AnimTaskStack {
  public:
    /// At most this many tasks before a change finishes the newest fade early, and before any fade ends at once;
    /// fades count as tasks, as in the original.
    static constexpr std::size_t kEarlyFinishTasks = 6;
    static constexpr std::size_t kMaxTasks = 12;

    /// A change of animation: `task` (not null) becomes the newest, fading in over the current tasks across
    /// `fadeSeconds` (0: at once, the older tasks go now).
    /// @orig 0x001754e8 CharacterInstance_InsertTask (unknown)
    /// @orig 0x00106c80 AnimTask_Fade (AnimationBlend.cpp)
    void change(std::unique_ptr<AnimTask> task, float fadeSeconds);
    /// Advances every task and every fade by `seconds`; a finished clip-then-next task gives way to its next one,
    /// and a fade that has run its time removes the tasks older than its own.
    void advance(float seconds);
    /// The blended pose: the oldest task's, then each newer task over it with the outgoing weight
    /// `(1 + cos(π t / d)) / 2`.
    [[nodiscard]] Pose sample(std::span<const Quat, kPoseBones> bindRotations) const;
    /// The newest task (the original's "top" task, under the fade), or null with none.
    /// @orig 0x00175210 CharacterInstance_TopTask (unknown)
    [[nodiscard]] AnimTask* top() const { return m_layers.empty() ? nullptr : m_layers.front().task.get(); }
    /// Tasks held, fades included (n layers hold n tasks and n - 1 fades).
    [[nodiscard]] std::size_t taskCount() const { return m_layers.empty() ? 0 : m_layers.size() * 2 - 1; }
    /// Layers held, newest first.
    [[nodiscard]] std::size_t layerCount() const { return m_layers.size(); }

    /// The outgoing weight of a fade `elapsed` seconds into `duration`: 1 at the start, 0 at the end and after;
    /// 0 for a zero duration.
    [[nodiscard]] static float outgoingWeight(float elapsed, float duration);

  private:
    struct Layer {
        std::unique_ptr<AnimTask> task;
        float fade = 0.0F;    // seconds this task fades in over
        float elapsed = 0.0F; // seconds of that fade run
    };
    // Drops every layer older than layer `index`, ending its fade.
    void dropOlderThan(std::size_t index);

    std::vector<Layer> m_layers; // newest first
};

/// The root's motion in a pose: section A as sampled (already scaled by its task's rate), and bone 0's rotation read
/// as a turn about z per 1/30 s (angle `2 acos(w)`, its sign from z), 0 when the pose has neither.
[[nodiscard]] RootMotion rootMotionOf(const Pose& pose);

} // namespace coney::anim
