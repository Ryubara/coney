// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::scenes {

/// Coney's display setting for the letterbox (docs/guides/enhancements.md#where-the-settings-live): whether its bars
/// are drawn. Off, a cinematic plays full screen; the letterbox still runs (it hides the HUD and the spinning icons as
/// in the original), only its two black bars are left out. A render-side value: the simulation never reads it.
struct LetterboxSettings {
    bool drawn = true; ///< Draw the bars (the original's look, the default).
};

/// The program's one letterbox setting, which the debug menus' `Display/Cutscene letterbox` tunable edits.
[[nodiscard]] LetterboxSettings& letterboxSettings();

/// The letterbox a cinematic puts on every player's view: `ScreenQueueEffect` type 2 closes the bars over a time
/// (1.5 s, at once for a chained scene), type 3 opens them again (docs/research/scenes.md#starting).
///
/// The bars are two black quads, top and bottom, each the letterbox's level × 0.12 of the screen's height
/// (docs/research/graphics.md#screen-effects). **Coney's choices**: the level moves linearly in game time, and a new
/// effect starts from where the bars are.
class Letterbox {
  public:
    /// The height of each bar, closed, as a fraction of the screen's height.
    /// @orig 0x0018d5f8 ScreenFx_DrawLetterbox (ScreenEffectsManager.cpp)
    static constexpr float kBarHeight = 0.12F;

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
