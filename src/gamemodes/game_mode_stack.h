// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "core/frame_clock.h"
#include "core/game_timer.h"
#include "core/pads.h"
#include "gamemodes/game_mode.h"

namespace coney {

/// What the main loop calls each real frame, besides the modes. Every hook may be empty.
struct FrameHooks {
    /// Waits for the frame's start under any frame cap and returns the real nanoseconds since the previous frame
    /// began; the platform implements it (src/platform/frame_pacer.h). Empty in test mode: the loop then never asks
    /// for the time, which lockstep pacing does not need.
    std::function<std::uint64_t()> waitForFrame;
    /// Handles the window's events (which carry the keyboard and gamepad state); returns false once the user asked to
    /// quit.
    std::function<bool()> beginFrame;
    /// Called at the end of each frame with the number of steps it ran, for frame-rate statistics.
    std::function<void(std::uint32_t steps)> endFrame;
};

/// What a run of the main loop did.
struct LoopCounts {
    std::uint64_t frames = 0; ///< Real frames: each renders once.
    std::uint64_t steps = 0;  ///< Fixed 1/30 s simulation steps.
    std::uint64_t held = 0;   ///< Steps the step gate held (paused, slow motion): due, but not run.
};

/// The stack of game modes. Its run loop is the game's main loop: every simulation step the mode on top updates
/// once, every real frame the mode that updated last renders once, and the game ends when the stack is empty.
///
/// The stack does not own its modes (the original's are static objects); each must outlive its time on the stack.
///
/// Research: docs/research/boot.md#the-main-loop
class GameModeStack {
  public:
    /// Puts `mode` on top. The mode below, if it has been entered, is suspended first. The new mode is entered by the
    /// next step(), not here. Pushing a mode that is already on the stack is a programmer error (CONEY_ASSERT).
    /// @orig 0x0015e5e8 GameModeStack_Push (unknown)
    void push(GameMode& mode);

    /// Removes the top mode, calls its exit() if it was entered, then resume() on the new top if that one has been
    /// entered. Popping an empty stack is a programmer error (CONEY_ASSERT).
    /// @orig 0x0015e650 GameModeStack_Pop (unknown)
    void pop();

    /// The top mode, or nullptr when the stack is empty.
    /// @orig 0x0015e718 GameModeStack_Top (unknown)
    [[nodiscard]] GameMode* top() const;

    /// The id of the top mode, or 0 when the stack is empty (no mode has id 0).
    /// @orig 0x0015e748 GameModeStack_TopId (unknown)
    [[nodiscard]] std::uint32_t topId() const;

    /// Whether no mode is on the stack.
    [[nodiscard]] bool empty() const { return m_modes.empty(); }
    /// Number of modes on the stack.
    [[nodiscard]] std::size_t size() const { return m_modes.size(); }
    /// Whether `mode` is on the stack, at any depth.
    [[nodiscard]] bool contains(const GameMode& mode) const;

    /// One simulation step: enters the top mode if it has not been entered, runs its update with `frame`, and pops
    /// the top if the update returns ModeResult::Leave. Does nothing on an empty stack. Draws nothing: render() does.
    void step(const FrameTime& frame);

    /// Draws one frame: render() of the mode that ran the last step, if it is still on the stack (so the step a mode
    /// leaves on, or pushes another mode on, still shows that mode's picture, as the original's update drew it), else
    /// of the top mode if it has been entered; nothing on an empty stack or before any mode has run.
    void render(const RenderTime& time);

    /// The main loop: runs until the stack is empty, `hooks.beginFrame` returns false (the window was closed) or
    /// `stepLimit` steps have run. Each real frame it waits for the frame (`hooks.waitForFrame`), handles the window's
    /// events (`hooks.beginFrame`), asks `clock` how many steps the real time calls for, runs each step (sample the
    /// pads, advance `timer` by one update, update the top mode), then renders once with the clock's alpha (render()).
    ///
    /// A mode that leaves on a frame's last step is popped after that frame's render, so its last picture is shown;
    /// on any earlier step it is popped before the next one. Either way the next step sees the stack the original
    /// would.
    ///
    /// The limit counts **steps**, which is what `--frames N` means: in lockstep (test mode) every frame is one step
    /// and one render, so N frames are N steps and N renders whatever the machine's speed. Steps the step gate holds
    /// count towards the limit too, so a paused run still ends. Step indexes (FrameTime::index) carry on from one
    /// call to the next.
    /// @orig 0x0015e6b8 GameModeStack_RunUntilEmpty (unknown)
    LoopCounts runUntilEmpty(GameTimer& timer, FrameClock& clock, const FrameHooks& hooks,
                             std::optional<std::uint64_t> stepLimit);

    /// The loop in lockstep with no clock (test mode): one step and one render at alpha 1 per frame, `beginFrame` as
    /// FrameHooks::beginFrame. Returns the number of steps (= frames) run.
    std::uint64_t runUntilEmpty(GameTimer& timer, const std::function<bool()>& beginFrame,
                                std::optional<std::uint64_t> stepLimit);

    /// Sets what decides, for each step the clock calls for, whether it runs: the debug menus' time controls
    /// (src/debug/time_control.h). A step `gate` refuses still samples the pads, so the menus keep working, but the
    /// timer does not advance and no mode updates; the frame still renders, at alpha 1, so the game's last picture
    /// stays on screen. An empty `gate` runs every step.
    void setStepGate(std::function<bool()> gate) { m_stepGate = std::move(gate); }

    /// Sets where the pad samples come from: SDL devices, a script, or null for none (the records then stay
    /// disconnected). The source is not owned and must outlive its use by the stack.
    void setInput(InputSource* input) { m_input = input; }

    /// The pad records, which modes read their input from (`stack.pads().port(0)` in an update).
    ///
    /// The original's modes each call the pad update themselves (the legal screen, the memory-card check, the movie
    /// player, a task in a level: docs/research/frontend.md#input); Coney's loop does it once for every step
    /// instead, so every mode sees the same fresh records, and a mode that ignores input (the legal screen) simply
    /// never reads them. Sampling per step, not per real frame, keeps "pressed this step" seen exactly once.
    [[nodiscard]] const Pads& pads() const { return m_pads; }

    /// Updates the pad records with the input source's sample for step `frame` (FrameTime::index); does nothing
    /// without a source. runUntilEmpty() calls it each step; a test that drives step() itself can call it too.
    void samplePads(std::uint64_t frame);

  private:
    /// step() without the pop: a Leave is kept in m_popPending for finishPop().
    void updateTop(const FrameTime& frame);
    /// Pops the top if the last update asked to leave.
    void finishPop();

    std::vector<GameMode*> m_modes; ///< Bottom first; never holds null.
    Pads m_pads;
    InputSource* m_input = nullptr;    // where samplePads() reads from; not owned
    GameMode* m_lastUpdated = nullptr; // the mode the last step ran, while it is on the stack: what render() draws
    FrameTime m_lastStep;              // the last step's time, for the render's game time
    std::uint64_t m_steps = 0;         // steps runUntilEmpty() has run, over every call: the next FrameTime::index
    std::uint64_t m_samples = 0;       // pad samples taken, held steps included: the input source's index
    std::function<bool()> m_stepGate;  // whether a due step runs; empty: always
    bool m_holding = false;            // the gate held the last due step: render the newest step at alpha 1

    std::uint64_t m_renders = 0; // renders so far, for RenderTime::index
    bool m_popPending = false;   // the last update returned Leave and the pop has not happened yet
};

} // namespace coney
