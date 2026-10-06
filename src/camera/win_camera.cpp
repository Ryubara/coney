// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/win_camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::camera {

namespace {

constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;

// The horizontal unit vector of a heading in degrees (0 facing +y, anticlockwise from above).
anim::Vec3 facing(float degrees) {
    const float radians = degrees * kRadians;
    return anim::Vec3{-std::sin(radians), std::cos(radians), 0.0F};
}

} // namespace

WinCamera::WinCamera(anim::Vec3 feet, float headingDegrees, const WinCameraSettings& settings)
    : m_settings(settings), m_lookAt{feet.x, feet.y, feet.z + settings.height} {
    m_position = anim::add(m_lookAt, anim::scale(facing(headingDegrees + settings.angleDegrees), settings.distance));
}

void WinCamera::update(float seconds) {
    if (!m_orbiting) {
        return;
    }
    // A turn of the camera about the vertical through the look-at point.
    const float turn = m_settings.direction * m_settings.speedDegrees * seconds * kRadians;
    const float dx = m_position.x - m_lookAt.x;
    const float dy = m_position.y - m_lookAt.y;
    const float c = std::cos(turn);
    const float s = std::sin(turn);
    m_position.x = m_lookAt.x + (dx * c) - (dy * s);
    m_position.y = m_lookAt.y + (dx * s) + (dy * c);
}

CameraView WinCamera::view() const {
    return viewLookingAt(m_position, m_lookAt, m_settings.fieldOfView, kNearClip,
                         std::min(m_settings.farClip, kMaxFarClip));
}

} // namespace coney::camera
