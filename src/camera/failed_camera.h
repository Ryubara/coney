// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"
#include "camera/camera_lens.h"
#include "camera/camera_view.h"

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

// The death camera (`Cam_Failed`, type 12): when a mission fails, the gameplay cuts to a top-down shot of the beaten
// player that turns slowly and sinks toward him until the mission-failed screen opens. Research:
// docs/research/camera.md#death-camera

namespace coney::camera {

/// The death camera.
class FailedCamera {
  public:
    /// Its lens: the base camera's, which its constructor leaves alone (**inferred**).
    static constexpr CameraLens kLens = kBaseCameraLens;
    /// The upward ray is this much longer than the near clip; with nothing above, the camera sits this high.
    static constexpr float kHeight = 5.0F;
    /// The pitch about its own x axis, degrees (a half-angle of −0.7704 rad): almost straight down.
    static constexpr float kPitchDegrees = -88.28F;
    /// The random start: one of kYawSteps turns of kYawStepDegrees about the view direction, from −180°.
    static constexpr int kYawSteps = 36;
    static constexpr float kYawStepDegrees = 10.0F;
    /// The turn about the view direction, degrees a second at turn rate 1.
    static constexpr float kTurnDegrees = 15.0F;
    /// The sink toward the body, metres a second, and the nearest it comes.
    static constexpr float kSinkSpeed = 0.18F;
    static constexpr float kNearest = 2.5F;

    /// Placed over a human whose feet are at `feet`: a ray straight up from the feet, near clip + kHeight long (mask
    /// 0x200; `mesh` may be null for none), gives the height, the hit's distance less the near clip; the camera sits
    /// that high above the feet, looking down at kPitchDegrees, turned −180° + `yawStep` × 10° about its view
    /// direction (`yawStep` is the game's `Random_Int(36)`, taken modulo kYawSteps).
    /// @orig 0x00123630 CamFailed_Construct (unknown)
    /// @orig 0x001238a8 CamFailed_Place (unknown)
    FailedCamera(anim::Vec3 feet, const raycast::CollisionMesh* mesh, int yawStep);

    /// One update of `seconds` while rotating (`+0x1fe`): it turns about its view direction at kTurnDegrees a second
    /// and comes kSinkSpeed a second nearer the feet, never nearer than kNearest. **Coney's reading**: the camera
    /// stays straight above the feet as it sinks. **Coney choice**: the screen-effect state that stops it (the fade
    /// finished, `0x005fdeb8 + 0x1d8`) is not read; the 6.5 s tint never finishes before the 6 s hand-off anyway.
    /// @orig 0x00123bf0 CamFailed_Update (unknown)
    void update(float seconds);

    /// What it shows.
    [[nodiscard]] CameraView view() const;

    /// The point it looks at: the feet.
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// How far above the feet it is.
    [[nodiscard]] float distance() const { return m_distance; }
    /// Its turn about the view direction, degrees from the unturned shot.
    [[nodiscard]] float rollDegrees() const { return m_rollDegrees; }
    /// Whether it turns and sinks (`+0x1fe`).
    [[nodiscard]] bool rotating() const { return m_rotating; }
    void setRotating(bool on) { m_rotating = on; }

  private:
    anim::Vec3 m_lookAt;
    float m_distance = kHeight;
    float m_rollDegrees = 0.0F;
    bool m_rotating = true;
};

} // namespace coney::camera
