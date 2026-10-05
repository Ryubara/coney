// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/pad.h"
#include "world/view_frustum.h"
#include "world/world_streams.h"

namespace coney::world {

/// A free-flying camera for Coney's world viewer, driven by a pad. Coney's own tool: the original's cameras follow the
/// player. Its controls are Coney's choice (docs/guides/building.md#the-world-viewer):
///
/// - left stick (W A S D): fly forward, back and sideways along the view;
/// - right stick and d-pad (arrow keys): turn left and right, look up and down;
/// - L1 / R1 (Q / E): straight down / up;
/// - Cross held (K or Space): five times as fast.
///
/// It works in RenderWare's axes with y up, as the streamed worlds are (their ground faces +y). It reads no clock:
/// update() is given the step's length, so the same input gives the same path in test mode.
class DebugCamera {
  public:
    /// Flying speed in world units a second, without Cross.
    static constexpr float kSpeed = 20.0F;
    /// The factor Cross applies.
    static constexpr float kFastFactor = 5.0F;
    /// Turning speed in radians a second at full stick or with a d-pad direction held.
    static constexpr float kTurnRate = 1.6F;
    /// How far up or down it can look, in radians, short of straight up so the view never flips.
    static constexpr float kMaxPitch = 1.5F;

    /// A camera at `position` looking along +z, level, flying at `speed` world units a second (kSpeed by default; the
    /// sandbox flies slower, its world being metres at human scale).
    explicit DebugCamera(Vec3 position, float speed = kSpeed) : m_position(position), m_speed(speed) {}

    /// Moves and turns the camera by what `pad` holds, over `seconds`.
    void update(const Pad& pad, float seconds);

    /// Where it is and how it is turned.
    [[nodiscard]] CameraPose pose() const;

    /// Sets the heading (radians, 0 looking along +z, growing towards +x) and the pitch (radians, positive up,
    /// clamped to ±kMaxPitch).
    void setOrientation(float yaw, float pitch);

    [[nodiscard]] Vec3 position() const { return m_position; }
    [[nodiscard]] float yaw() const { return m_yaw; }
    [[nodiscard]] float pitch() const { return m_pitch; }

  private:
    Vec3 m_position;
    float m_speed = kSpeed;
    float m_yaw = 0.0F;
    float m_pitch = 0.0F;
};

} // namespace coney::world
