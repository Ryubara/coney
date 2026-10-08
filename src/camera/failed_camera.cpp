// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/failed_camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "raycast/collision_mesh.h"

namespace coney::camera {

namespace {

constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
// The upward ray's mask.
constexpr std::uint32_t kUpMask = 0x200U;

} // namespace

FailedCamera::FailedCamera(anim::Vec3 feet, const raycast::CollisionMesh* mesh, int yawStep) : m_lookAt(feet) {
    // The height: what of near + 5 m is clear straight up, less the near clip.
    const float length = kLens.nearClip + kHeight;
    float clear = length;
    if (mesh != nullptr) {
        const raycast::Ray up{.origin = raycast::Vec3{feet.x, feet.y, feet.z},
                              .direction = raycast::Vec3{0.0F, 0.0F, 1.0F},
                              .length = length};
        if (const auto hit = mesh->rayCast(up, {}, kUpMask)) {
            clear = hit->t;
        }
    }
    m_distance = clear - kLens.nearClip;
    const int step = ((yawStep % kYawSteps) + kYawSteps) % kYawSteps;
    m_rollDegrees = (static_cast<float>(step) * kYawStepDegrees) - 180.0F;
}

void FailedCamera::update(float seconds) {
    if (!m_rotating) {
        return;
    }
    m_rollDegrees += kTurnDegrees * seconds;
    if (m_rollDegrees > 180.0F) {
        m_rollDegrees -= 360.0F;
    }
    m_distance = std::max(kNearest, m_distance - (kSinkSpeed * seconds));
}

CameraView FailedCamera::view() const {
    // Pitched about its x axis from facing +y, then turned about the view direction.
    const float pitch = kPitchDegrees * kRadians;
    const anim::Vec3 forward{0.0F, std::cos(pitch), std::sin(pitch)};
    CameraView view;
    view.position = anim::Vec3{m_lookAt.x, m_lookAt.y, m_lookAt.z + m_distance};
    view.orientation = lookRotation(forward, m_rollDegrees * kRadians);
    view.lookAt = m_lookAt;
    view.fieldOfView = kLens.fieldOfView;
    view.nearClip = kLens.nearClip;
    view.farClip = kLens.farClip;
    return view;
}

} // namespace coney::camera
