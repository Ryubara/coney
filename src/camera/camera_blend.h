// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "camera/camera_view.h"

// The blend camera (type 5): `CameraMakeActive(camera, seconds > 0)` runs it from the view the old camera showed to
// the new camera's live view, linear in time. Research: docs/research/camera.md#blends

namespace coney::camera {

/// One blend between two cameras.
class CameraBlend {
  public:
    /// A blend from `from`, the source's view as the blend begins, over `seconds` (above 0).
    /// @orig 0x00143078 Cam_Blend_Start (unknown)
    CameraBlend(const CameraView& from, float seconds);

    /// One update of `dt` seconds toward `to`, the destination's view after its own update: the elapsed time counts
    /// on, `t = min(elapsed / seconds, 1)`; the look-at point and the position lerped by `t`, the orientation slerped,
    /// the field of view the destination's and the far clip the smaller of the last one and the lerped one. The
    /// sphere push the original gives the position is left out.
    /// @orig 0x00143590 Cam_Blend_Update (unknown)
    [[nodiscard]] CameraView step(const CameraView& to, float dt);

    /// Whether the time is up: the destination then becomes current directly.
    [[nodiscard]] bool done() const { return m_elapsed >= m_seconds; }
    /// The share of the way, 0 to 1.
    [[nodiscard]] float progress() const;

  private:
    CameraView m_from;
    float m_seconds;
    float m_elapsed = 0.0F;
    float m_farClip; // slot +0x20c: never grows during the blend
};

} // namespace coney::camera
