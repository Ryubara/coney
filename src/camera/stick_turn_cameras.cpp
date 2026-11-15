// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/stick_turn_cameras.h"

#include <cstddef>
#include <optional>

#include "camera/cameras.h"
#include "camera/follow_camera.h"

namespace coney::camera {

PlayerStickCameras::PlayerStickCameras(FollowCamera& follow, const Cameras* cameras)
    : m_follow(&follow), m_cameras(cameras) {}

std::optional<std::size_t> PlayerStickCameras::playerOfRecord(std::size_t /*record*/) const { return std::nullopt; }

bool PlayerStickCameras::viewsDifferFromPlayers() const { return false; }

std::optional<PlayerCameraFacts> PlayerStickCameras::playerCamera(std::size_t /*player*/) const { return std::nullopt; }

std::optional<FollowCameraFacts> PlayerStickCameras::follow(std::size_t /*player*/) const { return std::nullopt; }

void PlayerStickCameras::stickAtRest(std::size_t /*player*/) {}

} // namespace coney::camera
