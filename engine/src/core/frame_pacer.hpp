#pragma once

#include <cstdint>

#include "core/clock.hpp"

namespace coney::core {

// What one frame has to do: run `ticks` logic steps, then draw with `alpha`, the fraction of a step that has
// elapsed since the last of them (0 inclusive to 1 exclusive). `dropped` counts steps skipped by the catch-up cap.
struct FrameSteps {
    int ticks = 0;
    std::int64_t dropped = 0;
    double alpha = 0.0;
};

// Splits real time into fixed logic steps of 1/ticks_per_second second, independently of how often frames are drawn.
// Time is kept exactly (in units of 1/(ticks_per_second * 10^9) second), so the tick count never drifts from
// elapsed time * rate. At most max_catch_up_ticks steps run in one frame; whole steps beyond that are dropped and
// the fraction of a step is kept.
class FramePacer {
  public:
    FramePacer(int ticks_per_second, int max_catch_up_ticks, Nanoseconds start);

    // Accounts for the time up to `now` and says what this frame runs. A clock that goes backwards counts as no time.
    [[nodiscard]] FrameSteps advance(Nanoseconds now);

    [[nodiscard]] int ticks_per_second() const noexcept { return rate_; }
    [[nodiscard]] Nanoseconds tick_length() const noexcept;
    [[nodiscard]] std::int64_t total_ticks() const noexcept { return total_ticks_; }
    [[nodiscard]] std::int64_t total_dropped() const noexcept { return total_dropped_; }

  private:
    int rate_;
    int max_catch_up_;
    Nanoseconds last_;
    std::int64_t pending_ = 0; // elapsed time not yet spent on ticks, in ns * rate
    std::int64_t total_ticks_ = 0;
    std::int64_t total_dropped_ = 0;
};

} // namespace coney::core
