// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/fixed_camera.h"

#include <algorithm>

namespace coney::camera {

FixedCamera::FixedCamera(anim::Vec3 position, anim::Vec3 offset, float fieldOfView, float nearClip, float farClip)
    : m_position(position), m_offset(offset), m_lookAt{position.x, position.y + 1.0F, position.z},
      m_fieldOfView(fieldOfView), m_nearClip(nearClip), m_farClip(std::min(farClip, kMaxFarClip)) {}

void FixedCamera::update(std::span<const anim::Vec3> targets) {
    if (targets.empty()) {
        return;
    }
    anim::Vec3 sum;
    for (const anim::Vec3& target : targets) {
        sum = anim::add(sum, target);
    }
    m_lookAt = anim::add(anim::scale(sum, 1.0F / static_cast<float>(targets.size())), m_offset);
}

CameraView FixedCamera::view() const {
    return viewLookingAt(m_position, m_lookAt, m_fieldOfView, m_nearClip, m_farClip);
}

void FixedCamera::setClipping(float nearClip, float farClip) {
    m_nearClip = nearClip;
    m_farClip = std::min(farClip, kMaxFarClip);
}

} // namespace coney::camera
