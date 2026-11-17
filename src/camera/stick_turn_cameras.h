// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "core/pads.h"

// Player 1's cameras as the pads update reads them to turn the left stick. Research:
// docs/research/input.md#turned-stick

namespace coney::camera {

class Cameras;
class FollowCamera;

/// Player 1's cameras for the pads update (Pads::turnSticks()): record 0 is his pad, his current camera is the
/// manager's current view (the follow camera's own view without a manager), and the follow camera keeps the `+0x473`
/// flag a stick at rest sets. Coney has one player and one view, so the views never differ from the player cameras.
class PlayerStickCameras final : public StickTurnCameras {
  public:
    /// Player 1's cameras: his follow camera and the manager it belongs to (null: the follow camera alone). Both must
    /// outlive this.
    PlayerStickCameras(FollowCamera& follow, const Cameras* cameras);

    /// 0 for record 0 (port 1), none for the others.
    /// @coney-only Coney's one player is bound to port 1's record (Player_BindPad is not ported)
    [[nodiscard]] std::optional<std::size_t> playerOfRecord(std::size_t record) const override;
    /// Never: one view, one player camera.
    /// @coney-only Coney shows one view for one player (0x0050b198 = 0x0050b19c = 1)
    [[nodiscard]] bool viewsDifferFromPlayers() const override;
    /// Player 0's current view's orientation, slot 0; none for any other player.
    /// @coney-only a lookup of the view Camera_GetPlayerCamera (0x0011f9b0) names
    [[nodiscard]] std::optional<PlayerCameraFacts> playerCamera(std::size_t player) const override;
    /// Player 0's follow camera; none for any other player.
    /// @coney-only a lookup of what Cam_GetFollow (0x0011f9e0) names
    [[nodiscard]] std::optional<FollowCameraFacts> follow(std::size_t player) const override;
    /// Sets player 0's follow camera's flag (FollowCamera::setStickFrameAfresh()).
    /// @coney-only a forwarder for Pad_Update's write of 1 to +0x473 (0x00144fb0)
    void stickAtRest(std::size_t player) override;

  private:
    FollowCamera* m_follow;
    const Cameras* m_cameras;
};

} // namespace coney::camera
