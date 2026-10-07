// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "raycast/collision_mesh.h"

// The locked camera (type 1, `CameraCreateLocked`): it stays where it was put, looking along its heading and pitch,
// and keeps the humans `CamLockLocked` lists inside the frame's sides. level99's tutorial cuts to nine of them.
// Research: docs/research/camera.md#locked-cameras

namespace coney::camera {

/// A locked camera as `CameraCreateLocked(name, pos, fov, heading, pitch, roll, near, far)` makes it
/// (docs/references/bindings/camera.md#cameracreatelocked).
struct LockedCamera {
    /// It aims at a point this far ahead along its forward.
    static constexpr float kAimDistance = 3.0F;
    /// The far clip is at most this.
    static constexpr float kMaxFarClip = 150.0F;

    anim::Vec3 position;
    /// The script's angles, degrees, read as scriptedOrientation() reads them: the heading 0 facing +y and growing
    /// anticlockwise from above, the pitch positive looking up, the roll positive tipping the top to the right.
    float headingDegrees = 0.0F;
    float pitchDegrees = 0.0F;
    float rollDegrees = 0.0F;
    float fieldOfView = 65.0F;
    float nearClip = 0.1F;
    float farClip = 100.0F;
    /// `CamLockLocked`'s humans (camera `+0x20c`), each listed once, kept inside the frame's sides (keepInView()).
    std::vector<double> keptInView;

    /// Its view: at its position, turned by scriptedOrientation(), looking at the point kAimDistance ahead along its
    /// forward, the far clip at most kMaxFarClip. The original's line-of-sight test from its position is left out.
    /// @orig 0x00135680 Cam_Locked_Update (unknown)
    [[nodiscard]] CameraView view() const;
};

/// The orientation a script's heading, pitch and roll (degrees) give a locked camera or a path point:
/// `q = qz(heading) * qx(pitch) * qy(roll)`, each about its axis by the right-hand rule, so the pitch turns about the
/// camera's own right axis after the heading and the roll about its own forward axis. Its forward (+y) is
/// `(-sin h cos p, cos h cos p, sin p)`: heading 0 looks along +y and 90 along -x, a positive pitch looks up.
/// Research: docs/research/camera.md#scripted-angles
/// @orig 0x001353e0 CamLocked_BuildOrientation (unknown)
/// @orig 0x00142ac8 CamSpline_AddPointAngles (unknown)
[[nodiscard]] anim::Quat scriptedOrientation(float headingDegrees, float pitchDegrees, float rollDegrees);

/// The distances keepInView() works with, metres (docs/references/bindings/camera.md#camlocklocked).
struct KeepInViewRules {
    static constexpr float kMargin = 0.3F;       ///< How far inside a side the test point is kept.
    static constexpr float kHeadHeight = 1.4F;   ///< The test point's height above the feet.
    static constexpr float kNudge = 0.00001F;    ///< Added to the push, so the point ends just inside.
    static constexpr float kGroundFrom = 2.4F;   ///< The ground ray starts this far above the pushed feet ...
    static constexpr float kGroundRay = 3.4F;    ///< ... and is this long.
    static constexpr float kAboveGround = 0.01F; ///< The feet are put this far above the ground it hits.
};

/// One side of a view: its inward unit normal and offset (a point `p` is `dot(p, normal) - w` inside it).
struct ViewSide {
    anim::Vec3 normal;
    float w = 0.0F;
};

/// The side planes of `view`, left then right: through the camera at half the horizontal field of view either side of
/// the view direction. **Coney's reading**: the original reads planes 0 and 1 of the camera (`+0x70`), inferred to be
/// the left and right.
[[nodiscard]] std::array<ViewSide, 2> viewSides(const CameraView& view);

/// One listed human's test: its head point (`feet` + 1.4 m) is tried against the left side, then, only when the left
/// did not push it, the right; within 0.3 m of one it moves along the normal to just 0.3 m inside. The move's part
/// along the side, from `before` (the feet at the last update), is clipped by a ray through `mesh` to stop 0.3 m short
/// of a wall, and the feet are snapped to the ground below. Returns the new feet when it pushed, or when
/// `alreadyPushed` (the original's flag is not reset between humans, so after one push every later human is placed
/// again, snapped to the ground); nothing otherwise. `mesh` may be null (no clip, no snap).
/// @orig 0x00135ca8 LockedCam_KeepHumansInView (unknown)
/// @orig 0x00135960 LockedCam_PushInsideSides (unknown)
[[nodiscard]] std::optional<anim::Vec3> keepInView(const CameraView& view, anim::Vec3 feet, anim::Vec3 before,
                                                   const raycast::CollisionMesh* mesh, bool alreadyPushed);

} // namespace coney::camera
