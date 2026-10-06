// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"

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

/// A view at `position` looking at `lookAt` (world up), with `fieldOfView`, `nearClip` and `farClip`.
[[nodiscard]] CameraView viewLookingAt(anim::Vec3 position, anim::Vec3 lookAt, float fieldOfView, float nearClip,
                                       float farClip);

} // namespace coney::camera
