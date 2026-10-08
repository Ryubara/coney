// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "animation/anim_math.h"

// The launch velocity of a thrown held object, in the thrower's frame (x right, y forward, z up): aimed at a target
// with the lift that brings it down there, or straight ahead with a little lift, slowed by the object's weight.
// Research: docs/research/objects.md#throws

namespace coney::combat {

/// Gravity on a flying object, m/s² (the aim trace steps the z speed by −0.2613 per 1/60 s).
inline constexpr float kThrowGravity = 15.68F;
/// The speed numerators: a throw at a target (or the default) goes at 30 / w m/s, an aimed one with no target at
/// 20 / w, the default from a jog or faster with an overhead or ghetto object 50 / w.
inline constexpr float kThrowSpeed = 30.0F;
inline constexpr float kFastThrowSpeed = 50.0F;
inline constexpr float kAimedThrowSpeed = 20.0F;
/// The default throw's lift: the direction (0, 1, 0.1), normalised.
inline constexpr float kThrowDefaultLift = 0.1F;
/// A target this far above or below the horizontal (|u.z|, of the unit direction) falls to the default.
inline constexpr float kThrowSteepZ = 0.85F;
/// The spread's largest turn at 30 m and beyond, degrees, and the distances it scales between.
inline constexpr float kThrowSpreadDegrees = 8.0F;
inline constexpr float kThrowSpreadDistance = 30.0F;
inline constexpr float kThrowSpreadLeast = 0.1F;
/// A thrower above this scale throws as if the object weighed half.
inline constexpr float kThrowBigScale = 1.1F;

/// The weight factor `w`: 1 for a knife (kind 11), else 0.05 × the type's weight (`+0x62`); halved for a thrower
/// above kThrowBigScale. A bottle (20) is 1.0, a crate or chair (50) 2.5. **Coney's choice**: a weight of 0 counts as
/// 1, so a throw never divides by 0.
[[nodiscard]] float throwWeightFactor(int objectKind, int weight, float throwerScale);

/// What the velocity needs.
struct ThrowAim {
    float weightFactor = 1.0F; ///< throwWeightFactor().
    /// The target point minus the thrower's right hand, in the thrower's frame; nothing for no target.
    std::optional<anim::Vec3> toTarget;
    /// The spread's two draws, each in [−1, 1] (about world z, then about the horizontal axis across the throw);
    /// nothing for no spread (a player: flag `0x40000` at `+0xe0`).
    std::optional<anim::Vec3> spread;
    float spreadShare = 1.0F; ///< The spread's scale: 0.6 for an AI of brain kind 3.
    /// The default goes at 50 / w: the thrower's stored gait (`+0x1a8`) is above 2 (a jog or faster) and he holds a
    /// set 4 or 6 object (or a throwable one).
    bool fastDefault = false;
};

/// The velocity, m/s in the thrower's frame. With a target whose unit direction `u` is not too steep (|u.z| below
/// kThrowSteepZ): `u` clamped to 45° off forward when it points more sideways than forward, times 30 / w, plus
/// 7.84 × d / speed upwards (`d` the distance: half of gravity × the flight time), then turned by the spread, at most
/// 8° × min(d / 30, 1) (at least a tenth of that) each way. Otherwise (no target, or a steep one) the default:
/// (0, 1, 0.1) normalised × 30 / w, or × 50 / w with ThrowAim::fastDefault. **Coney's reading**: the spread's second
/// turn is about the horizontal axis across the velocity's heading.
/// @orig 0x002570e8 Human_ComputeThrowVelocity (unknown)
[[nodiscard]] anim::Vec3 throwVelocity(const ThrowAim& aim);

/// The velocity of a throw from the aiming state with no target: (0, cos p, sin p) × 20 / w, `p` the aim's pitch.
[[nodiscard]] anim::Vec3 aimedThrowVelocity(float weightFactor, float pitch);

} // namespace coney::combat
