// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/orbit_camera.h"

#include <algorithm>
#include <cmath>

namespace coney::characters {

OrbitCamera::OrbitCamera(anim::Vec3 target, float distance, float yaw, float pitch)
    : m_target(target), m_distance(std::clamp(distance, kMinDistance, kMaxDistance)), m_yaw(yaw),
      m_pitch(std::clamp(pitch, -kMaxPitch, kMaxPitch)) {}

void OrbitCamera::update(const Pad& pad, float seconds) {
    // Circling: the right stick in proportion to its deflection, or the d-pad at the full rate.
    float turn = pad.rightX();
    float rise = pad.rightY();
    if (pad.held(pad::kLeft)) {
        turn -= 1.0F;
    }
    if (pad.held(pad::kRight)) {
        turn += 1.0F;
    }
    if (pad.held(pad::kUp)) {
        rise += 1.0F;
    }
    if (pad.held(pad::kDown)) {
        rise -= 1.0F;
    }
    // Pushing right swings the camera towards its own right around the target: a larger yaw (the offset's derivative
    // by yaw is the pose's `right`).
    constexpr float kTurn = 6.28318530718F;
    m_yaw = std::remainder(m_yaw + std::clamp(turn, -1.0F, 1.0F) * kTurnRate * seconds, kTurn);
    m_pitch = std::clamp(m_pitch + std::clamp(rise, -1.0F, 1.0F) * kTurnRate * seconds, -kMaxPitch, kMaxPitch);

    // In and out: the left stick's y in proportion, R1 in and L1 out at the full rate; the distance changes by a
    // factor, so the speed feels the same near and far.
    float zoom = pad.leftY();
    if (pad.held(pad::kR1)) {
        zoom += 1.0F;
    }
    if (pad.held(pad::kL1)) {
        zoom -= 1.0F;
    }
    m_distance = std::clamp(m_distance * std::exp(-std::clamp(zoom, -1.0F, 1.0F) * kZoomRate * seconds), kMinDistance,
                            kMaxDistance);
}

OrbitPose OrbitCamera::pose() const {
    const float cp = std::cos(m_pitch);
    const anim::Vec3 offset{cp * std::cos(m_yaw), cp * std::sin(m_yaw), std::sin(m_pitch)};
    OrbitPose pose;
    pose.position = anim::add(m_target, anim::scale(offset, m_distance));
    pose.forward = anim::scale(offset, -1.0F);
    // Up is z made square to the view; never parallel to it, as the pitch stays short of ±90 degrees.
    const anim::Vec3 worldUp{0.0F, 0.0F, 1.0F};
    pose.up = anim::normalise(anim::subtract(worldUp, anim::scale(pose.forward, anim::dot(worldUp, pose.forward))));
    pose.right = anim::cross(pose.forward, pose.up);
    return pose;
}

} // namespace coney::characters
