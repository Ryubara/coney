// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pads.h"

#include <array>
#include <cstddef>
#include <optional>

#include "core/assert.h"

namespace coney {

namespace {

// The turn a quaternion (x, y, z, w) gives the stick: the x and y of rows 0 and 1 of its rotation matrix, the camera's
// x and y axes seen from above. The quaternion is taken as it is, not normalised, as the original's VU0 code does; all
// 0 gives the identity.
// Its part of the original: rows 0 and 1, their x and y.
// @orig 0x00336500 Quat_ToMatrix (unknown)
StickTurn turnOf(const std::array<float, 4>& orientation) {
    const auto [x, y, z, w] = orientation;
    // Coney's choice: rows 2 and 3 and every row's z, which the stick turn never reads, are not built.
    StickTurn turn;
    turn.xAxis = {1.0F - (2.0F * ((y * y) + (z * z))), 2.0F * ((x * y) + (z * w))};
    turn.yAxis = {2.0F * ((x * y) - (z * w)), 1.0F - (2.0F * ((x * x) + (z * z)))};
    return turn;
}

} // namespace

std::size_t Pads::recordOfPort(std::size_t port) {
    CONEY_ASSERT(port < kPadPorts);
    // Each port has four multitap slots; the game reads only the first of each.
    return port * 4;
}

void Pads::update(const PortSamples& samples) {
    for (std::size_t port = 0; port < kPadPorts; ++port) {
        // Outside play no pad belongs to a player: the identity turns its stick (0x005117a0).
        m_records.at(recordOfPort(port)).update(samples.at(port), StickTurn{});
    }
}

void Pads::turnSticks(StickTurnCameras& cameras) {
    for (std::size_t port = 0; port < kPadPorts; ++port) {
        const std::size_t record = recordOfPort(port);
        const std::optional<std::size_t> player = cameras.playerOfRecord(record);
        // Whose camera turns the stick, and the slot its orientation is kept in: the pad's own player's, or player 0's
        // for every pad (and the slot that camera names) while the views shown differ from the player cameras.
        std::optional<std::size_t> cameraPlayer = player;
        std::optional<std::size_t> slot = player;
        if (cameras.viewsDifferFromPlayers()) {
            cameraPlayer = 0;
            if (const std::optional<PlayerCameraFacts> first = cameras.playerCamera(0)) {
                slot = first->slot;
            }
        }
        // The kept orientation is reused while the pad's player's follow camera is his base camera and keeps its
        // frame; otherwise his camera's is taken afresh and kept. No player: the identity.
        const std::optional<FollowCameraFacts> follow = player ? cameras.follow(*player) : std::nullopt;
        StickTurn turn;
        if (cameraPlayer) {
            if (follow && follow->isBaseCamera && !follow->takesFrameAfresh) {
                turn = turnOf(m_kept.at(*player));
            } else if (const std::optional<PlayerCameraFacts> camera = cameras.playerCamera(*cameraPlayer);
                       camera && slot) {
                m_kept.at(*slot) = camera->orientation;
                turn = turnOf(camera->orientation);
            }
            // **Stand-in**: a player with no camera gets the identity; the original passes its matrix as the previous
            // pad left it (docs/research/input.md#open-questions).
        }
        // What the record's step 2 asks of the pad's own player and his follow camera.
        turn.player = player;
        turn.hasFollow = follow.has_value();
        turn.followIsPlayerCamera = follow && follow->isPlayerCamera;
        turn.lookingBehind = follow && follow->lookingBehind;
        Pad& pad = m_records.at(record);
        pad.turnStick(turn);
        // A stick at rest: the follow camera takes its orientation afresh from the next update (+0x473 = 1).
        if (pad.followStickRest() && player) {
            cameras.stickAtRest(*player);
        }
    }
}

const std::array<float, 4>& Pads::keptOrientation(std::size_t player) const {
    CONEY_ASSERT(player < kPlayers);
    return m_kept.at(player);
}

const Pad& Pads::record(std::size_t index) const {
    CONEY_ASSERT(index < kRecords);
    return m_records.at(index);
}

std::size_t Pads::connectedCount() const {
    std::size_t count = 0;
    for (const Pad& pad : m_records) {
        count += pad.connected() ? 1 : 0;
    }
    return count;
}

} // namespace coney
