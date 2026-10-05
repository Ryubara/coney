// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::debug {

/// Pause, single step and slow motion for the debug menus, without ever changing the length of a step.
///
/// The game always advances by whole fixed 1/30 s steps (src/core/game_timer.h), as many as real time calls for (the
/// frame clock, src/core/frame_clock.h). Slow motion runs fewer of them: at a divisor of 4 one due step in four runs,
/// so the game moves at a quarter of its speed and every step is still exactly 1/30 s. Paused, no step runs except
/// those asked for with stepOnce(). A run with the same input and the same time controls is therefore the same run.
///
/// Coney's own tool (docs/research/debug.md#not-present).
class TimeControl {
  public:
    /// The largest slow-motion divisor: one step in every 30 due.
    static constexpr int kMaxDivisor = 30;

    /// Stops or restarts the steps.
    void setPaused(bool paused) { m_paused = paused; }
    /// Whether the steps are stopped.
    [[nodiscard]] bool paused() const { return m_paused; }
    /// Asks for one more step while paused (they add up); does nothing while running.
    void stepOnce();
    /// Steps asked for and not yet run.
    [[nodiscard]] int queuedSteps() const { return m_queued; }

    /// Runs one step in every `divisor` due, clamped to 1 (full speed) to kMaxDivisor.
    void setSlowMotion(int divisor);
    /// The slow-motion divisor; 1 at full speed.
    [[nodiscard]] int slowMotion() const { return m_divisor; }

    /// Asked once per due step (the game-mode stack's step gate): whether it runs. Counts due steps and steps run.
    [[nodiscard]] bool shouldStep();

    /// Due steps seen, run or held.
    [[nodiscard]] std::uint64_t frames() const { return m_frames; }
    /// Steps let through.
    [[nodiscard]] std::uint64_t steps() const { return m_steps; }

  private:
    bool m_paused = false;
    int m_queued = 0;
    int m_divisor = 1;
    int m_phase = 0; // due steps since the last step run in slow motion
    std::uint64_t m_frames = 0;
    std::uint64_t m_steps = 0;
};

} // namespace coney::debug
