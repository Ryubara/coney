// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"
#include "core/pad.h"

namespace coney::characters {

/// Where a camera is and how it is turned: a right-handed frame whose `right` is `forward × up`, as the world viewer's
/// camera pose is (src/world/view_frustum.h).
struct OrbitPose {
    anim::Vec3 position;
    anim::Vec3 forward;
    anim::Vec3 up;
    anim::Vec3 right;
};

/// A camera that circles a point, for Coney's character viewer, driven by a pad like every movement in Coney. Coney's
/// own tool: the game's cameras follow the player. It works in the characters' game axes, z up. Its controls are
/// Coney's choice (docs/guides/building.md#the-character-viewer), each analog in proportion to the stick:
///
/// - right stick, d-pad (arrow keys): circle left and right, and up and down;
/// - left stick up and down (W / S), R1 / L1 (E / Q): move in and out.
///
/// It reads no clock: update() is given the step's length, so the same input gives the same path in test mode.
class OrbitCamera {
  public:
    /// Turning speed in radians a second at full stick or with a d-pad direction held.
    static constexpr float kTurnRate = 2.0F;
    /// How fast the distance changes at full stick: it shrinks or grows by e^kZoomRate each second.
    static constexpr float kZoomRate = 1.2F;
    /// The nearest and farthest it goes, in metres.
    static constexpr float kMinDistance = 0.5F;
    static constexpr float kMaxDistance = 20.0F;
    /// How far above or below the target it may circle, in radians, short of straight up so the view never flips.
    static constexpr float kMaxPitch = 1.45F;

    /// A camera `distance` from `target`, at heading `yaw` (radians about z, 0 on the +x side, growing towards +y) and
    /// `pitch` (radians above the target's height, clamped to ±kMaxPitch).
    OrbitCamera(anim::Vec3 target, float distance, float yaw, float pitch);

    /// Circles and moves in or out by what `pad` holds, over `seconds`.
    void update(const Pad& pad, float seconds);

    /// The camera's pose, looking at the target.
    [[nodiscard]] OrbitPose pose() const;

    [[nodiscard]] anim::Vec3 target() const { return m_target; }
    [[nodiscard]] float distance() const { return m_distance; }
    [[nodiscard]] float yaw() const { return m_yaw; }
    [[nodiscard]] float pitch() const { return m_pitch; }

  private:
    anim::Vec3 m_target;
    float m_distance;
    float m_yaw;
    float m_pitch;
};

} // namespace coney::characters
