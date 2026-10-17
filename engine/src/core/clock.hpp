#pragma once

#include <chrono>

namespace coney::core {

using Nanoseconds = std::chrono::nanoseconds;

// A monotonic time source. The main loop and the logger read time only through this, so tests can drive time by hand.
class Clock {
  public:
    virtual ~Clock() = default;
    // Time since an arbitrary fixed origin; never goes backwards.
    [[nodiscard]] virtual Nanoseconds now() const = 0;
};

// The host's monotonic clock.
class SteadyClock final : public Clock {
  public:
    [[nodiscard]] Nanoseconds now() const override {
        return std::chrono::duration_cast<Nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
    }
};

// A clock that only moves when told to.
class ManualClock final : public Clock {
  public:
    explicit ManualClock(Nanoseconds start = Nanoseconds{0}) : now_(start) {}

    [[nodiscard]] Nanoseconds now() const override { return now_; }
    void advance(Nanoseconds step) { now_ += step; }
    void set(Nanoseconds time) { now_ = time; }

  private:
    Nanoseconds now_;
};

} // namespace coney::core
