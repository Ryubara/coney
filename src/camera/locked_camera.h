// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"
#include "camera/camera_view.h"

// The locked camera (type 1, `CameraCreateLocked`): it stays where it was put, looking along its heading and pitch.
// level99's tutorial cuts to nine of them. Research: docs/research/camera.md#locked-cameras

namespace coney::camera {

/// A locked camera as `CameraCreateLocked(name, pos, fov, heading, pitch, roll, near, far)` makes it
/// (docs/references/bindings/camera.md#cameracreatelocked).
struct LockedCamera {
    /// It aims at a point this far ahead along its forward.
    static constexpr float kAimDistance = 3.0F;
    /// The far clip is at most this.
    static constexpr float kMaxFarClip = 150.0F;

    anim::Vec3 position;
    /// **Coney's reading** of the angles, which the pages do not give: the heading as a human's (0 facing +y,
    /// anticlockwise from above), the pitch positive looking down (as the follow camera's), the roll positive turning
    /// the top to the right.
    float headingDegrees = 0.0F;
    float pitchDegrees = 0.0F;
    float rollDegrees = 0.0F;
    float fieldOfView = 65.0F;
    float nearClip = 0.1F;
    float farClip = 100.0F;

    /// Its view: at its position, facing along its angles, looking at the point kAimDistance ahead, the far clip at
    /// most kMaxFarClip. The original's line-of-sight test from its position and the humans `CamLockLocked` gives it
    /// (none in level99) are left out.
    /// @orig 0x00135680 Cam_Locked_Update (unknown)
    [[nodiscard]] CameraView view() const;
};

} // namespace coney::camera
