// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney {

/// The game clock. It counts in the original's ticks, the PS2 EE's cycle counter at 294.912 MHz, so that one frame
/// of the fixed step is the same whole number of ticks as in the original and game time never drifts by rounding.
///
/// It never reads a real clock: in fixed-step mode it advances by exactly 1/30 s per update, and in real-time mode by
/// whatever elapsed time its caller measured (the platform layer, or a test). That keeps the engine's test mode
/// possible.
///
/// Research: docs/research/boot.md#timers
class GameTimer {
  public:
    /// Ticks per second: the EE's cycle counter rate.
    static constexpr std::uint64_t kTicksPerSecond = 294'912'000;
    /// One fixed step, exactly 1/30 s.
    static constexpr std::uint64_t kFixedStepTicks = 0x960000;
    /// The most one real-time update may advance: 40 ms.
    static constexpr std::uint64_t kMaxRealStepTicks = 0xb40000;

    /// Starts at game time 0, in fixed-step mode, not paused.
    GameTimer() = default;

    /// Advances game time by one update. In fixed-step mode it moves exactly kFixedStepTicks and ignores
    /// `realElapsedTicks`; in real-time mode it moves `realElapsedTicks`, at most kMaxRealStepTicks. Paused, it does
    /// not move. Returns the ticks it advanced.
    ///
    /// The original also lifts the 40 ms clamp while the player's camera is in an unexplained "state 4"; Coney has
    /// no cameras yet. TODO(docs/research/boot.md#open-questions): what camera state 4 is.
    /// @orig 0x00145a10 GameTimer::Update (unknown)
    std::uint64_t update(std::uint64_t realElapsedTicks = 0);

    /// Chooses fixed-step (true, what every in-game frame uses) or real-time mode.
    void setFixedStep(bool fixedStep) { m_fixedStep = fixedStep; }
    /// Whether the timer is in fixed-step mode.
    [[nodiscard]] bool fixedStep() const { return m_fixedStep; }

    /// Stops or restarts game time.
    void setPaused(bool paused) { m_paused = paused; }
    /// Whether game time is stopped.
    [[nodiscard]] bool paused() const { return m_paused; }

    /// Game time in ticks.
    [[nodiscard]] std::uint64_t ticks() const { return m_ticks; }
    /// Game time in whole milliseconds (ticks / 294,912), as the original keeps it after every update.
    [[nodiscard]] std::uint64_t milliseconds() const { return m_ticks / (kTicksPerSecond / 1000); }

    /// Converts a tick count to seconds.
    [[nodiscard]] static double toSeconds(std::uint64_t ticks) {
        return static_cast<double>(ticks) / static_cast<double>(kTicksPerSecond);
    }

  private:
    std::uint64_t m_ticks = 0;
    bool m_fixedStep = true;
    bool m_paused = false;
};

} // namespace coney
