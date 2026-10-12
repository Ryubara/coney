// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "core/game_timer.h"

namespace coney {

/// How the main loop turns real frames into simulation steps.
enum class FramePacing : std::uint8_t {
    /// One step and one render per frame, drawn at the newest state (alpha 1), whatever the real time. Test mode uses
    /// it with no clock at all, and `--fps-cap 30` uses it paced to 30 frames a second: the original's own rhythm
    /// (docs/research/graphics.md#frame-rate), where a slow frame slows the game down.
    Lockstep,
    /// As many fixed steps as the real time that passed calls for (0 to FrameClock::kMaxStepsPerFrame), and one render
    /// per frame that blends the last two steps by the leftover time: the game runs at the same speed at any display
    /// rate and moves smoothly above 30 frames a second.
    Interpolated,
};

/// What one real frame does: run `steps` fixed steps, then render once with `alpha`.
struct FramePlan {
    std::uint32_t steps = 0; ///< Fixed 1/30 s steps to run this frame.
    /// Where between the last two steps the render falls: 0 shows the state before the newest step, 1 the newest
    /// state. Always 1 in lockstep; in [0, 1) when interpolating.
    float alpha = 1.0F;
};

/// The main loop's frame clock: it is fed the real time each frame took and says how many fixed steps to run and how
/// far to blend the render. It never reads a clock itself: the platform measures the time (src/platform/frame_pacer.h)
/// and a test feeds made-up times, so the engine's test mode stays possible.
///
/// The simulation always advances in the original's fixed step of exactly GameTimer::kFixedStepTicks
/// (docs/research/boot.md#timers), whatever the display rate. The leftover real time is kept in whole units of a
/// millionth of a tick (a nanosecond is exactly 294,912 of them), so no rounding is ever lost: N seconds of real
/// time always give exactly 30 × N steps, however they are split into frames.
///
/// Two guards, both Coney's choices (docs/research/graphics.md#coneys-implementation):
/// - **No catching up past kMaxStepsPerFrame steps.** A frame is credited with at most kMaxStepsPerFrame steps of real
///   time; beyond that the time is dropped and the game slows down, as the original does when a frame runs long
///   (graphics.md#frame-rate), instead of running ever more steps per frame to catch up (the "spiral of death").
/// - **Gaps are not fast-forwarded.** The same cap means that after the window was dragged, the process was suspended
///   or a debugger stopped it, the game moves on by at most kMaxStepsPerFrame steps, not by the whole gap.
///
/// A debug time scale above 1x (setSpeed()) credits the real time times the speed and raises the cap with it.
///
/// Research: docs/research/graphics.md#frame-rate, docs/research/boot.md#the-main-loop
class FrameClock {
  public:
    /// The most steps one frame runs: 4, so the game keeps full speed down to 7.5 frames a second (the original's
    /// real-time clamp of 40 ms, 1.2 steps, is for its per-frame step; Coney's step is fixed, so it needs whole steps).
    static constexpr std::uint32_t kMaxStepsPerFrame = 4;
    /// Nanoseconds in a second.
    static constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;

    /// A clock with `pacing`. In Interpolated pacing the first frame runs one step at once, so the first render
    /// already has a mode to draw.
    explicit FrameClock(FramePacing pacing = FramePacing::Lockstep);

    /// The plan for a frame that took `elapsedNanoseconds` of real time since the previous one (ignored in lockstep).
    [[nodiscard]] FramePlan advance(std::uint64_t elapsedNanoseconds);

    /// The pacing now: the one chosen at construction, or the last setPacing().
    [[nodiscard]] FramePacing pacing() const { return m_pacing; }
    /// Changes the pacing between two frames (the debug menus' live frame cap: a cap of 30 is lockstep, any other
    /// interpolated). Real time not yet stepped is dropped, so the next frame starts the new pacing afresh.
    void setPacing(FramePacing pacing);

    /// The fastest speed setSpeed() takes, in percent: 10x.
    static constexpr std::uint32_t kMaxSpeedPercent = 1000;
    /// Makes steps due `percent` / 100 times as fast as real time calls for (clamped to 1 to kMaxSpeedPercent; 100,
    /// the default, is the game's own speed): the debug menus' time scale above 1x (src/debug/time_control.h). Every
    /// step is still exactly one fixed step; a faster game only runs more of them, and the per-frame cap grows with it
    /// (maxStepsPerFrame()). In lockstep a frame runs the speed's whole steps (150: 1, 2, 1, 2). Test mode never calls
    /// it, so it stays one step a frame. Coney's own (docs/guides/debug-menu.md#pages); the original has no time scale.
    void setSpeed(std::uint32_t percent);
    /// The speed in percent; 100 unless setSpeed() changed it.
    [[nodiscard]] std::uint32_t speed() const { return m_speed; }
    /// The most steps one interpolated frame runs at this speed: kMaxStepsPerFrame times the speed rounded up to whole
    /// multiples (16 at 400 %), so the game keeps its scaled speed down to 7.5 frames a second and a slower machine
    /// slows down instead of piling up steps.
    [[nodiscard]] std::uint32_t maxStepsPerFrame() const;

    /// Real time credited but not yet run as a step, in millionths of a tick of game time at the current speed
    /// (always less than one step).
    [[nodiscard]] std::uint64_t leftover() const { return m_accumulated / 100; }

  private:
    /// One step in the accumulator's unit, hundredths of a millionth of a tick: the hundredths make every whole-percent
    /// speed exact, so N seconds at speed p always give exactly 30 × N × p / 100 steps.
    static constexpr std::uint64_t kScaledStepUnits = GameTimer::kFixedStepTicks * 1'000'000 * 100;
    /// Millionths of a tick per nanosecond: 294,912,000 ticks a second × 10^6 / 10^9. Times the speed in percent, it
    /// is the accumulator's units per nanosecond.
    static constexpr std::uint64_t kUnitsPerNanosecond = GameTimer::kTicksPerSecond / 1000;

    FramePacing m_pacing;
    std::uint64_t m_accumulated;       // real time not yet stepped, in hundredths of a millionth of a tick
    std::uint32_t m_speed = 100;       // setSpeed(), percent
    std::uint32_t m_lockstepCarry = 0; // lockstep: hundredths of a step owed beyond the whole steps run
};

} // namespace coney
