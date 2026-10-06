// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/camera_blend.h"

#include <algorithm>

namespace coney::camera {

CameraBlend::CameraBlend(const CameraView& from, float seconds)
    : m_from(from), m_seconds(seconds), m_farClip(from.farClip) {}

float CameraBlend::progress() const { return m_seconds > 0.0F ? std::min(m_elapsed / m_seconds, 1.0F) : 1.0F; }

CameraView CameraBlend::step(const CameraView& to, float dt) {
    m_elapsed += dt;
    const float t = progress();
    // Linear in time, no easing: the points lerped, the orientation slerped from the start view's.
    m_farClip = std::min(m_farClip, m_from.farClip + (to.farClip - m_from.farClip) * t);
    return CameraView{.position = anim::lerp(m_from.position, to.position, t),
                      .orientation = anim::slerp(m_from.orientation, to.orientation, t),
                      .lookAt = anim::lerp(m_from.lookAt, to.lookAt, t),
                      .fieldOfView = to.fieldOfView,
                      .nearClip = to.nearClip,
                      .farClip = m_farClip};
}

} // namespace coney::camera
