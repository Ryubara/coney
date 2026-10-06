// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/locked_camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::camera {

CameraView LockedCamera::view() const {
    constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
    // The forward of the heading (0 facing +y, anticlockwise) tipped down by the pitch.
    const float heading = headingDegrees * kRadians;
    const float pitch = pitchDegrees * kRadians;
    const anim::Vec3 forward{-std::sin(heading) * std::cos(pitch), std::cos(heading) * std::cos(pitch),
                             -std::sin(pitch)};
    return CameraView{.position = position,
                      .orientation = lookRotation(forward, rollDegrees * kRadians),
                      .lookAt = anim::add(position, anim::scale(forward, kAimDistance)),
                      .fieldOfView = fieldOfView,
                      .nearClip = nearClip,
                      .farClip = std::min(farClip, kMaxFarClip)};
}

} // namespace coney::camera
