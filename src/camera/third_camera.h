// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"
#include "camera/camera_view.h"

// The third-person camera (`Cam_3rdPerson`, type 16, `CameraCreateThird`): a camera behind a target, turning with it
// at a lag, for the chase and close shots. Research: docs/references/bindings/camera.md#cameracreatethird

namespace coney::camera {

/// `CameraCreateThird(name, target, fov, distance, height, angle, offset, near, far)`'s numbers.
struct ThirdCameraSettings {
    float fieldOfView = 60.0F;
    float distance = 5.1F;     ///< Metres behind the look-at point along the eased facing.
    float height = 1.8F;       ///< Metres above the look-at point.
    float angleDegrees = 0.0F; ///< The camera swung about the vertical through the look-at point; 0 is behind.
    /// The look-at point from the target's feet, in the target's frame (x right, y ahead, z up).
    anim::Vec3 offset{0.0F, 0.0F, 1.5F};
    float nearClip = 0.1F;
    float farClip = 150.0F; ///< At most ThirdCamera::kMaxFarClip.
};

/// A third-person camera.
class ThirdCamera {
  public:
    /// The far clip is at most this.
    static constexpr float kMaxFarClip = 150.0F;
    /// The share of the way to the target's facing the camera turns each update.
    static constexpr float kTurnShare = 0.1F;

    /// A camera with `settings`, placed by its first update.
    explicit ThirdCamera(const ThirdCameraSettings& settings);

    /// One update with the target's feet and heading (degrees, 0 facing +y, anticlockwise): the look-at point is the
    /// feet plus the offset turned by the heading, the camera's facing eases kTurnShare of the way to the target's
    /// (all the way on its first update), and it stands `distance` behind that facing turned by the angle and
    /// `height` above the look-at point.
    /// @orig 0x001205e8 Cam3rdPerson_Update (unknown)
    void update(anim::Vec3 feet, float headingDegrees);

    /// What it shows.
    [[nodiscard]] CameraView view() const;
    /// Where it is.
    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// The point it looks at.
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// The camera's eased facing, degrees.
    [[nodiscard]] float headingDegrees() const { return m_heading; }
    /// Sets the near and far clips (the far at most kMaxFarClip).
    void setClipping(float nearClip, float farClip);

  private:
    ThirdCameraSettings m_settings;
    anim::Vec3 m_position;
    anim::Vec3 m_lookAt;
    float m_heading = 0.0F;
    bool m_started = false;
};

} // namespace coney::camera
