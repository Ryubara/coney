// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "animation/anim_math.h"
#include "camera/camera_view.h"

// The fixed camera (`Cam_Fixed`, type 0, `CameraCreateFixed`): a camera standing at one point that turns each update
// to look at the humans on the shared camera target list. Research:
// docs/references/bindings/camera.md#cameracreatefixed

namespace coney::camera {

/// A fixed camera.
class FixedCamera {
  public:
    /// The far clip is at most this.
    static constexpr float kMaxFarClip = 150.0F;

    /// At `position`, looking at the listed humans' average plus `offset`; it looks along +y until its first update.
    FixedCamera(anim::Vec3 position, anim::Vec3 offset, float fieldOfView, float nearClip, float farClip);

    /// One update: looks at the average of `targets` (the listed humans found) plus the offset; with none it keeps its
    /// last view. **Coney choice**: the original's look at one chosen entry when two list counters are equal is not
    /// traced, so the average is always used.
    /// @orig 0x00124690 CamFixed_Update (unknown)
    /// @orig 0x00124000 CamFixed_ComputeLookAt (unknown)
    void update(std::span<const anim::Vec3> targets);

    /// What it shows.
    [[nodiscard]] CameraView view() const;
    /// The point it looks at.
    [[nodiscard]] anim::Vec3 lookAt() const { return m_lookAt; }
    /// Sets the near and far clips (the far at most kMaxFarClip).
    void setClipping(float nearClip, float farClip);

  private:
    anim::Vec3 m_position;
    anim::Vec3 m_offset;
    anim::Vec3 m_lookAt;
    float m_fieldOfView;
    float m_nearClip;
    float m_farClip;
};

} // namespace coney::camera
