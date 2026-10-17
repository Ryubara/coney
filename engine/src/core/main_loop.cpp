#include "core/main_loop.hpp"

#include "core/frame_pacer.hpp"

#include <optional>

namespace coney::core {

namespace {

// Counts ticks and frames over windows of at least a second and logs their rates.
class RateReporter {
  public:
    explicit RateReporter(Logger& logger) : logger_(logger) {}

    // The first frame only starts the first window.
    void count(int ticks, Nanoseconds now) {
        if (!window_start_) {
            window_start_ = now;
            return;
        }
        ticks_ += ticks;
        ++frames_;
        const Nanoseconds span = now - *window_start_;
        if (span < std::chrono::seconds{1}) {
            return;
        }
        const double seconds = std::chrono::duration<double>(span).count();
        logger_.debug(log_category::kLoop, "{:.1f} ticks/s, {:.1f} frames/s", static_cast<double>(ticks_) / seconds,
                      static_cast<double>(frames_) / seconds);
        window_start_ = now;
        ticks_ = 0;
        frames_ = 0;
    }

  private:
    Logger& logger_;
    std::optional<Nanoseconds> window_start_;
    std::int64_t ticks_ = 0;
    std::int64_t frames_ = 0;
};

} // namespace

LoopSummary run_main_loop(LoopClient& client, const Clock& clock, const LoopSettings& settings, Logger& logger,
                          const LoopOptions& options) {
    FramePacer pacer(settings.ticks_per_second, settings.max_catch_up_ticks, clock.now());
    RateReporter rates(logger);
    LoopSummary summary;

    while (!options.max_frames || summary.frames < *options.max_frames) {
        if (!client.begin_frame()) {
            break;
        }
        const Nanoseconds now = clock.now();
        const FrameSteps steps = pacer.advance(now);
        if (steps.dropped > 0) {
            logger.warning(log_category::kLoop, "fell behind; dropped {} ticks", steps.dropped);
        }
        for (int i = 0; i < steps.ticks; ++i) {
            client.tick();
        }
        client.draw(steps.alpha);
        ++summary.frames;
        rates.count(steps.ticks, now);
    }

    summary.ticks = pacer.total_ticks();
    summary.dropped_ticks = pacer.total_dropped();
    return summary;
}

} // namespace coney::core
