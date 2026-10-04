// SPDX-License-Identifier: GPL-3.0-or-later
#include "world/debug_camera.h"

#include <algorithm>
#include <cmath>

namespace coney::world {

void DebugCamera::update(const Pad& pad, float seconds) {
    // Turning: the right stick, or the d-pad at full rate.
    float turn = pad.rightX();
    float look = pad.rightY();
    if (pad.held(pad::kLeft)) {
        turn -= 1.0F;
    }
    if (pad.held(pad::kRight)) {
        turn += 1.0F;
    }
    if (pad.held(pad::kUp)) {
        look += 1.0F;
    }
    if (pad.held(pad::kDown)) {
        look -= 1.0F;
    }
    // Turning right moves the view towards `right`, which is a smaller yaw (yaw grows towards +x, the screen's left).
    setOrientation(m_yaw - std::clamp(turn, -1.0F, 1.0F) * kTurnRate * seconds,
                   m_pitch + std::clamp(look, -1.0F, 1.0F) * kTurnRate * seconds);

    // Moving: the left stick along the view and across it, L1 and R1 straight down and up.
    const CameraPose view = pose();
    const float speed = kSpeed * (pad.held(pad::kCross) ? kFastFactor : 1.0F) * seconds;
    const float ahead = pad.leftY() * speed;
    const float aside = pad.leftX() * speed;
    float rise = 0.0F;
    if (pad.held(pad::kR1)) {
        rise += speed;
    }
    if (pad.held(pad::kL1)) {
        rise -= speed;
    }
    m_position.x += view.forward.x * ahead + view.right.x * aside;
    m_position.y += view.forward.y * ahead + view.right.y * aside + rise;
    m_position.z += view.forward.z * ahead + view.right.z * aside;
}

CameraPose DebugCamera::pose() const {
    // Heading about +y, then pitch; right = forward × up keeps the frame right-handed as RenderWare's are, which puts
    // +x on the screen's left when looking along +z.
    const float cy = std::cos(m_yaw);
    const float sy = std::sin(m_yaw);
    const float cp = std::cos(m_pitch);
    const float sp = std::sin(m_pitch);
    CameraPose pose;
    pose.position = m_position;
    pose.forward = Vec3{sy * cp, sp, cy * cp};
    pose.right = Vec3{-cy, 0.0F, sy};
    pose.up = Vec3{-sy * sp, cp, -cy * sp};
    return pose;
}

void DebugCamera::setOrientation(float yaw, float pitch) {
    constexpr float kTurn = 6.28318530718F;
    m_yaw = std::remainder(yaw, kTurn);
    m_pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

} // namespace coney::world
