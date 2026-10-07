// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "effects/ground_fog.h"

namespace coney::effects {

/// `StartRoomSmoke(colour, amount)`'s settings (docs/references/bindings/effects.md#startroomsmoke).
struct RoomSmokeSettings {
    std::array<std::uint8_t, 3> tint{}; ///< The haze's `{r, g, b}`.
    std::uint8_t lowestAlpha = 0;       ///< A drift's alpha is picked from lowestAlpha to highestAlpha.
    std::uint8_t highestAlpha = 0;
    float amount = 0.0F; ///< Scales a drift's sideways scroll.
};

/// One drift of the haze: the target the smoke blends toward.
struct SmokeDrift {
    float scroll = 0.0F;      ///< U per update, 0.0005-0.0015 × amount, either way.
    float widthScale = 1.0F;  ///< 1.63-1.93.
    float heightScale = 1.0F; ///< 1.0-1.2.
    float alpha = 0.0F;       ///< A whole number from the lowest to the highest alpha.
};

/// What the smoke draws this frame: one sprite of `room_smoke_overlay` across the screen.
struct SmokeSprite {
    float guiY = 0.2F;                    ///< The sprite's centre's GUI `y` (its `x` is 0.5).
    float width = 0.0F;                   ///< Overlay units (the screen is 1.595 wide).
    float height = 0.0F;                  ///< Overlay units (the screen is 1.1 high).
    float u = 0.0F;                       ///< The texture's U offset, 0-1; the sprite shows (u, -0.1)-(u + 1, 0.9).
    std::array<std::uint8_t, 4> colour{}; ///< The tint, with the drift's alpha.
};

/// The room-smoke overlay (screen effect layer 3, `OE_RoomSmoke`): a sprite of the wrapping `room_smoke_overlay`
/// texture over the whole screen that scrolls sideways, rides up and down with the camera's tilt, slides as the camera
/// turns, and blends without rest along a chain of random drifts (scroll, size and alpha), each reached in 4-20 s.
///
/// **Coney's readings**: the original blends on the real-time clock and ticks on game time; Coney's fixed step drives
/// both, so they agree at 30 steps a second. The heading is the look vector's angle from +x toward +y (the page gives
/// no convention), and the first tick takes the heading as it is (no slide). The generator is the smoke's own
/// xorshift32, so a run is the same every time.
///
/// Research: docs/research/graphics.md#room-smoke
class RoomSmoke {
  public:
    /// The overlay ticks at most this many times a second of game time (`+0x10` = 30).
    static constexpr float kTicksPerSecond = 30.0F;
    /// A drift's blend time, ms.
    static constexpr std::uint32_t kMinBlendMs = 4000;
    static constexpr std::uint32_t kMaxBlendMs = 20000;

    explicit RoomSmoke(std::uint32_t seed = 0x2545F491U) : m_random(seed != 0 ? seed : 1U) {}

    /// `StartRoomSmoke`: a stopped smoke starts with `settings` (a random U offset, a first drift as both "from" and
    /// "now", a second as "to"); a running one takes the new settings (`0x0019aff8`) and jumps to a new "to" at its
    /// next tick.
    /// @orig 0x0019add0 OE_RoomSmoke_Construct (OE_RoomSmoke.cpp)
    void start(const RoomSmokeSettings& settings);
    /// `EndRoomSmoke`: the overlay is gone.
    void stop() { m_on = false; }
    /// Whether the overlay is on.
    [[nodiscard]] bool running() const { return m_on; }

    /// One step of `seconds` of game time, seen from `viewer` (none: no tick, as the original waits for its view's
    /// camera): at most kTicksPerSecond ticks a second, each blending the drift, following the camera and moving the
    /// sprite.
    /// @orig 0x0019b4c0 OE_RoomSmoke_Update (OE_RoomSmoke.cpp)
    void step(float seconds, const std::optional<EffectsViewer>& viewer);

    /// The sprite to draw while running().
    [[nodiscard]] const SmokeSprite& sprite() const { return m_sprite; }
    /// The drift blended to now.
    [[nodiscard]] const SmokeDrift& drift() const { return m_now; }
    /// The settings.
    [[nodiscard]] const RoomSmokeSettings& settings() const { return m_settings; }

  private:
    // A number in [0, 1) from the generator.
    float unit();
    // A new random drift and its blend time.
    // @orig 0x0019b198 OE_RoomSmoke_PickDrift (OE_RoomSmoke.cpp)
    SmokeDrift pickDrift();
    // The drift from "from" to "to" by the share of the blend time gone; at its end the next "to" is picked.
    // @orig 0x0019b2a8 OE_RoomSmoke_BlendDrift (OE_RoomSmoke.cpp)
    void blend();
    // The tilt to the sprite's height and the turn to an extra scroll this tick.
    // @orig 0x0019b070 OE_RoomSmoke_FollowCamera (OE_RoomSmoke.cpp)
    float followCamera(const EffectsViewer& viewer);

    RoomSmokeSettings m_settings;
    SmokeDrift m_from;
    SmokeDrift m_now;
    SmokeDrift m_to;
    SmokeSprite m_sprite;
    float m_clockMs = 0.0F;         // game time since the start
    float m_lastTickMs = 0.0F;      // the last tick's game time
    float m_blendStartMs = 0.0F;    // +0xe0
    float m_blendMs = 1.0F;         // +0xe4
    std::optional<float> m_heading; // the view's last heading, 0-0.9999 (+0x20)
    bool m_on = false;
    std::uint32_t m_random;
};

} // namespace coney::effects
