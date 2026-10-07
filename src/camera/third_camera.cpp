// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/third_camera.h"

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

// `degrees` wrapped into [-180, 180).
float wrap(float degrees) { return degrees - (360.0F * std::floor((degrees + 180.0F) / 360.0F)); }

} // namespace

ThirdCamera::ThirdCamera(const ThirdCameraSettings& settings) : m_settings(settings) {
    m_settings.farClip = std::min(settings.farClip, kMaxFarClip);
}

void ThirdCamera::update(anim::Vec3 feet, float headingDegrees) {
    // The look-at point: the offset turned into the target's frame.
    const anim::Vec3 ahead = facing(headingDegrees);
    const anim::Vec3 right{ahead.y, -ahead.x, 0.0F};
    const anim::Vec3 offset =
        anim::add(anim::scale(right, m_settings.offset.x), anim::scale(ahead, m_settings.offset.y));
    m_lookAt = anim::add(feet, anim::Vec3{offset.x, offset.y, m_settings.offset.z});
    // The facing eased toward the target's the short way round.
    m_heading = m_started ? wrap(m_heading + (kTurnShare * wrap(headingDegrees - m_heading))) : wrap(headingDegrees);
    m_started = true;
    const anim::Vec3 back = anim::scale(facing(m_heading + m_settings.angleDegrees), -m_settings.distance);
    m_position = anim::add(m_lookAt, anim::Vec3{back.x, back.y, m_settings.height});
}

CameraView ThirdCamera::view() const {
    return viewLookingAt(m_position, m_lookAt, m_settings.fieldOfView, m_settings.nearClip, m_settings.farClip);
}

void ThirdCamera::setClipping(float nearClip, float farClip) {
    m_settings.nearClip = nearClip;
    m_settings.farClip = std::min(farClip, kMaxFarClip);
}

} // namespace coney::camera
