// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Slow motion: an animation event on a player's clip shortens the characters' step to the slow-motion factor of
// 1/30 s, so humans, animation and the cameras' character-step logic run slower while frames keep their rate.
// Research: docs/research/camera.md#slow-motion

namespace coney::camera {

/// The characters' step under slow motion (`0x005102cc`).
class SlowMotion {
  public:
    /// The step without slow motion, seconds.
    static constexpr float kNormalStep = 1.0F / 30.0F;
    /// The animation event types that turn it on and off for the player whose clip carries them.
    static constexpr std::uint16_t kEventOn = 0x2e;
    static constexpr std::uint16_t kEventOff = 0x2f;
    /// The most players it marks.
    static constexpr int kPlayers = 2;

    /// `CfgFollowCamera`'s last argument (`0x005148a0`): the step's share of 1/30 s in slow motion.
    void setFactor(float factor) { m_factor = factor; }
    [[nodiscard]] float factor() const { return m_factor; }

    /// An animation event of `type` on `player`'s clip (0-based): kEventOn marks the player and sets the step to the
    /// factor of 1/30 s; kEventOff unmarks it and, when no player is marked, sets the step back. Other types and
    /// players do nothing.
    /// @orig 0x0041ab30 SlowMotion_On (unknown)
    /// @orig 0x0041ab60 SlowMotion_Off (unknown)
    void event(std::uint16_t type, int player);

    /// The characters' step now, seconds.
    [[nodiscard]] float stepSeconds() const { return m_step; }
    /// Whether some player has it on.
    [[nodiscard]] bool active() const { return m_marked != 0; }

  private:
    /// **Coney's choice** before `CfgFollowCamera` sets it: global.lua's 0.2, which every level passes.
    float m_factor = 0.2F;
    float m_step = kNormalStep;
    std::uint32_t m_marked = 0; // a bit per player
};

} // namespace coney::camera
