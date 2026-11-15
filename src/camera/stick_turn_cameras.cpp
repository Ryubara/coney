// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/stick_turn_cameras.h"

#include <cstddef>
#include <optional>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"

namespace coney::camera {

PlayerStickCameras::PlayerStickCameras(FollowCamera& follow, const Cameras* cameras)
    : m_follow(&follow), m_cameras(cameras) {}

std::optional<std::size_t> PlayerStickCameras::playerOfRecord(std::size_t record) const {
    if (record == Pads::recordOfPort(0)) {
        return 0;
    }
    return std::nullopt;
}

bool PlayerStickCameras::viewsDifferFromPlayers() const { return false; }

std::optional<PlayerCameraFacts> PlayerStickCameras::playerCamera(std::size_t player) const {
    if (player != 0) {
        return std::nullopt;
    }
    // The manager's current view (a blend's, a scene's, a locked camera's), or the follow camera's own direction.
    const anim::Quat orientation =
        m_cameras != nullptr ? m_cameras->view().orientation : lookRotation(m_follow->forward());
    return PlayerCameraFacts{.orientation = {orientation.x, orientation.y, orientation.z, orientation.w}, .slot = 0};
}

std::optional<FollowCameraFacts> PlayerStickCameras::follow(std::size_t player) const {
    if (player != 0) {
        return std::nullopt;
    }
    // **Coney's reading**: the follow camera is the base camera the others return to, and none looks behind (Coney has
    // no reverse camera yet).
    const bool current = m_cameras == nullptr || m_cameras->current().kind == CameraKind::Follow;
    return FollowCameraFacts{.isBaseCamera = true,
                             .takesFrameAfresh = m_follow->takesStickFrameAfresh(),
                             .isPlayerCamera = current,
                             .lookingBehind = false};
}

void PlayerStickCameras::stickAtRest(std::size_t player) {
    if (player == 0) {
        m_follow->setStickFrameAfresh(true);
    }
}

} // namespace coney::camera
