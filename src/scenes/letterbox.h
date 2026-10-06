// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::scenes {

/// The letterbox a cinematic puts on every player's view: `ScreenQueueEffect` type 2 closes the bars over a time
/// (1.5 s, at once for a chained scene), type 3 opens them again (docs/research/scenes.md#starting).
///
/// **Coney's choices** (the page gives the effect types and times, not the bars' look): the bars close linearly in game
/// time, from nothing to kBarHeight of the screen's height each at the top and the bottom (a 16:9 picture inside the
/// 4:3 screen), and a new effect starts from where the bars are.
class Letterbox {
  public:
    /// The height of each bar, closed, as a fraction of the screen's height.
    static constexpr float kBarHeight = 0.125F;

    /// Starts closing (`in`) or opening the bars over `seconds` at game time `nowMs`; 0 s or less is at once.
    void start(bool in, float seconds, std::uint64_t nowMs);

    /// How far the bars are closed at game time `nowMs`: 0 open, 1 closed.
    [[nodiscard]] float amount(std::uint64_t nowMs) const;
    /// The height of each bar at `nowMs`, as a fraction of the screen's height.
    [[nodiscard]] float barHeight(std::uint64_t nowMs) const { return amount(nowMs) * kBarHeight; }

  private:
    float m_from = 0.0F;
    float m_to = 0.0F;
    std::uint64_t m_startMs = 0;
    std::uint64_t m_durationMs = 0;
};

} // namespace coney::scenes
