// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"
#include "camera/camera_view.h"

// The win camera (`Cam_Win`, `CameraCreateWin`): at the end of a Rumble match it circles the winner, looking at a point
// above him. One shared camera, set up again by each call. Research: docs/research/rumble.md#win-camera

namespace coney::camera {

/// `CameraCreateWin(name, target, fov, distance, angle, speed, height, far, direction)`'s numbers
/// (docs/references/bindings/camera.md#cameracreatewin).
struct WinCameraSettings {
    float fieldOfView = 58.0F;
    float distance = 3.0F;       ///< From the look-at point along the target's facing, metres.
    float angleDegrees = -19.0F; ///< The start turned by this about the vertical.
    float speedDegrees = 40.0F;  ///< The orbit, degrees a second.
    float height = 1.1F;         ///< The look-at point above the target's feet, metres.
    float farClip = 90.0F;       ///< At most kMaxFarClip.
    float direction = 1.0F;      ///< +1 or −1: the orbit's sense.
};

/// The win camera.
class WinCamera {
  public:
    /// The far clip is at most this; the near clip is kNearClip.
    static constexpr float kMaxFarClip = 150.0F;
    static constexpr float kNearClip = 0.1F;

    /// Set up on a target whose feet are at `feet`, facing `headingDegrees` (0 facing +y, anticlockwise): the look-at
    /// point `height` above the feet, the camera `distance` from it along the facing turned by the angle.
    /// @orig 0x001439d8 Cam_Win_Start (unknown)
    WinCamera(anim::Vec3 feet, float headingDegrees, const WinCameraSettings& settings);

    /// One update of `seconds`: while orbiting, the camera turns about the look-at point by direction × speed ×
    /// seconds. **Coney choice**: the screen-effect state that stops the orbit (`0x005fdeb8 + 0x1d8` and `+0x1dc`, an
    /// open question on the page) is not read, so it orbits until replaced.
    /// @orig 0x00143c78 Cam_Win_Update (unknown)
    void update(float seconds);

    /// What it shows: from its position looking at the look-at point.
    [[nodiscard]] CameraView view() const;

    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// Whether it orbits (`+0x1f8`).
    [[nodiscard]] bool orbiting() const { return m_orbiting; }
    void setOrbiting(bool on) { m_orbiting = on; }

  private:
    WinCameraSettings m_settings;
    anim::Vec3 m_lookAt;
    anim::Vec3 m_position;
    bool m_orbiting = true;
};

} // namespace coney::camera
