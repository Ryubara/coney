// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace coney {

/// The rates one span of real time gave: what `--show-fps` prints and the debug menus' FPS counter shows.
struct FrameRateReading {
    double framesPerSecond = 0.0;   ///< Frames drawn a second.
    double frameMilliseconds = 0.0; ///< The average real time a frame took.
    double stepsPerSecond = 0.0;    ///< Fixed 1/30 s steps run a second: 30 while the game keeps full speed.
};

/// Averages real frame times over spans of at least a set length and keeps the last span's rates. It reads no clock:
/// the platform's frame pacer feeds it the time each frame took (src/platform/frame_pacer.h), and a test feeds made-up
/// times. It is only shown, never read by a step (docs/guides/conventions.md#update-and-render).
class FrameRateMeter {
  public:
    /// The span the debug menus' counter averages over: half a second, so it refreshes about twice a second.
    static constexpr std::uint64_t kHalfSecond = 500'000'000;
    /// A second: the span `--show-fps` prints.
    static constexpr std::uint64_t kSecond = 1'000'000'000;

    /// A meter publishing a reading each time the frames counted add up to `windowNanoseconds` (at least 1).
    explicit FrameRateMeter(std::uint64_t windowNanoseconds);

    /// Counts a frame of `nanoseconds` real time that ran `steps` fixed steps; a frame with no time (the first, which
    /// has nothing to be measured from) is not counted. Once the counted frames span the window, their rates replace
    /// the reading and the count starts again. Returns whether a new reading came out.
    bool add(std::uint64_t nanoseconds, std::uint32_t steps);

    /// The last span's rates; nothing before the first span ends.
    [[nodiscard]] const std::optional<FrameRateReading>& reading() const { return m_reading; }

  private:
    std::uint64_t m_window;
    std::uint64_t m_spanNanoseconds = 0; // the frames counted since the last reading
    std::uint64_t m_frames = 0;
    std::uint64_t m_steps = 0;
    std::optional<FrameRateReading> m_reading;
};

} // namespace coney
