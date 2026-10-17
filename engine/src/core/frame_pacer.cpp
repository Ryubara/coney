#include "core/frame_pacer.hpp"

#include <algorithm>
#include <stdexcept>

namespace coney::core {

namespace {
constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
// Longer gaps are cut to this before scaling, which keeps the arithmetic in range; any gap this long is far past
// the catch-up cap anyway.
constexpr std::int64_t kLongestGap = 3600 * kNanosPerSecond;
} // namespace

FramePacer::FramePacer(int ticks_per_second, int max_catch_up_ticks, Nanoseconds start)
    : rate_(ticks_per_second), max_catch_up_(max_catch_up_ticks), last_(start) {
    if (ticks_per_second <= 0) {
        throw std::invalid_argument("ticks_per_second must be positive");
    }
    if (max_catch_up_ticks <= 0) {
        throw std::invalid_argument("max_catch_up_ticks must be positive");
    }
}

Nanoseconds FramePacer::tick_length() const noexcept { return Nanoseconds{kNanosPerSecond / rate_}; }

FrameSteps FramePacer::advance(Nanoseconds now) {
    const std::int64_t gap = std::clamp<std::int64_t>((now - last_).count(), 0, kLongestGap);
    if (now > last_) {
        last_ = now;
    }
    pending_ += gap * rate_;

    FrameSteps steps;
    const std::int64_t due = pending_ / kNanosPerSecond;
    steps.ticks = static_cast<int>(std::min<std::int64_t>(due, max_catch_up_));
    steps.dropped = due - steps.ticks;
    pending_ %= kNanosPerSecond;
    steps.alpha = static_cast<double>(pending_) / static_cast<double>(kNanosPerSecond);

    total_ticks_ += steps.ticks;
    total_dropped_ += steps.dropped;
    return steps;
}

} // namespace coney::core
