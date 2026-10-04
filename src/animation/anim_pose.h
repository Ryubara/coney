// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"

// Sampling a clip at a time into a pose, playing a clip forward, and blending two poses. Pure functions of their
// arguments: no clock is read, so the same calls give the same poses on every run (test mode).
// Behaviour: docs/research/formats/animation.md#behaviour.

namespace coney::anim {

/// A sampled pose: the two root translations and a local rotation for each of the 34 bones (relative to the bone's
/// parent). Bones a clip does not animate keep the rotation they were given (the bind rotation).
struct Pose {
    Vec3 rootVelocity;               ///< Section A at this time, m/s; zero when the clip has none.
    Vec3 rootTranslation;            ///< Section B at this time: the pelvis's position.
    bool hasRootVelocity = false;    ///< The clip has a section A.
    bool hasRootTranslation = false; ///< The clip has a section B; otherwise the pelvis keeps its bind offset.
    std::array<Quat, kPoseBones> rotations{};
};

/// The value of a position channel at `frame` (fractional): lerped between the key at or before it and the next one,
/// `t = (frame - key) / (next - key)`; the last key holds after it, the first before it. `channel` must not be empty
/// (checked by CONEY_ASSERT).
[[nodiscard]] Vec3 sampleChannel(std::span<const PositionKey> channel, float frame);

/// The value of a rotation channel at `frame`, as sampleChannel() but nlerped.
[[nodiscard]] Quat sampleChannel(std::span<const RotationKey> channel, float frame);

/// The pose of `clip` at `seconds` (clamped to 0 and the clip's duration): every channel sampled at frame
/// `seconds × 30`; bones without a channel take `bindRotations`.
/// @orig 0x00104ce0 AnimCursor_SamplePose (unknown)
[[nodiscard]] Pose samplePose(const AnimClip& clip, float seconds, std::span<const Quat, kPoseBones> bindRotations);

/// `a` blended towards `b` by `weight` (0 gives `a`, 1 gives `b`): rotations slerped, translations lerped. With
/// `subtreeBone` below kPoseBones only that bone and its descendants (by poseBoneParent()) are blended, the other
/// bones keep `a`'s, so an upper-body clip can play over walking legs; with kPoseBones or more, every bone. The root
/// translations are blended only for the whole skeleton.
/// @orig 0x00105158 Pose_BlendPartial (unknown)
[[nodiscard]] Pose blendPoses(const Pose& a, const Pose& b, float weight, std::size_t subtreeBone = kPoseBones);

/// Where a clip is being played: its time, advanced by the fixed step. The original's cursor also keeps a key pointer
/// and a countdown per channel so it never searches; Coney samples by a search per channel (sampleChannel()), so
/// the time is all the state there is.
class AnimCursor {
  public:
    /// A cursor at the start of `clip`, which must outlive it.
    explicit AnimCursor(const AnimClip& clip) : m_clip(&clip) {}

    /// Moves the time on by `seconds × rate`. Returns the overshoot past the clip's duration (0 while inside it); the
    /// time then stays at the end, and the caller loops or chains the next clip with the overshoot, losing no time.
    /// The original's rate comes from the clip's flags (animation.md); where those flags are stored is not known yet,
    /// so callers pass 1.
    /// @orig 0x001044a0 AnimCursor_Advance (unknown)
    float advance(float seconds, float rate = 1.0F);

    /// Starts the clip again at `seconds` (an overshoot from advance(), clamped to the duration).
    void restart(float seconds = 0.0F);

    /// The time in seconds and the frame (`time × 30`).
    [[nodiscard]] float time() const { return m_time; }
    [[nodiscard]] float frame() const { return m_time * kClipFrameRate; }
    /// The clip.
    [[nodiscard]] const AnimClip& clip() const { return *m_clip; }

  private:
    const AnimClip* m_clip; // not owned
    float m_time = 0.0F;
};

} // namespace coney::anim
