// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/pad.h"

namespace coney {

/// The two controller ports of a PS2.
inline constexpr std::size_t kPadPorts = 2;

/// One sample for each port, port 1 first: what an input source delivers once per step.
using PortSamples = std::array<PadSample, kPadPorts>;

/// Where pad samples come from: SDL gamepads and the keyboard in a normal run, a script in test mode. The engine asks
/// it once per step and never reads a device itself, so a test can replace it.
class InputSource {
  public:
    virtual ~InputSource() = default;
    InputSource() = default;
    InputSource(const InputSource&) = delete;
    InputSource& operator=(const InputSource&) = delete;
    InputSource(InputSource&&) = delete;
    InputSource& operator=(InputSource&&) = delete;

    /// The pads' state for frame `frame` (0-based, FrameTime::index). The loop asks for each frame once, in order.
    [[nodiscard]] virtual PortSamples sample(std::uint64_t frame) = 0;
};

/// One player's current camera as the pads update reads it to turn his pad's stick (`Camera_GetPlayerCamera`).
struct PlayerCameraFacts {
    /// Its orientation as a quaternion (x, y, z, w), the base camera's `+0x20` (vtable `+0x224`): its +y the view
    /// direction, +z up.
    std::array<float, 4> orientation{0.0F, 0.0F, 0.0F, 1.0F};
    /// The player slot the camera names (vtable `+0x27c`), whose kept orientation it is copied into when the views
    /// differ from the player cameras.
    std::size_t slot = 0;
};

/// One player's follow camera as the pads update reads it (`Cam_GetFollow(player, 0)`).
struct FollowCameraFacts {
    /// It is the player's base camera, the one returned to (`Camera_GetPlayerBase`).
    bool isBaseCamera = false;
    /// Its `+0x473`: the stick's frame is taken from the camera afresh (1), or the kept one reused (0).
    bool takesFrameAfresh = true;
    /// It is the player's current camera (`Camera_GetPlayerCamera`).
    bool isPlayerCamera = false;
    /// It looks behind the player (`+0x455`).
    bool lookingBehind = false;
};

/// What the pads update in play asks of the game about the players' cameras, to turn each pad's left stick by its
/// player's camera (Pads::turnSticks()). Player slots and record numbers, never pointers, so a networked game can
/// answer for each slot.
///
/// Research: docs/research/input.md#turned-stick
class StickTurnCameras {
  public:
    virtual ~StickTurnCameras() = default;
    StickTurnCameras() = default;
    StickTurnCameras(const StickTurnCameras&) = delete;
    StickTurnCameras& operator=(const StickTurnCameras&) = delete;
    StickTurnCameras(StickTurnCameras&&) = delete;
    StickTurnCameras& operator=(StickTurnCameras&&) = delete;

    /// The player pad record `record` belongs to (`+0x42`), or none.
    [[nodiscard]] virtual std::optional<std::size_t> playerOfRecord(std::size_t record) const = 0;
    /// Whether the views shown differ in number from the player cameras (`0x0050b198` != `0x0050b19c`).
    [[nodiscard]] virtual bool viewsDifferFromPlayers() const = 0;
    /// The player's current camera, or none.
    [[nodiscard]] virtual std::optional<PlayerCameraFacts> playerCamera(std::size_t player) const = 0;
    /// The player's follow camera, or none.
    [[nodiscard]] virtual std::optional<FollowCameraFacts> follow(std::size_t player) const = 0;
    /// The player's stick was found at rest: his follow camera takes its orientation afresh from now on (`+0x473` =
    /// 1). Called only for a player with a follow camera.
    virtual void stickAtRest(std::size_t player) = 0;
};

/// All the game's pad records: 8, of which only record 0 (port 1) and record 4 (port 2, first multitap slot) are ever
/// updated, as in the original. The others stay disconnected; they exist so record numbers match the original's.
///
/// Research: docs/research/frontend.md#pad-record
class Pads {
  public:
    /// How many records there are.
    static constexpr std::size_t kRecords = 8;

    /// The record a port's pad is read into: 0 for port 1, 4 for port 2 (CONEY_ASSERT for any other port).
    [[nodiscard]] static std::size_t recordOfPort(std::size_t port);

    /// How many players' kept orientations there are (`0x005ddc90`, one per player camera).
    static constexpr std::size_t kPlayers = 2;

    /// Updates the records of both ports from one sample each, each stick turned by the identity, as for a pad that
    /// belongs to no player: what the pads update does outside play. In play turnSticks() follows.
    /// Its part of the original: the loop over the two ports and the identity turn.
    /// @orig 0x001454a8 Pads_Update (unknown)
    void update(const PortSamples& samples);

    /// Turns both ports' left sticks by their players' cameras, as the pads update does in play (step 2 of the
    /// humans' update, before the characters move): each record's player's camera orientation, which is kept for
    /// him, or the kept one while his follow camera is his base camera and keeps its frame; player 0's camera for
    /// every pad while the views differ from the player cameras; the identity for a pad with no player. A stick at
    /// rest marks its player's follow camera to take its orientation afresh (StickTurnCameras::stickAtRest()).
    /// Its part of the original: the stick turn of each pad.
    /// @orig 0x001454a8 Pads_Update (unknown)
    void turnSticks(StickTurnCameras& cameras);

    /// The orientation kept for `player` (`0x005ddc90 + player × 0x10`, x, y, z, w; all 0 until a camera's is
    /// copied in), 0 to kPlayers - 1 (CONEY_ASSERT otherwise).
    [[nodiscard]] const std::array<float, 4>& keptOrientation(std::size_t player) const;

    /// Record `index`, 0 to kRecords - 1 (CONEY_ASSERT otherwise).
    [[nodiscard]] const Pad& record(std::size_t index) const;
    /// The record of `port` (0 for port 1, 1 for port 2).
    [[nodiscard]] const Pad& port(std::size_t port) const { return record(recordOfPort(port)); }
    /// How many records are connected: the main menu counts the eight records' connected words (`+0x4c`) to decide
    /// whether story asks for the number of players (docs/research/frontend.md#profile-manager).
    [[nodiscard]] std::size_t connectedCount() const;

  private:
    std::array<Pad, kRecords> m_records{};
    std::array<std::array<float, 4>, kPlayers> m_kept{}; // 0x005ddc90, one quaternion per player
};

} // namespace coney
