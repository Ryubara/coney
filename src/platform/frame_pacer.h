// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "core/frame_rate_meter.h"

// No SDL type appears in this header, so code that holds a FramePacer stays platform-neutral and never includes SDL.

namespace coney::platform {

/// The main loop's real-time pacing: the **only** place in Coney that reads the real clock or sleeps
/// (docs/guides/conventions.md#update-and-render; a pre-commit check enforces it). It measures how long each frame
/// took, for core's FrameClock to turn into fixed steps, and holds frames to a cap with precise sleeps.
///
/// It is used only in a normal windowed run. Test mode (`--headless`, `--frames`, `--input-script`, `--screenshot`)
/// runs in lockstep and never makes one, so tests stay deterministic.
class FramePacer {
  public:
    /// Paces to at most `fpsCap` frames a second (0: no cap; vsync, if on, still limits the rate). With `report`, a
    /// line of frame and step rates is passed to it after each second of real time.
    explicit FramePacer(std::uint32_t fpsCap, std::function<void(std::string_view)> report = {});

    /// Waits until the next frame may start under the cap, then returns the real nanoseconds since the previous frame
    /// started (0 for the first). Frames are held to whole multiples of 1/cap s from a start time, so the rate does not
    /// drift; a frame that runs more than one period late starts the count again rather than rushing to catch up.
    [[nodiscard]] std::uint64_t waitForFrame();

    /// Counts a finished frame that ran `steps` fixed steps, with the time waitForFrame() last measured, for the rates.
    void endFrame(std::uint32_t steps);

    /// The cap, frames a second (0: none).
    [[nodiscard]] std::uint32_t cap() const { return m_cap; }
    /// Changes the cap from the next frame on (the debug menus' Display page); the count of capped frames starts again.
    void setCap(std::uint32_t fpsCap);

    /// The rates of the last half second, for the debug menus' FPS counter; nothing before the first half second.
    [[nodiscard]] const FrameRateMeter& meter() const { return m_display; }

    /// One line of totals since the pacer started: frames, steps, seconds and both rates.
    [[nodiscard]] std::string summary() const;

  private:
    std::uint32_t m_cap;
    std::function<void(std::string_view)> m_report;
    std::uint64_t m_lastStart = 0;   // when the previous frame started, in SDL nanoseconds; 0 before the first
    std::uint64_t m_lastElapsed = 0; // what waitForFrame() last returned
    std::uint64_t m_epoch = 0;       // the start of the current run of capped frames
    std::uint64_t m_capFrames = 0;   // frames since m_epoch
    std::uint64_t m_firstStart = 0;  // when the first frame started, for summary()
    std::uint64_t m_frames = 0;      // totals for summary()
    std::uint64_t m_steps = 0;
    FrameRateMeter m_reportMeter{FrameRateMeter::kSecond}; // the report's second
    FrameRateMeter m_display{FrameRateMeter::kHalfSecond}; // the FPS counter's half second
};

} // namespace coney::platform
