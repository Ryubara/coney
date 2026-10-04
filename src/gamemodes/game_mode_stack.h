// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "core/game_timer.h"
#include "core/pads.h"
#include "gamemodes/game_mode.h"

namespace coney {

/// The stack of game modes. Its run loop is the game's main loop: every frame the mode on top runs once, and the
/// game ends when the stack is empty.
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

    /// One iteration of the main loop: enters the top mode if it has not been entered, runs its update with `frame`,
    /// and pops the top if the update returns ModeResult::Leave. Does nothing on an empty stack.
    void step(const FrameTime& frame);

    /// Runs the main loop until the stack is empty, `beginFrame` returns false (the window was closed) or
    /// `frameLimit` frames have run. Each frame calls `beginFrame` (when set), updates the pads (samplePads()),
    /// advances `timer` by one update and runs step() with the time it advanced. Returns the number of frames run.
    ///
    /// The loop does no pacing: the original paces itself by waiting for vertical sync in the frame's present
    /// (docs/research/boot.md#one-frame), which Coney's renderer will do; tests and `--frames` run flat out.
    /// @orig 0x0015e6b8 GameModeStack_RunUntilEmpty (unknown)
    std::uint64_t runUntilEmpty(GameTimer& timer, const std::function<bool()>& beginFrame,
                                std::optional<std::uint64_t> frameLimit);

    /// Sets where the pad samples come from: SDL devices, a script, or null for none (the records then stay
    /// disconnected). The source is not owned and must outlive its use by the stack.
    void setInput(InputSource* input) { m_input = input; }

    /// The pad records, which modes read their input from (`stack.pads().port(0)` in an update).
    ///
    /// The original's modes each call the pad update themselves (the legal screen, the memory-card check, the movie
    /// player, a task in a level: docs/research/frontend.md#input); Coney's loop does it once for every frame
    /// instead, so every mode sees the same fresh records, and a mode that ignores input (the legal screen) simply
    /// never reads them.
    [[nodiscard]] const Pads& pads() const { return m_pads; }

    /// Updates the pad records with the input source's sample for frame `frame` (FrameTime::index); does nothing
    /// without a source. runUntilEmpty() calls it each frame; a test that drives step() itself can call it too.
    void samplePads(std::uint64_t frame);

  private:
    std::vector<GameMode*> m_modes; ///< Bottom first; never holds null.
    Pads m_pads;
    InputSource* m_input = nullptr; // where samplePads() reads from; not owned
};

} // namespace coney
