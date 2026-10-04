// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney {

class GameModeStack;

/// What a game mode's update asks of the stack.
enum class ModeResult : std::uint8_t {
    Stay,  ///< Keep the mode; it runs again next frame if it is still on top.
    Leave, ///< Pop the top of the stack.
};

/// The time one frame covers, handed to the mode on top.
struct FrameTime {
    std::uint64_t index = 0;     ///< Frames run before this one.
    double seconds = 0.0;        ///< The step: exactly 1/30 s under the fixed timestep.
    std::uint64_t gameTicks = 0; ///< Game time after this step, in GameTimer ticks.
};

/// A screen or state of the game (front end, in-game, memory card, error), run by a GameModeStack.
///
/// The stack calls enter() before a mode's first update and exit() when it is popped; suspend() when another mode is
/// pushed over it and resume() when it is on top again. update() runs one frame and says whether to stay. Every
/// method but id() and update() does nothing by default, as in the original's base class.
///
/// Unlike the original, update() is given the frame's time instead of reading a global timer, so the engine can run
/// on a fixed timestep with no real clock (test mode).
///
/// Research: docs/research/boot.md#game-mode
class GameMode {
  public:
    virtual ~GameMode() = default;
    GameMode() = default;
    GameMode(const GameMode&) = delete;
    GameMode& operator=(const GameMode&) = delete;
    GameMode(GameMode&&) = delete;
    GameMode& operator=(GameMode&&) = delete;

    /// The mode's id. The original's modes use 1 to 0x14 (docs/research/boot.md#game-mode); Coney's own tool modes
    /// use 0x100 and above so they never collide with one.
    [[nodiscard]] virtual std::uint32_t id() const = 0;

    /// Runs one frame. A mode may push and pop modes on `stack` from here. Returning ModeResult::Leave pops the top
    /// of the stack, which is this mode unless it pushed another one this frame (the original behaves the same).
    virtual ModeResult update(GameModeStack& stack, const FrameTime& frame) = 0;

    /// Called once before the first update after the mode is pushed.
    virtual void enter() {}
    /// Called when the mode is popped, if it was entered.
    virtual void exit() {}
    /// Called when the mode is on top again after the mode above it was popped.
    virtual void resume() {}
    /// Called when another mode is pushed on top of this one.
    virtual void suspend() {}

    /// Whether enter() has run and exit() has not: the original's state field (0 or 1).
    [[nodiscard]] bool entered() const { return m_entered; }

  private:
    friend class GameModeStack;
    bool m_entered = false;
};

} // namespace coney
