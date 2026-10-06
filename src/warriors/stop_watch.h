// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace coney {

/// The mission stopwatch (`W_StopWatch`, the object at `*0x0051504c`): one countdown or count-up timer for the whole
/// game. `W_SetStopWatch` sets its time, target and callback; `W_StartStopWatch` runs or stops it; each frame of play
/// the time moves toward the target by the game time that passed, and on reaching it the watch stops and its callback
/// is due. Times are in milliseconds of the game timer, so a fixed-step test drives it exactly.
///
/// **Coney's stand-ins:** the rate (`+0x10`), whose writer is not traced, is 1. The warning beep (sound `0x0058b9f0`,
/// at most once a second inside the warning window while counting down) is reported by step() for the caller to play;
/// its exact test against `+0x40` and `+0x48` is Coney's reading of the page (the beep comes when the time is inside
/// the window and a second below the last beep).
///
/// Research: docs/research/scripting.md#stopwatch, docs/references/bindings/level.md#w_setstopwatch
class StopWatch {
  public:
    /// What one step did.
    struct Step {
        bool finished = false; ///< The time reached the target: the watch stopped and callback() is due.
        bool beep = false;     ///< A warning beep is due.
    };

    /// `W_SetStopWatch(time, target, callback)`: the current and target times, the callback's name; the warning
    /// window back to 0 and the last beep to 1000.
    /// @orig 0x004235f0 W_SetStopWatch (unknown)
    void set(std::int32_t timeMs, std::int32_t targetMs, std::string callback);
    /// `W_StartStopWatch(run)`: runs or stops the watch, taking the game timer's reading `nowMs` so a resumed watch
    /// does not count the time it was stopped.
    /// @orig 0x004233a8 StopWatch_Start (unknown)
    void start(bool run, std::uint64_t nowMs);
    /// `W_ShowStopWatch`'s part of the watch: the warning window `warnMs` (the beep's start is kept a second above it).
    void setWarning(std::int32_t warnMs);

    /// A frame of play at game time `nowMs`: while running, moves the time toward the target by the time passed since
    /// the last step, clamps it there on reaching or passing it and stops.
    /// @orig 0x004233f8 StopWatch_Update (unknown)
    Step step(std::uint64_t nowMs);

    /// `W_GetStopWatchTime()`: the current time.
    [[nodiscard]] std::int32_t time() const { return m_time; }
    /// The target time.
    [[nodiscard]] std::int32_t target() const { return m_target; }
    /// Whether the watch is running (`+0x14`).
    [[nodiscard]] bool running() const { return m_running; }
    /// The callback's name (`+0x20`); empty for none.
    [[nodiscard]] const std::string& callback() const { return m_callback; }

  private:
    std::uint64_t m_lastMs = 0;  // `+0x00`: the game timer at the last step
    std::int32_t m_time = 0;     // `+0x08`
    std::int32_t m_target = 0;   // `+0x0c`
    float m_rate = 1.0F;         // `+0x10`
    bool m_running = false;      // `+0x14`
    std::string m_callback;      // `+0x20`
    std::int32_t m_warning = 0;  // `+0x40`
    std::int32_t m_lastBeep = 0; // `+0x48`
};

} // namespace coney
