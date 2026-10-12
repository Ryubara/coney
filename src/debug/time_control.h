// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace coney::debug {

/// Pause, single step and the time scale for the debug menus, without ever changing the length of a step.
///
/// The game always advances by whole fixed 1/30 s steps (src/core/game_timer.h), as many as real time calls for (the
/// frame clock, src/core/frame_clock.h). The time scale changes how many of them run, never their length:
/// - **Below 1x** the steps stay due 30 times a real second, so the pads (and the menus) are still read every 1/30 s,
///   and only that share of them runs: at 0.25x one due step in four. The share is counted in hundredths, so a
///   scale gives exactly its steps over a run.
/// - **Above 1x** every due step runs, and the main loop has the frame clock make more of them due
///   (dueSpeedPercent(), FrameClock::setSpeed()): at 2x, 60 steps a real second. Test mode never speeds the clock up,
///   so it stays one step a frame.
/// Paused, no step runs except those asked for with stepOnce(). A run with the same input and the same time controls
/// is therefore the same run.
///
/// Coney's own tool (docs/research/debug.md#not-present, docs/guides/debug-menu.md#pages).
class TimeControl {
  public:
    /// The slowest scale, in hundredths: 0.05x, one step in twenty.
    static constexpr int kMinScalePercent = 5;
    /// The fastest scale, in hundredths: 4x.
    static constexpr int kMaxScalePercent = 400;

    /// Stops or restarts the steps.
    void setPaused(bool paused) { m_paused = paused; }
    /// Whether the steps are stopped.
    [[nodiscard]] bool paused() const { return m_paused; }
    /// Asks for one more step while paused (they add up); does nothing while running.
    void stepOnce();
    /// Steps asked for and not yet run.
    [[nodiscard]] int queuedSteps() const { return m_queued; }

    /// Sets the time scale (1 is the game's own speed), rounded to hundredths and clamped to kMinScalePercent to
    /// kMaxScalePercent. The first due step after a change runs at once.
    void setScale(double scale);
    /// Sets the time scale in hundredths (100 is 1x), clamped as setScale().
    void setScalePercent(int percent);
    /// The time scale; 1 at the game's own speed.
    [[nodiscard]] double scale() const { return m_percent / 100.0; }
    /// The time scale in hundredths.
    [[nodiscard]] int scalePercent() const { return m_percent; }
    /// How fast the frame clock should make steps due, in percent (FrameClock::setSpeed()): the scale above 1x, else
    /// 100, since a slower scale holds due steps instead.
    [[nodiscard]] std::uint32_t dueSpeedPercent() const;

    /// Asked once per due step (the game-mode stack's step gate): whether it runs. Counts due steps and steps run.
    [[nodiscard]] bool shouldStep();

    /// Due steps seen, run or held.
    [[nodiscard]] std::uint64_t frames() const { return m_frames; }
    /// Steps let through.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }

  private:
    bool m_paused = false;
    int m_queued = 0;
    int m_percent = 100;
    int m_credit = 0; // below 1x: hundredths of a step owed; a step runs when it reaches 100
    std::uint64_t m_frames = 0;
    std::uint64_t m_steps = 0;
};

/// The time scale as the menus show it: `0.25x`, `1x`, `2.5x`.
[[nodiscard]] std::string formatTimeScale(int percent);

} // namespace coney::debug
