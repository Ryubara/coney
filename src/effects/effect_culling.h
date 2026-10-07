// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "animation/anim_math.h"
#include "camera/camera_view.h"

namespace coney::effects {

/// Whether `point` (game axes) lies in the view of `view` with a `margin` in metres: it fails only when it lies more
/// than `margin` outside one of the view's six frustum planes (the near and far clip planes and the four sides of the
/// view window on the 4:3 picture).
///
/// Research: docs/research/script-types.md (the spawn tests), docs/research/objects.md#shatter
/// @orig 0x003a50e0 Camera_IsPointInPlayerView (unknown)
[[nodiscard]] bool pointInView(const camera::CameraView& view, anim::Vec3 point, float margin);

/// The effects' near test (`0x003a7d58(range, margin, p)`): a camera within `range` metres of `point` and `point` in
/// its view with `margin` (pointInView()). Coney has one view, player 1's, whose camera is the scene camera while a
/// scene plays. The glass shatter asks with 15 and 10.
/// @orig 0x003a5280 Cameras_IsWithinRange (unknown)
/// @orig 0x003a51f8 Cameras_IsPointVisibleAny (unknown)
[[nodiscard]] bool effectNearView(const camera::CameraView& view, anim::Vec3 point, float range, float margin);

} // namespace coney::effects
