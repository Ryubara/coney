// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

// What a camera shows this update: where it is, which way it faces, the point it looks at and its lens. Every kind of
// camera (follow, locked, scene, blend) hands one to the cameras' manager, which gives the current one to the renderer.
// Research: docs/research/camera.md#blends

namespace coney::camera {

/// A camera's view, in game axes (z up).
struct CameraView {
    anim::Vec3 position;
    /// The camera's frame: its +y is the view direction, +z up and +x right, turned into the world.
    anim::Quat orientation;
    /// The point it looks at: on the view direction for most cameras, but a blend lerps it apart from its slerped
    /// orientation (docs/research/camera.md#blends).
    anim::Vec3 lookAt;
    float fieldOfView = 65.0F; ///< Horizontal, degrees, on a 4:3 picture.
    float nearClip = 0.1F;
    float farClip = 115.0F;
};

/// The orientation that faces along `forward` (any length but 0) with world +z as up as far as it can, then turned
/// `roll` radians about the view direction (positive turns the top to the right). Straight up or down it takes +y as
/// up.
[[nodiscard]] anim::Quat lookRotation(anim::Vec3 forward, float roll = 0.0F);

/// The unit view direction of `view`: its orientation's +y.
[[nodiscard]] anim::Vec3 viewForward(const CameraView& view);

/// The unit up direction of `view`: its orientation's +z.
[[nodiscard]] anim::Vec3 viewUp(const CameraView& view);

/// Whether `view` sees `point`: no farther than `range` (at most the far clip; 0 or less means the far clip), inside
/// its view window (the 4:3 picture of its field of view) beyond the near clip, and with no triangle of `mesh` (may be
/// null) on the line from the camera to it.
/// @orig 0x00122548 Camera_CanSeePoint (unknown)
[[nodiscard]] bool canSeePoint(const CameraView& view, anim::Vec3 point, float range,
                               const raycast::CollisionMesh* mesh);

/// A view at `position` looking at `lookAt` (world up), with `fieldOfView`, `nearClip` and `farClip`.
[[nodiscard]] CameraView viewLookingAt(anim::Vec3 position, anim::Vec3 lookAt, float fieldOfView, float nearClip,
                                       float farClip);

/// A view pinned at `position` with the orientation `orientation` as a base camera stores it (+0x10 and +0x20,
/// docs/research/camera.md#the-base-camera-object; normalised here, as any length but 0 is taken), looking along its
/// +y with its +z up, the look-at point one metre ahead: `--camera`, Coney's test aid for matching the original's
/// frames.
[[nodiscard]] CameraView pinnedView(anim::Vec3 position, anim::Quat orientation, float fieldOfView, float nearClip,
                                    float farClip);

} // namespace coney::camera
