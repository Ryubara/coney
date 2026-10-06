// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_clip.h"
#include "animation/anim_math.h"

// A turn and a slide spread over a time at a constant rate, as an attack steers onto its target: the turn's rate
// (angle / T) and the slide's velocity ((goal - position) / T) are stored when the attack starts, and each character
// step applies one step's worth of them until their time runs out, the last step only for the time left. No easing.
// Research: docs/research/combat.md#targets ("The steer in detail")

namespace coney::human {

/// What one step of a TurnAndSlide gives: the heading's change and the slide's velocity for the step.
struct TurnAndSlideStep {
    float turn = 0.0F;   ///< Radians to add to the heading this step.
    anim::Vec3 velocity; ///< The slide's velocity this step (m/s, horizontal), on top of the clip's root motion.
};

/// A stored turn rate and slide velocity, each with the time it has left (human `+0x304` / `+0x308` and `+0x2e0` /
/// `+0x300`).
class TurnAndSlide {
  public:
    /// Turns from `heading` to `target` (radians) over `seconds`: the rate is the wrapped difference over `seconds`.
    /// A difference under 0.01 rad, or no time, stores no turn.
    /// @orig 0x0023cf88 Human_TurnToOver (unknown)
    void turnToOver(float heading, float target, float seconds);
    /// Slides from `from` to `goal` (across the ground) over `seconds`: the velocity is the difference over `seconds`.
    /// None when the goal is under 0.01 m away, 13 m or more away, or would need more than 50 m/s with `seconds` above
    /// one step; none without time.
    /// @orig 0x0023d2b8 Human_MoveToOver (unknown)
    void moveToOver(anim::Vec3 from, anim::Vec3 goal, float seconds);
    /// One character step of `dt` seconds: the turn rate × the step and the slide velocity, each for the time it has
    /// left (the last step scaled by the time left / `dt`); both count down by `dt` and stop at 0.
    /// @orig 0x0023f5e0 Human_ApplyTurnAndSlide (unknown)
    [[nodiscard]] TurnAndSlideStep step(float dt);
    /// Drops what is left of both.
    void clear();

    [[nodiscard]] bool turning() const { return m_turnTime > 0.0F; }
    [[nodiscard]] bool sliding() const { return m_slideTime > 0.0F; }
    /// The stored turn rate, rad/s, and slide velocity, m/s.
    [[nodiscard]] float turnRate() const { return m_turnRate; }
    [[nodiscard]] anim::Vec3 slideVelocity() const { return m_slide; }

  private:
    float m_turnRate = 0.0F;  // rad/s
    float m_turnTime = 0.0F;  // seconds left
    anim::Vec3 m_slide;       // m/s
    float m_slideTime = 0.0F; // seconds left
};

/// The smallest turn and slide the steer stores (below them, nothing): 0.01 rad and 0.01 m.
inline constexpr float kSteerMinTurn = 0.01F;
inline constexpr float kSteerMinSlide = 0.01F;
/// The steer slides no farther than this (m), nor faster than kSteerMaxSlideSpeed (m/s) over more than one step.
inline constexpr float kSteerMaxSlide = 13.0F;
inline constexpr float kSteerMaxSlideSpeed = 50.0F;
/// The target's lead: its velocity × (the steer's time + this), seconds.
inline constexpr float kSteerLeadExtraSeconds = 0.1F;
/// A lead longer than this (m) that carries the target farther away is halved.
inline constexpr float kSteerLeadHalveBeyond = 1.0F;
/// The reach is longer by this for a target scaled above kSteerBigScale, and shorter by kSteerRearReach from behind.
inline constexpr float kSteerBigReach = 0.07F;
inline constexpr float kSteerBigScale = 1.1F;
inline constexpr float kSteerRearReach = 0.1F;

/// The clip event types that count as a contact: an attack's steer lasts until the first of them, and a grab's
/// alignment a share of the time to it.
[[nodiscard]] bool isContactEvent(unsigned type);

/// The time to `clip`'s first contact event played at `rate`: the first event isContactEvent() takes (its frame / 30
/// / `rate`), or the clip's playing time when it has none.
/// @orig 0x00101658 Anim_FirstContactTime (unknown)
[[nodiscard]] float firstContactTime(const anim::AnimClip& clip, float rate);
/// Where an attack steers to (attackSteerGoal()).
struct SteerGoal {
    anim::Vec3 aim;   ///< The target where it will be: the point the attacker turns to face.
    anim::Vec3 stand; ///< Where the attacker should stand: `reach` short of `aim` along the line from the attacker.
};

/// Where an attacker standing at `from` should stand to strike a target at `target` moving at `targetVelocity` with
/// a reach of `reach`, `seconds` before the attack's first event: the target led by its velocity × (`seconds` +
/// kSteerLeadExtraSeconds) (half that when the lead is longer than kSteerLeadHalveBeyond and carries it farther away),
/// less `reach` along the line from the attacker. Horizontal; both points keep `from`'s height.
[[nodiscard]] SteerGoal attackSteerGoal(anim::Vec3 from, anim::Vec3 target, anim::Vec3 targetVelocity, float reach,
                                        float seconds);

} // namespace coney::human
