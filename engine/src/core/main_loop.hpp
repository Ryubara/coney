#pragma once

#include <cstdint>
#include <optional>

#include "core/clock.hpp"
#include "core/log.hpp"
#include "core/settings.hpp"

namespace coney::core {

// What the main loop drives. The platform, game and renderer plug in here.
class LoopClient {
  public:
    virtual ~LoopClient() = default;
    // Called first in every frame, before the clock is read: handle input and window events. False ends the loop.
    virtual bool begin_frame() = 0;
    // One fixed logic step.
    virtual void tick() = 0;
    // Draws the frame; `alpha` is how far real time is between the last tick and the next one, from 0 up to 1.
    virtual void draw(double alpha) = 0;
};

struct LoopOptions {
    std::optional<std::uint64_t> max_frames; // stop after this many drawn frames
};

struct LoopSummary {
    std::uint64_t frames = 0;
    std::int64_t ticks = 0;
    std::int64_t dropped_ticks = 0;
};

// Runs frames until the client asks to stop or max_frames is reached. Logs the tick and frame rates at debug level
// once a second of clock time, and a warning when a stall drops ticks.
LoopSummary run_main_loop(LoopClient& client, const Clock& clock, const LoopSettings& settings, Logger& logger,
                          const LoopOptions& options = {});

} // namespace coney::core
