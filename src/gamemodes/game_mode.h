// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney {

class GameModeStack;

/// What a game mode's update asks of the stack.
enum class ModeResult : std::uint8_t {
    Stay,  ///< Keep the mode; it runs again next step if it is still on top.
    Leave, ///< Pop the top of the stack.
};

/// The time one simulation step covers, handed to the mode on top. Always exactly one fixed step of 1/30 s.
struct FrameTime {
    std::uint64_t index = 0;     ///< Steps run before this one.
    double seconds = 0.0;        ///< The step: exactly 1/30 s under the fixed timestep.
    std::uint64_t gameTicks = 0; ///< Game time after this step, in GameTimer ticks.
    std::uint64_t stepTicks = 0; ///< Ticks this step advanced: GameTimer::kFixedStepTicks under the fixed step.
};

/// What one render is given: how far between the last two steps to draw, and the game time that stands for.
struct RenderTime {
    /// Where between the last two steps the frame falls: 0 is the state before the newest step, 1 the newest state.
    /// Exactly 1 in lockstep (test mode, `--fps-cap 30`), so a render then draws the newest state unblended.
    float alpha = 1.0F;
    /// The game time the frame shows, in GameTimer ticks: the newest step's time less (1 - alpha) of a step. Equal to
    /// the newest FrameTime::gameTicks when alpha is 1.
    std::uint64_t gameTicks = 0;
    std::uint64_t index = 0; ///< Renders before this one.
};

/// A screen or state of the game (front end, in-game, memory card, error), run by a GameModeStack.
///
/// The stack calls enter() before a mode's first update and exit() when it is popped; suspend() when another mode is
/// pushed over it and resume() when it is on top again. update() runs one simulation step and says whether to stay;
/// render() draws a frame. Every method but id() and update() does nothing by default, as in the original's base class
/// (which has no render: there, each mode's update draws and presents its own frame).
///
/// **Update and render are apart** (docs/guides/conventions.md#update-and-render), so the game runs at the original's
/// fixed 1/30 s step at any display rate: update() runs 0 to a few times per real frame and is the only place that
/// changes the game's state; render() runs once per real frame, may only read that state and blend the last two
/// steps by RenderTime::alpha, and presents. Unlike the original, update() is given the step's time instead of reading
/// a global timer, so the engine can run with no real clock (test mode).
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

    /// Runs one simulation step: input, game logic, timers, animation, and the 2D sprites the GUI lists for the step.
    /// Draws nothing. A mode may push and pop modes on `stack` from here. Returning ModeResult::Leave pops the top of
    /// the stack, which is this mode unless it pushed another one this step (the original behaves the same).
    virtual ModeResult update(GameModeStack& stack, const FrameTime& frame) = 0;

    /// Draws one frame of the state the last updates left, blended by `time.alpha` where things move continuously,
    /// and presents it. Must not change anything update() reads: it may run any number of times between two updates
    /// (none at all is possible too). The stack renders the mode that ran the last step (GameModeStack::render()).
    virtual void render(const RenderTime& /*time*/) {}

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
